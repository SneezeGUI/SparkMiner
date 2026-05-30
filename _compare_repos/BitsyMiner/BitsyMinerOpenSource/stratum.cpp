/* 
 * BitsyMiner Open Source
 * Copyright (c) 2025 Justin Williams
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "defines_n_types.h"
#include "stratum.h"
#include "miner.h"
#include "utils.h"
#include "monitor.h"
#include "MyWiFi.h"
#include "MyWebServer.h"

unsigned long id = 1;


// typedef struct {
//   WiFiClient client;
//   uint64_t  hashesAtStart;
//   double    bestDifficulty;
//   uint32_t  connectMillis;
//   bool      isBackup;
//   char*     wallet;
// } StratumSession;


extern SetupData settings;
extern volatile bool isMining;
extern QueueHandle_t stratumMessageQueueHandle;
extern QueueHandle_t appMessageQueueHandle;
extern MonitorData monitorData;
extern double poolDifficulty;

// Prototype
void suggestDifficulty(WiFiClient& client, double difficulty);

uint32_t lastMiningNotify = 0;
unsigned long lastSubmitted = millis();
bool reconnect = false;  // Use this falg to force a reconnect


uint16_t submissionsNextPos = 0;
jobSubmitQueueEntry submissionsNeedingResponse[MAX_SUBMISSIONS_AWAITING_RESPONSE];


StaticJsonDocument<4096> doc;

stratum_block sb;

uint32_t acceptedSubmissions = 0;
uint32_t rejectedSubmissions = 0;
uint32_t lastHashrateCalc = millis();

const char* incomingMessageColor = "#ffffff";
const char* infoMessageColor = "#ffc133";

// Used for submissions
char* currentWallet;


// The web process can request a reconnect
bool requestStratumReconnect() {
  reconnect = true;
  return true;
}

//Get next JSON RPC Id
unsigned long getNextId() {
    if (id == ULONG_MAX) {
      id = 1;
      return id;
    }
    return ++id;
}

bool containsError() {
  if (!doc.containsKey("error")) return false;
  if (doc["error"].size() == 0) return false;
  dbg("ERROR: %d | reason: %s \n", (const int) doc["error"][0], (const char*) doc["error"][1]);
  return true; 
}


bool parseMiningNotify(String& line) {

  //https://github.com/MintPond/mtp-stratum-mining-protocol/blob/master/04_MINING.NOTIFY.md
  if( doc.containsKey("params") ) {

    lastMiningNotify = millis();

    sb.jobId = String((const char*) doc["params"][0]);
    sb.prevHash = String((const char*) doc["params"][1]);
    sb.coinBase1 = String((const char*) doc["params"][2]);
    sb.coinBase2 = String((const char*) doc["params"][3]);
    sb.merkleBranch = doc["params"][4];
    sb.version = String((const char*) doc["params"][5]);
    sb.difficulty = String((const char*) doc["params"][6]);
    sb.nTime = String((const char*) doc["params"][7]);
    sb.cleanJobs = doc["params"][8];

    startMiningJob(&sb);

  }
  return true;
}



// Submit a job from a queue entry
void prepareAndSubmit(WiFiClient& client, jobSubmitQueueEntry *sqEntry) {

  char msg[STRATUM_OUT_MESSAGE_SIZE];

  String ts = String(sqEntry->timestamp, HEX);
  String n = String(sqEntry->nonce, HEX);
  String vb = String(sqEntry->versionBits, HEX);

  char tStamp[9] = "00000000";
  if( ts.length() <= 8 ) {
    safeStrnCpy(&tStamp[8 - ts.length()], ts.c_str(), sizeof(tStamp));
  }

  char nonce[9] = "00000000";
  if( n.length() <= 8 ) {
    safeStrnCpy(&nonce[8 - n.length()], n.c_str(), sizeof(nonce));
  }
  
  char vbits[9] = "00000000";
  if( vb.length() <= 8 ) {
    safeStrnCpy(&vbits[8 - vb.length()], vb.c_str(), sizeof(vbits));
  }
  
  sqEntry->submissionMessageId = getNextId();

  snprintf(msg, STRATUM_OUT_MESSAGE_SIZE, "{\"id\": %lu, \"method\": \"mining.submit\", \"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\"]}\n", 
      sqEntry->submissionMessageId, 
        currentWallet, sqEntry->jobId, sqEntry->extraNonce2, tStamp, nonce, vbits);
  
  client.print(msg);

  dbg("Submitting: ");
  dbg("%s", msg);

  addToWebLog(msg);

  //Copy into our buffer of submissions awaiting response
  if( sqEntry->sessionMessageId ) {
    memcpy(&submissionsNeedingResponse[submissionsNextPos], sqEntry, sizeof(jobSubmitQueueEntry));
    if( ++submissionsNextPos >= MAX_SUBMISSIONS_AWAITING_RESPONSE ) {
      submissionsNextPos = 0;
    }
  }
  lastSubmitted = millis();
}

bool parseSubscribeResponse(String& line) {
  //JsonDocument doc;
  line.trim();
  if( line.length() == 0 ) {
    return false;
  }

  DeserializationError error = deserializeJson(doc, line);
  if( error || containsError() || ! doc.containsKey("result") ) {
    return false;
  }

  // Expecting an array type
  if( ! doc["result"].is<JsonArray>() ) return false;

   //mSubscribe.sub_details = String((const char*) doc["result"][0][0][1]);
  sb.extraNonce1 = String((const char*) doc["result"][1]);
  sb.extraNonce2Size = doc["result"][2];


  return true;
}

bool parseAuthorizeResponse(String& line) {
  //JsonDocument doc;
  line.trim();
  if( line.length() == 0) {    
    return false;
  }

  DeserializationError error = deserializeJson(doc, line);
  if( error || containsError() || ! doc.containsKey("result") ) {
    return false;
  }

  return true;
}

bool parseConfigResponse(String &line) {
  line.trim();
  if( line.length() == 0) {    
    return false;
  }

  addToWebLog(line);

  DeserializationError error = deserializeJson(doc, line);
  if( error || containsError() || ! doc["result"].is<JsonObject>() ) {
    return false;
  }

  uint32_t vmask = 0;

  if( doc["result"]["version-rolling"].is<bool>() && doc["result"]["version-rolling"] && 
    doc["result"]["version-rolling.mask"].is<const char*>() ) {
      vmask = strtoul(doc["result"]["version-rolling.mask"].as<const char*>(), NULL, 16);      
  }

  return true;
}



bool subscribe(WiFiClient& client, const char* wallet, const char* password) {

  int t;
  char msg[STRATUM_OUT_MESSAGE_SIZE];
  char minerName[MINER_NAME_LENGTH];
  char versionStr[15];

  id = 0;

  versionToString(versionStr, MINING_HARDWARE_VERSION_HEX);
  strcpy(minerName, MINING_HARDWARE_NAME);
  strcat(minerName, "/");
  strcat(minerName, versionStr);

  dbg("Miner Name: %s\n", minerName);

  // Subscribe
  id = getNextId();  
  snprintf(msg, STRATUM_OUT_MESSAGE_SIZE, "{\"id\": %lu, \"method\": \"mining.subscribe\", \"params\": [\"%s\"]}\n", id, minerName);
  dbg("Subscribe: %s\n", msg);
  client.print(msg);

  addToWebLog(msg);


  t = 300;
  while( ! client.available() && t-- > 0) {
    vTaskDelay(10/portTICK_PERIOD_MS);
  }

  if( client.available() ) {
    String resp = client.readStringUntil('\n');

    dbg("%s\n", resp.c_str());

    if( ! parseSubscribeResponse(resp) ) {
      dbg("Error parsing subscribe response\n");
      return false;
    }

  } else {
    dbg("No subscribe response.\n");
    return false;
  }


  // Authorize
  
  id = getNextId();
  snprintf(msg, STRATUM_OUT_MESSAGE_SIZE, "{\"id\": %lu, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}\n", id, 
      wallet, password);

  client.print(msg);
  dbg("Authorizing: %s\n", msg);
  
  addToWebLog(msg);

  t = 20;
  while( ! client.available() && t-- > 0) {
    vTaskDelay(50/portTICK_PERIOD_MS);
  }

  if( client.available() ) {
    String resp = client.readStringUntil('\n');
    dbg("%s\n", resp.c_str());
  }

  return true;

}

void parseSetDifficulty(String& line) {
  double difficulty;

  if( doc.containsKey("params") ) {
    difficulty = (double) doc["params"][0];
    if( ! isnan(difficulty) && difficulty > 0 ) {
      setPoolDifficulty(difficulty);
    }
  }
  
}


void parseMiningConfigure(String& line) {

}

bool handleServerMessage(WiFiClient& client) { 
  
  //JsonDocument doc;
  String line = client.readStringUntil('\n');
  line.trim();

  if( line.length() == 0) {
    return false;
  }
  
  
  addToWebLog(incomingMessageColor, line.c_str());

  dbg("Main Stratum Server: %s\n", line.c_str());

  DeserializationError error = deserializeJson(doc, line);
  if( error ) {
    return false;
  }

  // Look for messages that are responses to submissiosn
  if( doc.containsKey("id") && doc.containsKey("result") ) {

    uint32_t id = doc["id"];
    bool result = doc["result"];
    String err = doc["error"];

    for(int16_t i = 0; i < MAX_SUBMISSIONS_AWAITING_RESPONSE; i++) {

      if( submissionsNeedingResponse[i].submissionMessageId && submissionsNeedingResponse[i].submissionMessageId == id ) {
        // Call the callback if there is one
        if( submissionsNeedingResponse[i].callback ) {
          submissionsNeedingResponse[i].callback(
              submissionsNeedingResponse[i].sessionId, 
              submissionsNeedingResponse[i].sessionMessageId,
              result, err.c_str());
        }
        if( ! result ) {
          dbg("Rejected submission!\n");
        } else {
          // If it wasn't rejected, then update our stats
          if( submissionsNeedingResponse[i].submitflags & SUBMIT_FLAG_BLOCK_SOLUTION ) {
            monitorData.validBlocksFound++;
          }
          if( submissionsNeedingResponse[i].submitflags & SUBMIT_FLAG_32BIT ) {
            monitorData.blocks32Found++;
          }
          double difficulty = submissionsNeedingResponse[i].difficulty;

          if( ! isnan(difficulty) && ! isinf(difficulty) && 
            (isnan(monitorData.bestDifficulty) || isinf(monitorData.bestDifficulty) || difficulty >= monitorData.bestDifficulty) ) 
          {
            monitorData.bestDifficulty = difficulty;
          }
        }


        submissionsNeedingResponse[i].submissionMessageId = 0; // Just for good measure
        break;
      }
    }
  }


  if( doc.containsKey("method") ) {
    if( strcmp("mining.notify", (const char*) doc["method"]) == 0 ) {
      parseMiningNotify(line);
    } else if( strcmp("mining.set_difficulty", (const char*) doc["method"]) == 0) {
      parseSetDifficulty(line);
    } else if(strcmp("mining.configure", (const char*) doc["method"]) == 0 ) {
      parseMiningConfigure(line);
    } else {
      dbg("Stratum: unknown method\n");
    }
  } 

  return true;
}

void suggestDifficulty(WiFiClient& client, double difficulty) {
      
    char msg[STRATUM_OUT_MESSAGE_SIZE];

    // Subscribe
    unsigned long id = getNextId();
    snprintf(msg, STRATUM_OUT_MESSAGE_SIZE, "{\"id\": %lu, \"method\": \"mining.suggest_difficulty\", \"params\": [%.10g]}\n", id, difficulty);
    client.print(msg);

    addToWebLog(msg);
    
    dbg("Request Difficulty: %s\n", msg);

    lastSubmitted = millis();

}

void stopExternalMiners() {
  ApplicationMessage m;
  m.action = MAIN_ACTION_STOP_EXTERNAL_MINERS;
  xQueueSend(appMessageQueueHandle, &m, pdMS_TO_TICKS(150));
}

// Stop the stratum connection and stop mining
void stopClient(WiFiClient& client) {
  isMining = false;
  client.stop();
  stopExternalMiners();
  monitorData.poolConnected = false;
  monitorData.currentPool[0] = '\0';
  vTaskDelay(20 / portTICK_PERIOD_MS);
  xQueueReset(stratumMessageQueueHandle); // Clear the submission queue
}


void stratumTask(void *task_id) {
  
  bool usingBackup = false;
  WiFiClient client, altClient;

  jobSubmitQueueEntry sqEntry;

  dbg("\nBeginning stratum worker\n");

  uint32_t lastPoolConnectTime = 0;
  uint32_t backupConnectTime = 0;
  uint32_t lastDifficultyEval = 0;

  while( true ) {

    if(WiFi.status() != WL_CONNECTED || MyWiFi::isAccessPoint()) {        
      //Serial.println("Stratum: WiFi not connected");
      monitorData.wifiConnected = false;
      monitorData.poolConnected = false;
      if( isMining || client.connected() ) {
        stopClient(client);
      }
      vTaskDelay(500 / portTICK_PERIOD_MS);
      continue;
    }

    // If we're here, then we're connected to WiFi
    monitorData.wifiConnected = true;

    if( ! (strlen(settings.poolUrl) && settings.poolPort) ) {
      dbg("Stratum: No pool specified.\n");
      if( isMining || client.connected() ) {
        stopClient(client);
      }
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    }

    if( ! settings.wallet[0]  || ! settings.poolPassword[0] ) {
      dbg("Stratum: Wallet and/or pool password not set.\n");
      if( isMining || client.connected() ) {
        stopClient(client);
      }
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    }

    if(! client.connected()) {
      if( isMining || monitorData.poolConnected ) {
        stopClient(client);
      }
      usingBackup = false;
      //stratumCloseClientConnections(); // Close out anyone connected to us on our StratumServer
      dbg("*************************************************\nStratum: Attempting client connection...\n*************************************************\n");
      
      
      addToWebLog(infoMessageColor, "Connecting to primary pool.");

      if (!client.connect(settings.poolUrl, settings.poolPort)) {

        dbg("Connection failed.\n");

        addToWebLog(infoMessageColor, "Pool connection failed.");
        vTaskDelay(10000 / portTICK_PERIOD_MS);

        // See if it's time to try the backup
        if( millis() - lastPoolConnectTime > 30000) {
          if( strlen(settings.backupPoolUrl) && strlen(settings.backupPoolPassword) && strlen(settings.backupWallet) && settings.backupPoolPort ) {
            dbg("*** Attempting backup pool connection ***\n");
            addToWebLog(infoMessageColor, "Connecting to backup pool.");
            if( client.connect(settings.backupPoolUrl, settings.backupPoolPort) ) {              
              if( subscribe(client, settings.backupWallet, settings.backupPoolPassword) ) { // Try to subscribe to backup connection
                lastMiningNotify = millis();
                backupConnectTime = millis();
                currentWallet = settings.backupWallet;
                safeStrnCpy(monitorData.currentPool, settings.backupPoolUrl, MAX_POOL_URL_LENGTH + 1);
                usingBackup = true;
              } else {
                addToWebLog(infoMessageColor, "Backup pool connection failed.");
                client.stop();
              }
            } 
          }
        } else { 
          continue; // Not switching to backup, so jump back up and try again
        }     

      } else {
      
        // Reset last notify time to connection time
        lastMiningNotify = millis();
        
        // Make sure it's clear we're not on the
        // backup, in case this is the result of a settings update
        usingBackup = false;

        // Got connection, so try to subscribe
        if( ! subscribe(client, settings.wallet, settings.poolPassword) ) {
          stopClient(client);
          vTaskDelay(10000 / portTICK_PERIOD_MS);
          continue;
        } else {
          currentWallet = settings.wallet;
          safeStrnCpy(monitorData.currentPool, settings.poolUrl, MAX_POOL_URL_LENGTH + 1);
        }
      }

    }

    // If we've been on the bacup for a while, maybe think about switching back
    if( usingBackup && millis() - backupConnectTime > 120000 ) {
      addToWebLog(infoMessageColor, "Attempting reconnect to primary pool.");
      dbg("********** Attempt reconnect to main ***********\n");
      if( altClient.connect(settings.poolUrl, settings.poolPort) ) {
        if( subscribe(altClient, settings.wallet, settings.poolPassword) ) {
          stopClient(client);
          //stratumCloseClientConnections(); // Close out anyone connected to us on our StratumServer
          usingBackup = false;
          currentWallet = settings.wallet;
          safeStrnCpy(monitorData.currentPool, settings.poolUrl, MAX_POOL_URL_LENGTH + 1);
          client = altClient;
          addToWebLog(infoMessageColor, "Successful reconnect to primary pool.");
          continue;
        }
      }      
      // Still here, then reset backup time
      backupConnectTime = millis();
    }

    // If we're here, we're connected to a pool
    monitorData.poolConnected = true;
    lastPoolConnectTime = millis();

    // Handle any incoming messages
    while( client.available() > 0 ) {
      handleServerMessage(client);
    }

    // Look for submit messages on the queue
    while( uxQueueMessagesWaiting(stratumMessageQueueHandle) ) {
      if( xQueueReceive( stratumMessageQueueHandle, &sqEntry, 0 ) == pdTRUE ) {
        prepareAndSubmit(client, &sqEntry);
      }
    }


    // If we were asked to reconnect, let's do it
    if( reconnect ) {
      stopClient(client);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      reconnect = false;
    }

    if( millis() - lastSubmitted > 120000 ) {
      suggestDifficulty(client, DESIRED_DIFFICULTY);
    }

    // If we're not getting messages from the server, time to close the connection
    if( millis() - lastMiningNotify >= 700000 ) {
      addToWebLog(infoMessageColor, "No client activity. Disconnecting from pool.");
      dbg("******* DEAD CLIENT *******\n");
      stopClient(client);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
    
  }
}
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
#include "freertos/task.h"
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <ArduinoJson.h>
#include "defines_n_types.h"
#include "miner.h"
#include "stratum.h"
#include "monitor.h"
#include "MyWebServer.h"
#include "nvs_handler.h"
#include "esp_system.h"
#include "MyWiFi.h"
#include "utils.h"

#include "eventTask.h"
#include "esp_mac.h"

#include "esp_pm.h"

#include "esp32-hal-ledc.h"

#if defined(USE_SD_CARD)
  #include <SPI.h>
  #include <FS.h>
  #include <SD.h>
#endif

#define STRATUM_QUEUE_LENGTH 25
#define STRATUM_QUEUE_ITEM_SIZE sizeof(jobSubmitQueueEntry)

#define APPLICATION_MESSAGE_QUEUE_LENGTH 12


extern MonitorData monitorData;
TaskHandle_t mTask1, mTask2, strTaskHandle, monTaskHandle, webTaskHandle, eventTaskHandle, strServerTaskHandle = NULL;

static StaticQueue_t stratumQueueBuffer; // Static task messaging queue
uint8_t stratumQueueStorageArea[ STRATUM_QUEUE_LENGTH * STRATUM_QUEUE_ITEM_SIZE];
uint8_t appMessageBuffer[APPLICATION_MESSAGE_QUEUE_LENGTH * sizeof(ApplicationMessage)];


static StaticQueue_t applicationMessageQueueBuffer; // Static task messaging queue

QueueHandle_t stratumMessageQueueHandle;
QueueHandle_t appMessageQueueHandle;


SetupData settings;


#if defined(USE_SD_CARD)

void settingsFromSDCard() {

  if( !SD.begin() ) {
    dbg("Card mount failed\n");
    return;
  }

  uint8_t cardType = SD.cardType();
  if( cardType == CARD_NONE ) {
    dbg("No SD Card found.\n");
    return;
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  dbg("SD Card Size: %lluMB\n", cardSize);

  File file = SD.open("/config.json");
  if(! file) {
    dbg("Failed to open file.\n");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  bool saveTheSettings = true;

  if(! error) {
    if( doc.containsKey("ssid") ) {
      strlcpy(settings.ssid, doc["ssid"], MAX_SSID_LENGTH);      
    }
    if( doc.containsKey("ssidPassword") ) {
      strlcpy(settings.ssidPassword, doc["ssidPassword"], MAX_PASSWORD_LENGTH);
    }
    if( doc.containsKey("wallet") ) {
      strlcpy(settings.wallet, doc["wallet"], MAX_WALLET_LENGTH);
    }
    if( settings.ssid[0] && settings.ssidPassword[0] && settings.wallet[0] ) {
      settings.currentMode = MODE_INSTALL_COMPLETE;
    }    
    if( doc.containsKey("saveSettings") ) {
      saveTheSettings = doc["saveSettings"];
    }   
    if( saveTheSettings ) {
      saveSettings();
    }    
  } 
  file.close();
}

#endif //USE_SD_CARD


void ipCallback() {  
  if( ! MyWiFi::isAccessPoint() ) {
    IPAddress myIP = MyWiFi::getIP();
    if( myIP != INADDR_NONE ) {
      dbg("***********\nGot IP Address: %s\n***********\n", myIP.toString().c_str());

      String ip = myIP.toString();
      strncpy(monitorData.ipAddress, ip.c_str(), 15);      
    }    
  }
}

void setup() {

  char apName[32];

  Serial.begin(115200);
  Serial.setTimeout(0);
  delay(100);

  // Disable the watchdog timer entirely
  esp_task_wdt_deinit();

  // Build our access point name in case we need it
  buildAccessPointName(apName);

  // Set up Wifi event handling
  MyWiFi::setupEvents();
  MyWiFi::setIPCallback(ipCallback);

  // Tell our WiFi about access point name
  MyWiFi::setAccessPointInfo(apName, AP_SSID_PASSWORD);

  // Try to load settings and monitor data
  init_nvs();
  loadSettings();
  loadMonitorData();

#if defined(USE_SD_CARD)
  settingsFromSDCard();
#endif

  dbg("%x\n", settings.ipAddress);

  // Check if we are starting up in access point mode
  if( settings.currentMode == MODE_FACTORY_FRESH || settings.ssid[0] == 0 ) {

    MyWiFi::enterAccessPointMode();

    IPAddress myIP = WiFi.softAPIP();
    dbg("AP IP address: ");
    dbg("%s\n", myIP.toString().c_str());


  } else {

    MyWiFi::setSSIDInfo(settings.ssid, settings.ssidPassword);
    
    if( settings.staticIp ) {
      dbg("Attempting to set static IP");
      MyWiFi::configure(settings.ipAddress, settings.gateway, settings.subnet, settings.primaryDNS, settings.secondaryDNS);      
    } else {
      dbg("Static IP not set\n");
    }
    
    MyWiFi::enterStationMode();

  }

#ifdef USE_DISPLAY

  initializeDisplay(settings.screenRotation, settings.screenBrightness);  
  if( MyWiFi::isAccessPoint() ) {
    dbg("Set up as access point.\n");
    setCurrentScreen(SCREEN_ACCESS_POINT);  
  } else {
    dbg("Show mining screen\n");
    setCurrentScreen(SCREEN_MINING);
  }

#endif

  // Create a message queue for submitting jobs
  stratumMessageQueueHandle = xQueueCreateStatic(STRATUM_QUEUE_LENGTH, STRATUM_QUEUE_ITEM_SIZE, stratumQueueStorageArea, &stratumQueueBuffer);
  appMessageQueueHandle = xQueueCreateStatic(APPLICATION_MESSAGE_QUEUE_LENGTH, sizeof(ApplicationMessage), appMessageBuffer, &applicationMessageQueueBuffer);

  //disableCore0WDT();
  
  // Try disabling power management
  esp_pm_lock_handle_t pm_lock;
  esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "disable_sleep", &pm_lock);
  esp_pm_lock_acquire(pm_lock);
  dbg("Attempting to disable power management.\n");

  // Create miner tasks
  #ifndef SINGLE_CORE
    xTaskCreatePinnedToCore(minerTask, "Miner0", MINER_0_STACK_SIZE, (void*) 0, MINER_0_TASK_PRIORITY, &mTask1, MINER_0_CORE);
    xTaskCreatePinnedToCore(miner1Task, "Miner1", MINER_1_STACK_SIZE, (void*) 1, MINER_1_TASK_PRIORITY, &mTask2, MINER_1_CORE);
  #else
    xTaskCreatePinnedToCore(miner1Task, "Miner1", MINER_1_STACK_SIZE, (void*) 1, MINER_1_TASK_PRIORITY, &mTask2, 0);
  #endif
  
  xTaskCreatePinnedToCore(stratumTask, "Stratum", STRATUM_STACK_SIZE, NULL, STRATUM_TASK_PRIORITY, &strTaskHandle, STRATUM_CORE);
  xTaskCreatePinnedToCore(monitorTask, "Monitor", MONITOR_STACK_SIZE, NULL, MONITOR_TASK_PRIORITY, &monTaskHandle, MONITOR_CORE);
  xTaskCreatePinnedToCore(webTask, "WebServer", WEB_SERVER_STACK_SIZE, NULL, WEB_SERVER_TASK_PRIORITY, &webTaskHandle, WEB_SERVER_CORE);
  xTaskCreatePinnedToCore(eventTask, "EventServer", EVENT_HANDLER_STACK_SIZE, NULL, EVENT_HANDLER_TASK_PRIORITY, &eventTaskHandle, EVENT_HANDLER_CORE);


  #if defined(ESP32_2432S028) || defined(ESP32_2432S024)

    pinMode(LED1_RED_PIN, OUTPUT);
    pinMode(LED1_GREEN_PIN, OUTPUT);
    pinMode(LED1_BLUE_PIN, OUTPUT);

    #if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)

    ledcSetup(LED1_RED_CHANNEL, LED1_FREQUENCEY, LED1_RESOLUTION);
    ledcAttachPin(LED1_RED_PIN, LED1_RED_CHANNEL);

    ledcSetup(LED1_GREEN_CHANNEL, LED1_FREQUENCEY, LED1_RESOLUTION);
    ledcAttachPin(LED1_GREEN_PIN, LED1_GREEN_CHANNEL);
  
    ledcSetup(LED1_BLUE_CHANNEL, LED1_FREQUENCEY, LED1_RESOLUTION);
    ledcAttachPin(LED1_BLUE_PIN, LED1_BLUE_CHANNEL);

    #else

    ledcAttach(LED1_RED_PIN, LED1_FREQUENCEY, LED1_RESOLUTION);
    ledcAttach(LED1_GREEN_PIN, LED1_FREQUENCEY, LED1_RESOLUTION);
    ledcAttach(LED1_BLUE_PIN,LED1_FREQUENCEY, LED1_RESOLUTION);

    #endif

    ledcWrite(LED1_RED_CHANNEL, HIGH);
    ledcWrite(LED1_GREEN_CHANNEL, HIGH);
    ledcWrite(LED1_BLUE_CHANNEL, HIGH);

    ApplicationMessage am;
    am.action = MAIN_ACTION_LED1_SET;
    xQueueSend(appMessageQueueHandle, &am, pdMS_TO_TICKS(100));

  #endif
  
}



void loop() {
  vTaskDelay(200/portTICK_PERIOD_MS);
}


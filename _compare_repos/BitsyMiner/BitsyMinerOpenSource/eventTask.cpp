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
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "defines_n_types.h"
#include "utils.h"
#include "MyWiFi.h"
#include "nvs_handler.h"
#include "monitor.h"
#include "esp_wifi.h"
#include "miner.h"

typedef struct {
  unsigned long lastDownTime;
  unsigned long lastReleaseTime;
  unsigned long heldTime;
  bool pressed;
  bool released;
  bool clicked;
} Button;

Button button1 = {0, 0, 0, false, false, false};

extern SetupData settings;
extern MonitorData monitorData;

extern QueueHandle_t appMessageQueueHandle;

uint32_t lastScreenTouch = millis();
uint32_t lastDataSave = millis(); 
uint32_t lastWifiReconnect = millis();  // Last time we attempted to reconnect


WiFiUDP udp;

// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionally you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
NTPClient timeClient(udp, settings.ntpServer, 0, 60000);


// Timer interrupt
void IRAM_ATTR isr() {
  int val = digitalRead(PIN_INPUT);
  unsigned long m = millis();
  if( val == BUTTON_PRESSED ) {
    if(m - button1.lastDownTime > 250) {
      button1.lastDownTime = m;
      button1.pressed = true;      
    }
  } else {
    button1.pressed = false;
    button1.released = true;
    button1.lastReleaseTime = m;
    button1.heldTime = button1.lastReleaseTime - button1.lastDownTime;
    if( button1.heldTime >= 50 ) {
      button1.clicked = true;
    }        
  }
}


//////////////////////////////////////////////////////////////////////////////////////////
// Writes the mode back to factory fresh, forcing settings to revert
// to factory mode, then resets.
//////////////////////////////////////////////////////////////////////////////////////////
void factoryReset() {
  
  dbg("Do a factory reset\n");
  // Clear out settings
  //settings.currentMode = MODE_FACTORY_FRESH;
  delete_all_keys();

  //writeSettings();
  ESP.restart();
}

void turnOnAccessPoint() {
  MyWiFi::enterAccessPointMode();
  #ifdef USE_DISPLAY
    setCurrentScreen(SCREEN_ACCESS_POINT);
  #endif
}


void setLED1Levels() {

  //duty = (4095 / valueMax) * min(value, valueMax);

  #if defined(ESP32_2432S028) || defined(ESP32_2432S024)
    #if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      ledcWrite(LED1_RED_CHANNEL, (255 - settings.led1red) * 16 + 15); // Adding 15 to get it all the way to 4095 (all the way off)
      ledcWrite(LED1_GREEN_CHANNEL, (255 - settings.led1green) * 16 + 15);
      ledcWrite(LED1_BLUE_CHANNEL, (255 - settings.led1blue) * 16 + 15);
    #else
      ledcWrite(LED1_RED_PIN, (255 - settings.led1red) * 16 + 15); // Adding 15 to get it all the way to 4095 (all the way off)
      ledcWrite(LED1_GREEN_PIN, (255 - settings.led1green) * 16 + 15);
      ledcWrite(LED1_BLUE_PIN, (255 - settings.led1blue) * 16 + 15);
    #endif
  #endif

}

void screenActionHandler(ApplicationMessage* am) {


  #ifdef USE_DISPLAY
    if( am->action & MAIN_ACTION_SET_BRIGHTNESS ) {
      setBrightness(settings.screenBrightness);
    }
    if( am->action & MAIN_ACTION_SET_ROTATION ) {
      setRotation(settings.screenRotation);
    }
    if( am->action & MAIN_ACTION_GOTO_MAIN_SCREEN) {
      setCurrentScreen(SCREEN_MINING);
      lastScreenTouch = millis();     
    }
    if( am->action & MAIN_ACTION_SHOW_FIRMWARE_SCREEN ) {
      setCurrentScreen(SCREEN_FIRMWARE);
    }
    if( am->action & MAIN_ACTION_REDRAW ) {
      redraw();
    }
    if( am->action & MAIN_ACTION_REFRESH ) {
      // Get rid of back-to-back refresh requests
      ApplicationMessage m;

      //while( taskMessages.peekMessage(&m, 150) && m.action ==  MAIN_ACTION_REFRESH) {
      while( xQueuePeek(appMessageQueueHandle, &m, 0) == pdTRUE && m.action == MAIN_ACTION_REFRESH ) {
        //taskMessages.receiveMessage(&m, 150);
        xQueueReceive(appMessageQueueHandle, &m, 0);
      }
      refreshDisplay();
    }    
  #endif

}



// See if application message belong to the main process, and handl them if so
void handleApplicationMessage(ApplicationMessage* am) {

  if( am->action & MAIN_ACTION_NETWORK_CONNECT ) {
    MyWiFi::setSSIDInfo(settings.ssid, settings.ssidPassword);
    
    uint32_t ip = settings.staticIp ? settings.ipAddress : 0;
    MyWiFi::configure(ip, settings.gateway, settings.subnet, settings.primaryDNS, settings.secondaryDNS);
    MyWiFi::enterStationMode();
  }
  
  if( am->action & MAIN_ACTION_RESTART ) {
    ESP.restart();
  }
  
  if( am->action & MAIN_ACTION_LED1_SET ) {
    setLED1Levels();
  }

  screenActionHandler(am);
}


//////////////////////////////////////////////////////////////////////////////////////////
// Write command responses out to the serial port
//////////////////////////////////////////////////////////////////////////////////////////
void doSerialOutput(const char* str) {
  Serial.printf("%s\n", str);
  Serial.flush();
  vTaskDelay(100/portTICK_PERIOD_MS);
}

//////////////////////////////////////////////////////////////////////////////////////////
// Handle commands passed on the serial port
//////////////////////////////////////////////////////////////////////////////////////////
void handleSerialCommand(char *cmd) {

  char* resp = NULL;

  static const char* RESP_AP_MODE = "Entering access point mode...";
  static const char* RESP_FACTORY_RESET = "Performing factory reset...";

  static const char* CMD_ACCESS_POINT = "access point";
  static const char* CMD_FACTORY_RESET = "factory reset please";

  // Trim start
  while(cmd && cmd[0] && (cmd[0] == '\x20' || cmd[0] == '\x09')) cmd++; 

  // Trim end
  int i = strlen(cmd) - 1;
  while( cmd && cmd[i] && (cmd[0] == '\x20' || cmd[0] == '\x09') ) {
    cmd[i--] = '\0';      
  }

  if( cmd && cmd[0] ) {

    if( strlen(cmd) > 5 && strncmp("list ", cmd, 5) == 0 ) {
      if( strcmp("ip", &cmd[5]) == 0 ) {
        IPAddress ip = MyWiFi::getIP();
        doSerialOutput(ip.toString().c_str());
      } else if(strcmp("ssid", &cmd[5]) == 0 ) {
        if( settings.ssid[0] ) {
          doSerialOutput(settings.ssid);
        } else {
          doSerialOutput("No SSID set.");
        }
      } else if(strcmp("wallet", &cmd[5]) == 0 ) {
        doSerialOutput(settings.wallet);
      } else if(strcmp("pool", &cmd[5]) == 0 ) {
        doSerialOutput(settings.poolUrl);
      } else if(strcmp("mac", &cmd[5]) == 0) {
        char mac[17];
        getMacAddress(mac);
        doSerialOutput(mac);
      } else if(strcmp("model", &cmd[5]) == 0) {
        doSerialOutput(MINING_HARDWARE_MODEL);
      }
    } else if( strcmp(CMD_ACCESS_POINT, cmd) == 0 && ! MyWiFi::isAccessPoint() ) {
      doSerialOutput(RESP_AP_MODE);
      turnOnAccessPoint();
    } else if(  strcmp(CMD_FACTORY_RESET, cmd) == 0 ) {
      doSerialOutput(RESP_FACTORY_RESET);
      factoryReset();
    } 
  }

}


//////////////////////////////////////////////////////////////////////////////////////////
// Main event task for app
//////////////////////////////////////////////////////////////////////////////////////////
void eventTask(void *task_id) {

  int16_t serialCommandLength = 0;
  char serialCommand[256];

  // Set up the interrupt handler on the button
  pinMode(PIN_INPUT, INPUT_PULLUP);
  attachInterrupt(PIN_INPUT, isr, CHANGE);

  int16_t ntpFrequencyCounter = 0;  
  
  bool timeClientActive = false;

  while(1) {
  

    // Reconnect to WiFi if need be
    if( ! MyWiFi::isAccessPoint() && ! MyWiFi::isConnected() && ! MyWiFi::isConnecting() && millis() - lastWifiReconnect > WIFI_RECONNECT_TIME ) {
      dbg("Reconnecting WiFi...");
      MyWiFi::enterStationMode();
      lastWifiReconnect = millis();
    }

    if( ! timeClientActive && MyWiFi::isConnected() ) {
      dbg("Starting NTP client...");
      timeClient.begin();
      timeClientActive = true;
    }

    // Save statistics
    if( millis() - lastDataSave >= DATA_SAVE_FREQUENCY_MS ) {
      if( settings.saveMonitorData ) {
        dbg("Saving data....\n");
        saveMonitorData();
      }
      lastDataSave = millis();
    }


    if( button1.pressed ) {
      unsigned long l = millis() - button1.lastDownTime;
      if( l >= FACTORY_RESET_BUTTON_TIME ) {
        factoryReset();
      } 
    } else if( button1.clicked ) {
      if( button1.heldTime >= ACCESS_POINT_BUTTON_TIME ) {
        turnOnAccessPoint();
      } else {
        #if defined(USE_DISPLAY)      
          handleScreenTouch();
        #endif
      }
      button1.clicked = false;
    }

    if( uxQueueMessagesWaiting(appMessageQueueHandle) ) {
      ApplicationMessage m;      
      if( xQueueReceive(appMessageQueueHandle, &m, 0) == pdTRUE  ) {
          handleApplicationMessage(&m);
      }      
    }

    #ifdef USE_DISPLAY
    if( screenTouched() ) {
      lastScreenTouch = millis();
      setBrightness(settings.screenBrightness);
      
      int16_t c = 10;
      while( screenTouched() && c-- > 0) {
        vTaskDelay(20/portTICK_PERIOD_MS);
      }
      if( c < 4 ) {
        handleScreenTouch();
      }
      
    } else if( settings.inactivityTimer && millis() - lastScreenTouch > settings.inactivityTimer) {
      setBrightness(settings.inactivityBrightness); // Turn off the display
    }
    #endif



    // Every once in a while deal with the NTP stuff
    if( ++ntpFrequencyCounter % 10 == 0 && timeClientActive ) {
      timeClient.update();
      monitorData.currentTime = timeClient.getEpochTime();
      ntpFrequencyCounter = 0;
    }

    // Read commands off the serial port
    while( Serial.available() ) {   
      char receivedChar = Serial.read();
      if( receivedChar == 10 || receivedChar == 13 ) {
        if( serialCommandLength > 0 ) {
          handleSerialCommand(serialCommand);
        }        
        serialCommandLength = 0;        
        break;
      } 
      
      if( serialCommandLength < 254 ) {
        serialCommand[serialCommandLength++] = receivedChar;
        serialCommand[serialCommandLength] = '\0';
      }
    }

    vTaskDelay(100/portTICK_PERIOD_MS);  


  }

}
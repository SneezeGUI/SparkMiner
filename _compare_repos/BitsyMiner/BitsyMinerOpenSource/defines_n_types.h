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
#ifndef DEFINES_N_TYPES_H
#define DEFINES_N_TYPES_H

#define MINING_HARDWARE_NAME "BitsyMinerOpen"
#define MINING_HARDWARE_VERSION_HEX 0x010200  // One byte each - X.X.X

#define BYTESWAP32(z) ((uint32_t)((z&0xFF)<<24|((z>>8)&0xFF)<<16|((z>>16)&0xFF)<<8|((z>>24)&0xFF)))

// Original BitsyMiner with support for online firmware updates
// compiled as follows:
// Minimal SPIFFS 1.9MB APP w/OTA 190KB SPIFFS
// Events on core 0
// Arduino on core 0

// Uncomment one of the following
//#define ESP32_2432S028  // TOUCH SCREEN  ESP32 Dev Module 2.8"
//#define ESP32_2432S024  // TOUCH SCREEN  ESP32 Dev Module 2.4"
//#define ESP32_DEV_HEADLESS  // Compile without graphical support

// Uncomment to compile for ST7789 LCD
//#define ST7789_LCD


// Uncomment the following line to see debug messages on the serial console
#define DEBUG_MESSAGES

#if defined(DEBUG_MESSAGES)
#define dbg(...) Serial.printf(__VA_ARGS__)
#else
#define dbg(...) (void)0
#endif


#define AP_SSID "Bitsy_"
#define AP_SSID_PASSWORD "bitsyminer"

#define FACTORY_RESET_BUTTON_TIME 10000
#define ACCESS_POINT_BUTTON_TIME 4000

// Value on pin when button pressed
#define BUTTON_PRESSED 0


#if defined(ESP32_2432S028)
  #if defined(ST7789_LCD)
    #define MINING_HARDWARE_MODEL "BTC-ESP32-2432S028-ST7789"
  #else
    #define MINING_HARDWARE_MODEL "BTC-ESP32-2432S028"
  #endif
  #define USE_DISPLAY
  #define USE_SD_CARD
  #define LED1_RED_PIN 4
  #define LED1_GREEN_PIN 16
  #define LED1_BLUE_PIN 17
  #define LED1_RED_CHANNEL 1
  #define LED1_GREEN_CHANNEL 2
  #define LED1_BLUE_CHANNEL 3
  #define LED1_FREQUENCEY 5000
  #define LED1_RESOLUTION 12
  #define PIN_INPUT 0
  #include "display.h"
#elif defined(ESP32_2432S024)
  #define MINING_HARDWARE_MODEL "BTC-ESP32-2432S024"
  #define USE_DISPLAY
  #define USE_SD_CARD
  #define LED1_RED_PIN 4
  #define LED1_GREEN_PIN 17
  #define LED1_BLUE_PIN 16
  #define LED1_RED_CHANNEL 1
  #define LED1_GREEN_CHANNEL 2
  #define LED1_BLUE_CHANNEL 3
  #define LED1_FREQUENCEY 5000
  #define LED1_RESOLUTION 12
  #define PIN_INPUT 0
  #include "display.h"
#elif defined(ESP32_DEV_HEADLESS)
  #define MINING_HARDWARE_MODEL "BTC-ESP32_DEV_HEADLESS"
  #define PIN_INPUT 0
#endif


#define DATA_SAVE_FREQUENCY_MS 3600000 // Once an hour

#define MODE_FACTORY_FRESH 0x0
#define MODE_INSTALL_COMPLETE 0x0707

#define MAX_USERNAME_LENGTH 32
#define MAX_SSID_LENGTH 63
#define MAX_PASSWORD_LENGTH 64
#define MIN_PASSWORD_LENGTH 8

#define MAX_POOL_URL_LENGTH 80
#define MAX_WALLET_LENGTH 120
#define MAX_POOL_PASSWORD_LENGTH 80

#define SETUP_EEPROM_DATA_ADDRESS 0

#define TICK_DELAY 100

#define WEB_SERVER_TASK_PRIORITY 5
#define WEB_SERVER_CORE 0
#define WEB_SERVER_STACK_SIZE 6000

#define STRATUM_TASK_PRIORITY 2
#define STRATUM_CORE 0
#define STRATUM_STACK_SIZE 7000

#define MONITOR_TASK_PRIORITY 1
#define MONITOR_CORE 0
#define MONITOR_STACK_SIZE 4000

#define MINER_0_CORE 0
#define MINER_0_TASK_PRIORITY 1
#define MINER_0_STACK_SIZE 5000

#define MINER_1_CORE 1
#define MINER_1_TASK_PRIORITY 19
#define MINER_1_STACK_SIZE 5000

#define EVENT_HANDLER_CORE 0
#define EVENT_HANDLER_TASK_PRIORITY 1
#define EVENT_HANDLER_STACK_SIZE 8000

#define STRATUM_SERVER_CORE 0
#define STRATUM_SERVER_TASK_PRIORITY 1
#define STRATUM_SERVER_STACK_SIZE 6000

#define CORE_0_YIELD_COUNT 1000 // How many hash cycles to go through before yielding to other tasks

#define MINER_NAME_LENGTH 60

#define NTP_UPDATE_INTERVAL 600000L // 1000 * 60 * 10 = 10 MINUTES
#define NTP_PACKET_SIZE 40

#define WIFI_RECONNECT_TIME 10000


#define EEPROM_SIZE sizeof(EepromSetupData)




#define MAIN_ACTION_NETWORK_CONNECT 1
#define MAIN_ACTION_GOTO_MAIN_SCREEN 2
#define MAIN_ACTION_SET_BRIGHTNESS 4
#define MAIN_ACTION_REDRAW 8
#define MAIN_ACTION_REFRESH 16
#define MAIN_ACTION_SET_ROTATION 32
#define MAIN_ACTION_SHOW_FIRMWARE_SCREEN 64
#define MAIN_ACTION_RESTART 128
#define MAIN_ACTION_LED1_SET 256
#define MAIN_ACTION_SEND_DIFFICULTY 512
#define MAIN_ACTION_SEND_JOB_ID 1024
#define MAIN_ACTION_SEND_HASH_BLOCK 2048
#define MAIN_ACTION_STOP_EXTERNAL_MINERS 4096


#define STRATUM_ACTION_RECONNECT 1

#define MAX_JOB_ID_LENGTH 64

typedef struct {
  uint16_t currentMode;
  char ssid[MAX_SSID_LENGTH + 1];
  char ssidPassword[MAX_PASSWORD_LENGTH + 1];
  char htaccessUser[MAX_USERNAME_LENGTH + 1];
  char htaccessPassword[MAX_PASSWORD_LENGTH + 1];
  char poolUrl[MAX_POOL_URL_LENGTH + 1];
  char backupPoolUrl[MAX_POOL_URL_LENGTH + 1];
  char ntpServer[MAX_POOL_URL_LENGTH + 1];
  int poolPort;
  int backupPoolPort;
  char wallet[MAX_WALLET_LENGTH + 1];
  char backupWallet[MAX_WALLET_LENGTH + 1];
  char poolPassword[MAX_POOL_PASSWORD_LENGTH + 1];
  char backupPoolPassword[MAX_POOL_PASSWORD_LENGTH + 1];
  uint8_t screenRotation;
  bool randomizeTimestamp;
  uint8_t screenBrightness;
  uint32_t inactivityTimer;
  uint8_t inactivityBrightness;
  uint32_t foregroundColor;
  uint32_t backgroundColor;
  bool staticIp;
  uint32_t ipAddress;
  uint32_t gateway;
  uint32_t subnet;
  uint32_t primaryDNS;
  uint32_t secondaryDNS;
  int32_t utcOffset;
  bool saveMonitorData; 
  bool clock24;
  bool stratumRepeater;
  bool reportIP;
  bool supportAsicBoost;
  bool coreZeroDisabled;
  bool invertColors;
  bool enableLogViewer;
  uint8_t led1red;
  uint8_t led1green;
  uint8_t led1blue;
  uint8_t webTheme;
} SetupData;


typedef struct {
  uint16_t  action;
  void*     data;
  uint32_t  len;
} ApplicationMessage;



#endif

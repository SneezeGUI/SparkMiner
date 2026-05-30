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
#include <WiFi.h>
#include <esp_wifi.h>
#include "defines_n_types.h"
#include "monitor.h"
#include "miner.h"
#include "utils.h"
#include "MyWiFi.h"


MonitorData monitorData = {};

extern volatile bool isMining;
extern QueueHandle_t appMessageQueueHandle;



void uptimeToString(char* dest, uint64_t uptime) {

  uptime /= 1000;                   // Convert to seconds
  uint16_t days = uptime / 86400;   // Get days
  uptime %= 86400;
  uint16_t hours = uptime / 3600;   // Get hours
  uptime %= 3600;
  uint16_t minutes = uptime / 60;   // Get minutes
  uptime %= 60;
  uint16_t seconds = uptime;

  sprintf(dest, "%dd %dh %dm %ds", days, hours, minutes, seconds);
}

void monitorTask(void *task_id) {

  ApplicationMessage appMessage;

  static uint32_t lastMillis = millis(); 
  //static unsigned long long lastTotalHashes = monitorData.totalHashes;

  static uint32_t lastValidBlocks = 0xffffffff;
  //static uint32_t last16 = 0xffffffff;
  static uint32_t last32 = 0xffffffff;
  static uint32_t lastTotalJobs = 0xffffffff;
  static uint32_t lastPoolSubs = 0xffffffff;
  static double lastBestDifficulty = -1.0;
  static uint64_t lastInternalHashes = 0;

  dbg("Beginning monitor worker\n");

  // Get MAC address at start
  getMacAddress(monitorData.macAddress);

  while( true ) {

    //unsigned long currentMillis = millis();
    unsigned long millisDiff = millis() - lastMillis;
    
    if( millisDiff >= 900 ) {

      uint64_t tHashes = monitorData.internalHashes - lastInternalHashes;
      
      monitorData.uptime = bigMillis();
      uptimeToString(monitorData.uptimeStr, monitorData.uptime);
      
      monitorData.isMining = isMining;
      //monitorData.hashesPerSecond = (double) (tHashes - lastTotalHashes) / (double) millisDiff;
      monitorData.hashesPerSecond = (double) tHashes / (double) millisDiff;
      monitorData.totalHashes += tHashes;
      
      lastInternalHashes = monitorData.internalHashes;

      if(! isnan(monitorData.externalHashesPerSecond) ) {
        monitorData.hashesPerSecond += monitorData.externalHashesPerSecond;
      }
      
      dtostrf(monitorData.hashesPerSecond, 3, 2, monitorData.hashesPerSecondStr);
      
      formatBigNumber(monitorData.totalHashesStr, monitorData.totalHashes);

      if( monitorData.bestDifficulty != lastBestDifficulty ) {
        formatBigNumber(monitorData.bestDifficultyStr, monitorData.bestDifficulty);
        lastBestDifficulty = monitorData.bestDifficulty;
      }

      if( lastPoolSubs != monitorData.poolSubmissions ) {
        formatBigNumber(monitorData.poolSubmissionsStr, monitorData.poolSubmissions);
        lastPoolSubs = monitorData.poolSubmissions;
      }
            
      if( last32 != monitorData.blocks32Found ) {
        formatBigNumber(monitorData.blocks32FoundStr, monitorData.blocks32Found);
        last32 = monitorData.blocks32Found;
      }
      
      if( lastTotalJobs != monitorData.totalJobs ) {
        formatBigNumber(monitorData.totalJobsStr, monitorData.totalJobs);
        lastTotalJobs = monitorData.totalJobs;
      }
      
      if( lastValidBlocks != monitorData.validBlocksFound ) {
        formatBigNumber(monitorData.validBlocksFoundStr, monitorData.validBlocksFound);
        lastValidBlocks = monitorData.validBlocksFound;
      }
      
      appMessage.action = MAIN_ACTION_REFRESH;
      xQueueSend(appMessageQueueHandle, &appMessage, 0);

      lastMillis = millis();

    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

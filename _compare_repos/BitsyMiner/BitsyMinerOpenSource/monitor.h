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
#ifndef MONITOR_H
#define MONITOR_H

typedef struct {
  uint64_t totalHashes;
  uint64_t uptime;
  double hashesPerSecond;
  uint64_t internalHashes;
  double externalHashesPerSecond; // from other devices
  double bestDifficulty;
  double poolDifficulty;
  uint32_t validBlocksFound;
  uint32_t blocks16Found;
  uint32_t blocks32Found;
  uint32_t poolSubmissions;
  uint32_t totalJobs;
  uint32_t currentTime;
  char bestDifficultyStr[20];
  char hashesPerSecondStr[20];
  char totalHashesStr[20];
  char poolSubmissionsStr[20];
  char blocks16FoundStr[20];
  char blocks32FoundStr[20];
  char totalJobsStr[20];
  char validBlocksFoundStr[20];
  char uptimeStr[20];
  char macAddress[20];
  char ipAddress[16];
  char currentPool[81];
  bool isMining;
  bool wifiConnected;
  bool poolConnected;
  uint32_t blockHeight;
  uint16_t firmwareUpdatePercentage;
  uint32_t espNowDiscoverableTime;
  uint32_t sessionPoolSubmissions;
  uint32_t sessionPoolRejects;
} MonitorData;

// void updateTotalHashes();
void monitorTask(void *task_id);


#endif

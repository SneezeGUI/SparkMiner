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
#ifndef MINER_H
#define MINER_H

#include "stratum.h"


#ifdef ESP32C3

  #define HASH_AREA_SHA256 SHA_H_BASE
  #define MSG_AREA_SHA256  SHA_TEXT_BASE

  #define INIT_HARDWARE_SHA256 \
      DPORT_REG_WRITE(SHA_MODE_REG, 2); // Sets mode to SHA256

  #define START_HARDWARE_SHA256 \
      DPORT_REG_WRITE(SHA_START_REG, 1); \
      while(DPORT_REG_READ(SHA_BUSY_REG) != 0);

  #define CONTINUE_HARDWARE_SHA256 \
      DPORT_REG_WRITE(SHA_START_REG, 1); \
      while(DPORT_REG_READ(SHA_BUSY_REG) != 0);

  #define LOAD_HARDWARE_SHA256

  #define HASH_TO_MSG_SHA256 \
     memcpy((void*)dataReg, (const void*) hashReg, 64);

#else

  #define HASH_AREA_SHA256 SHA_TEXT_BASE  
  #define MSG_AREA_SHA256 SHA_TEXT_BASE

  #define INIT_HARDWARE_SHA256 \
      DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA); \
      DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);

#endif


#define DEFAULT_DIFFICULTY 0x1dffff // As per stratum

typedef struct {
  unsigned long version;
  unsigned char prev_hash[32];
  unsigned char merkle_root[32];
  unsigned long timestamp;
  unsigned long difficulty;
  unsigned long nonce;
} hash_block;

void minerTask(void *task_id);
void miner1Task(void *task_id);
void setExtraNonce(const char* en);
void setExtraNonce2Length(size_t len);
void startMiningJob(stratum_block *sb);
void setPoolDifficulty(double pDiff);


#endif // MINER_H

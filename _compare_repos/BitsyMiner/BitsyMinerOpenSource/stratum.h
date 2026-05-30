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
#ifndef STRATUM_H
#define STRATUM_H

#include <ArduinoJson.h>
#include "defines_n_types.h"

#define DESIRED_DIFFICULTY 0.0014
#define STRATUM_OUT_MESSAGE_SIZE 512

#define MAX_SUBMISSIONS_AWAITING_RESPONSE 30

typedef void (*StratumSubmitCallback)(uint32_t, uint32_t, bool, const char*);

#define SUBMIT_FLAG_32BIT 2
#define SUBMIT_FLAG_BLOCK_SOLUTION 4

typedef struct {
  String jobId;
  String prevHash;
  String coinBase1;
  String coinBase2;
  String extraNonce1;
  String extraNonce2;
  int extraNonce2Size;  
  JsonArray merkleBranch;
  String version;
  String difficulty;
  String nTime;
  bool cleanJobs;
} stratum_block;


typedef struct {
  char jobId[MAX_JOB_ID_LENGTH];
  char extraNonce2[20];
  uint32_t timestamp;
  uint32_t nonce;
  uint32_t submissionMessageId;
  uint32_t sessionId;
  uint32_t sessionMessageId;
  uint32_t versionBits;
  uint32_t submitflags;
  double difficulty;
  StratumSubmitCallback callback;
} jobSubmitQueueEntry;


bool requestStratumReconnect();
void stratumTask(void *task_id);

#endif
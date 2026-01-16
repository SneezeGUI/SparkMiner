/*
 * SparkMiner - Mining Core
 * High-performance Bitcoin mining for ESP32
 *
 * Based on BitsyMiner by Justin Williams (GPL v3)
 *
 * Features:
 * - Dual-core mining (Core 0 + Core 1)
 * - Midstate optimization (75% less work per hash)
 * - Hardware SHA-256 via direct register access (300-500 KH/s)
 */

#ifndef MINER_H
#define MINER_H

#include <Arduino.h>
#include "sha256_types.h"
#include "sha256_hw.h"
#include "../stratum/stratum_types.h"

/* Hardcode Values for FW config at build time - usually dev and test purpose */
/* Define HARDCODE_POOLS to do this */
#define HARDCODED_WALLET_PRIM   ""
#define HARDCODED_WALLET_BKP    ""
const char h_primWallet[] = HARDCODED_WALLET_PRIM;
const char h_bkpWallet[] = HARDCODED_WALLET_BKP;

#define HARDCODED_POOL_PRIM     "public-pool.io"
#define HARDCODED_POOL_BKP      "public-pool.io"
const char h_primPool[] = HARDCODED_POOL_PRIM;
const char h_bkpPool[] = HARDCODED_POOL_BKP;

#define HARDCODED_PORT_PRIM  21496 
#define HARDCODED_PORT_BKP   21496
const int h_primPort = HARDCODED_PORT_PRIM;
const int h_bkpPort = HARDCODED_PORT_BKP;

#define HARDCODED_WORKER_PRIM   ""
#define HARDCODED_WORKER_BKP    ""
const char h_primWorker[] = HARDCODED_WORKER_PRIM;
const char h_bkpWorker[] = HARDCODED_WORKER_BKP;

/**
 * Initialize mining subsystem
 * - Disables watchdog timer
 * - Disables power management (no sleep)
 * - Creates mining tasks on both cores
 */
void miner_init();

/**
 * Start mining with new job
 * Called when pool sends mining.notify
 *
 * @param job Stratum job from pool
 */
void miner_start_job(const stratum_job_t *job);

/**
 * Stop mining
 * Called on pool disconnect or shutdown
 */
void miner_stop();

/**
 * Check if mining is active
 */
bool miner_is_running();

/**
 * Get current mining statistics
 */
mining_stats_t* miner_get_stats();

/**
 * Mining task for Core 0 (software SHA, lower priority)
 * Yields periodically to allow WiFi/Stratum/Display tasks
 */
void miner_task_core0(void *param);

/**
 * Mining task for Core 1 (dedicated, high priority)
 * Uses pipelined SHA for maximum throughput
 */
void miner_task_core1(void *param);

/**
 * Set pool difficulty for share validation
 */
void miner_set_difficulty(double poolDifficulty);

/**
 * Set extra nonce from pool subscription
 */
void miner_set_extranonce(const char *extraNonce1, int extraNonce2Size);

#endif // MINER_H

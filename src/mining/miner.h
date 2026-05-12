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

typedef struct {
	const char *chip;
	const char *miningBackend;
	bool hwShaAvailable;
	bool hwShaHotLoop;
	bool softwareMidstate;
	bool dmaHotPath;
	bool midstateRestoreSupported;
	bool nonceSplitCore0LowCore1High;
} miner_backend_info_t;

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
 * Mining task for Core 0
 * - ESP32-S3: software midstate-complete path (nonce low half)
 * - ESP32: hybrid/software helper path
 * Yields periodically to allow WiFi/Stratum/Display tasks
 */
void miner_task_core0(void *param);

/**
 * Mining task for Core 1 (dedicated, high priority)
 * - ESP32-S3: software midstate-complete path (nonce high half)
 * - ESP32: pipelined SHA hardware path
 */
void miner_task_core1(void *param);

/**
 * Set pool difficulty for share validation
 */
void miner_set_difficulty(double poolDifficulty);

/**
 * Get current pool difficulty
 */
double miner_get_difficulty();

/**
 * Set extra nonce from pool subscription
 */
void miner_set_extranonce(const char *extraNonce1, int extraNonce2Size);

/**
 * Backend capabilities and active-mode summary.
 * Single source of truth for startup/status reporting.
 */
const miner_backend_info_t *miner_get_backend_info();

#endif // MINER_H

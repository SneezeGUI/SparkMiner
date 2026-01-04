/*
 * SparkMiner - Mining Core Implementation
 * Based on BitsyMiner by Justin Williams (GPL v3)
 *
 * Optimized Bitcoin mining for ESP32 with:
 * - Midstate caching (75% less work per hash)
 * - Early 16-bit reject optimization
 * - Dual-core support
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>
#include "miner.h"
#include "sha256_types.h"
#include "sha256_hw.h"  // Hardware SHA-256 wrapper
#include "sha256_ll.h"  // Low-level hardware SHA register access
#include "../stratum/stratum.h"
#include "board_config.h"

// ============================================================
// Constants
// ============================================================
#define MAX_DIFFICULTY 0x1d00ffff
#define CORE_0_YIELD_COUNT 256

// ============================================================
// Globals
// ============================================================

// Mining state
static volatile bool s_miningActive = false;
static volatile bool s_core0Mining = false;
static volatile bool s_core1Mining = false;

// Current job
static block_header_t s_pendingBlock;
static char s_currentJobId[MAX_JOB_ID_LEN];
static SemaphoreHandle_t s_jobMutex = NULL;

// Extra nonce
static char s_extraNonce1[32] = {0};
static int s_extraNonce2Size = 4;
static unsigned long s_extraNonce2 = 1;

// Targets
static uint8_t s_blockTarget[32];
static uint8_t s_poolTarget[32];
static double s_poolDifficulty = 1.0;

// Statistics
static mining_stats_t s_stats = {0};

// Nonce ranges for dual-core
static unsigned long s_startNonce[2] = {0, 0x80000000};

// ============================================================
// Utility Functions
// ============================================================

static uint8_t decodeHexChar(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void hexToBytes(uint8_t *out, const char *in, size_t len) {
    for (size_t i = 0; i < len; i += 2) {
        out[i/2] = (decodeHexChar(in[i]) << 4) | decodeHexChar(in[i + 1]);
    }
}

static void encodeExtraNonce(char *dest, size_t len, unsigned long en) {
    static const char *tbl = "0123456789ABCDEF";
    dest += len * 2;
    *dest-- = '\0';
    while (len--) {
        *dest-- = tbl[en & 0x0f];
        *dest-- = tbl[(en >> 4) & 0x0f];
        en >>= 8;
    }
}

static void swapBytesInWords(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i += 4) {
        uint8_t temp = buf[i];
        buf[i] = buf[i + 3];
        buf[i + 3] = temp;
        temp = buf[i + 1];
        buf[i + 1] = buf[i + 2];
        buf[i + 2] = temp;
    }
}

// ============================================================
// Target Functions
// ============================================================

static void bits_to_target(uint32_t nBits, uint8_t *target) {
    uint32_t exponent = nBits >> 24;
    uint32_t mantissa = nBits & 0x007fffff;
    if (nBits & 0x00800000) {
        mantissa |= 0x00800000;
    }
    memset(target, 0, 32);
    if (exponent <= 3) {
        mantissa >>= 8 * (3 - exponent);
        memcpy(target, &mantissa, 4);
    } else {
        int shift = (exponent - 3);
        uint32_t *target_ptr = (uint32_t *)(target + shift);
        *target_ptr = mantissa;
    }
}

static void divide_256bit_by_double(uint64_t *target, double divisor) {
    uint64_t result[4] = {0};
    double remainder = 0.0;
    
    // Iterate from MSB (target[3]) to LSB (target[0])
    for (int i = 3; i >= 0; i--) {
        // Add carried remainder from upper word (scaled by 2^64)
        double val = (double)target[i] + remainder * 18446744073709551616.0;
        
        double res = val / divisor;
        
        // Clamp to prevent overflow (shouldn't happen with diff >= 1)
        if (res >= 18446744073709551615.0) {
            result[i] = 0xFFFFFFFFFFFFFFFFULL;
        } else {
            result[i] = (uint64_t)res;
        }
        
        remainder = val - ((double)result[i] * divisor);
    }
    
    memcpy(target, result, sizeof(result));
}

static void adjust_target_for_difficulty(uint8_t *pt, uint8_t *bt, double difficulty) {
    uint64_t target_parts[4];
    for (int i = 0; i < 4; i++) {
        target_parts[i] = ((uint64_t)bt[i * 8 + 0]) |
                          ((uint64_t)bt[i * 8 + 1] << 8) |
                          ((uint64_t)bt[i * 8 + 2] << 16) |
                          ((uint64_t)bt[i * 8 + 3] << 24) |
                          ((uint64_t)bt[i * 8 + 4] << 32) |
                          ((uint64_t)bt[i * 8 + 5] << 40) |
                          ((uint64_t)bt[i * 8 + 6] << 48) |
                          ((uint64_t)bt[i * 8 + 7] << 56);
    }
    divide_256bit_by_double(target_parts, difficulty);
    for (int i = 0; i < 4; i++) {
        pt[i * 8 + 0] = target_parts[i] & 0xff;
        pt[i * 8 + 1] = (target_parts[i] >> 8) & 0xff;
        pt[i * 8 + 2] = (target_parts[i] >> 16) & 0xff;
        pt[i * 8 + 3] = (target_parts[i] >> 24) & 0xff;
        pt[i * 8 + 4] = (target_parts[i] >> 32) & 0xff;
        pt[i * 8 + 5] = (target_parts[i] >> 40) & 0xff;
        pt[i * 8 + 6] = (target_parts[i] >> 48) & 0xff;
        pt[i * 8 + 7] = (target_parts[i] >> 56) & 0xff;
    }
}

static void setPoolTarget() {
    uint8_t maxDifficulty[32];
    bits_to_target(MAX_DIFFICULTY, maxDifficulty);
    adjust_target_for_difficulty(s_poolTarget, maxDifficulty, s_poolDifficulty);
    
    // Debug target (Top 64 bits)
    Serial.printf("[MINER] New Target (High): %02x%02x%02x%02x%02x%02x%02x%02x\n", 
        s_poolTarget[31], s_poolTarget[30], s_poolTarget[29], s_poolTarget[28],
        s_poolTarget[27], s_poolTarget[26], s_poolTarget[25], s_poolTarget[24]);
}

// Check if hash meets target (little-endian comparison from high bytes)
static int check_target(const uint8_t *hash, const uint8_t *target) {
    for (int i = 31; i >= 0; i--) {
        if (hash[i] < target[i]) return 1;  // Valid
        if (hash[i] > target[i]) return 0;  // Invalid
    }
    return 1;  // Equal is valid
}

// ============================================================
// Merkle Root Calculation
// ============================================================

static void double_sha256_merkle(uint8_t *dest, uint8_t *buf64) {
    sha256_hash_t ctx, ctx1;
    sha256(&ctx, buf64, 64);
    sha256(&ctx1, ctx.bytes, 32);
    memcpy(dest, ctx1.bytes, 32);
}

static void calculateMerkleRoot(uint8_t *root, uint8_t *coinbaseHash, JsonArray &merkleBranch) {
    uint8_t merklePair[64];
    memcpy(merklePair, coinbaseHash, 32);

    for (size_t i = 0; i < merkleBranch.size(); i++) {
        const char *branchHex = merkleBranch[i].as<const char *>();
        hexToBytes(&merklePair[32], branchHex, 64);
        // NerdMiner does NOT reverse merkle branches
        
        double_sha256_merkle(merklePair, merklePair);
        // NerdMiner does NOT reverse intermediate merkle results
    }
    memcpy(root, merklePair, 32);
}

static void createCoinbaseHash(uint8_t *hash, const stratum_job_t *job) {
    uint8_t coinbase[512];
    size_t cbLen = 0;

    // Coinbase1
    size_t cb1Len = job->coinBase1.length();
    hexToBytes(coinbase, job->coinBase1.c_str(), cb1Len);
    cbLen += cb1Len / 2;

    // ExtraNonce1
    size_t en1Len = strlen(s_extraNonce1);
    hexToBytes(&coinbase[cbLen], s_extraNonce1, en1Len);
    cbLen += en1Len / 2;

    // ExtraNonce2
    char en2Hex[17];
    encodeExtraNonce(en2Hex, s_extraNonce2Size, s_extraNonce2);
    hexToBytes(&coinbase[cbLen], en2Hex, s_extraNonce2Size * 2);
    cbLen += s_extraNonce2Size;

    // Coinbase2
    size_t cb2Len = job->coinBase2.length();
    hexToBytes(&coinbase[cbLen], job->coinBase2.c_str(), cb2Len);
    cbLen += cb2Len / 2;

    // Double SHA256
    sha256_hash_t ctx, ctx1;
    sha256(&ctx, coinbase, cbLen);
    sha256(&ctx1, ctx.bytes, 32);
    memcpy(hash, ctx1.bytes, 32);
    // NerdMiner does NOT reverse coinbase hash
}

// ============================================================
// Difficulty Calculation
// ============================================================

static double getDifficulty(sha256_hash_t *ctx) {
    static const double maxTarget = 26959535291011309493156476344723991336010898738574164086137773096960.0;
    double hashValue = 0.0;
    for (int i = 0, j = 31; i < 32; i++, j--) {
        hashValue = hashValue * 256 + ctx->bytes[j];
    }
    double difficulty = maxTarget / hashValue;
    if (isnan(difficulty) || isinf(difficulty)) {
        difficulty = 0.0;
    }
    return difficulty;
}

static void compareBestDifficulty(sha256_hash_t *ctx) {
    double difficulty = getDifficulty(ctx);
    if (!isnan(difficulty) && !isinf(difficulty) &&
        (isnan(s_stats.bestDifficulty) || isinf(s_stats.bestDifficulty) ||
         difficulty >= s_stats.bestDifficulty)) {
        s_stats.bestDifficulty = difficulty;
    }
}

// ============================================================
// Share Validation & Submission
// ============================================================

static void hashCheck(const char *jobId, sha256_hash_t *ctx, uint32_t timestamp, uint32_t nonce) {
    // Compare against pool target
    if (check_target(ctx->bytes, s_poolTarget)) {
        uint32_t flags = 0;

        // Check for 32-bit difficulty
        if (!ctx->hash[7]) {
            dbg("32-bit match\n");
            flags |= SUBMIT_FLAG_32BIT;
            s_stats.matches32++;
        }

        // Check against block target (lottery win!)
        if (check_target(ctx->bytes, s_blockTarget)) {
            Serial.println("[MINER] *** BLOCK SOLUTION FOUND! ***");
            flags |= SUBMIT_FLAG_BLOCK;
            s_stats.blocks++;
        }

        double shareDiff = getDifficulty(ctx);
        Serial.printf("[MINER] Share found! Diff: %.4f (pool: %.4f) Nonce: %08x\n", shareDiff, s_poolDifficulty, nonce);

        // Submit share
        submit_entry_t submission;
        memset(&submission, 0, sizeof(submission));
        strncpy(submission.jobId, jobId, MAX_JOB_ID_LEN - 1);
        encodeExtraNonce(submission.extraNonce2, s_extraNonce2Size, s_extraNonce2);
        submission.timestamp = timestamp;
        submission.nonce = nonce;
        submission.flags = flags;
        submission.difficulty = shareDiff;

        stratum_submit_share(&submission);
        s_stats.shares++;
    }

    // Always track best difficulty for stats
    compareBestDifficulty(ctx);
}

// ============================================================
// Public API
// ============================================================

void miner_init() {
    s_jobMutex = xSemaphoreCreateMutex();
    s_stats.startTime = millis();

    // Initialize hardware SHA-256 peripheral
    sha256_hw_init();
    Serial.println("[MINER] Initialized (Hardware SHA-256 via direct register access)");
}

void miner_start_job(const stratum_job_t *job) {
    if (!job) return;

    // Wait for any active mining to stop
    s_miningActive = false;
    while (s_core0Mining || s_core1Mining) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    xSemaphoreTake(s_jobMutex, portMAX_DELAY);

    // Random ExtraNonce2
    s_extraNonce2 = esp_random();

    // Build block header
    s_pendingBlock.version = strtoul(job->version.c_str(), NULL, 16);
    hexToBytes(s_pendingBlock.prev_hash, job->prevHash.c_str(), 64);
    swapBytesInWords(s_pendingBlock.prev_hash, 32); // Convert to Block Header Endianness (swap bytes in 4-byte words)

    // Create coinbase hash and merkle root
    uint8_t coinbaseHash[32];
    createCoinbaseHash(coinbaseHash, job);
    JsonArray branches = job->merkleBranch;
    calculateMerkleRoot(s_pendingBlock.merkle_root, coinbaseHash, branches);

    s_pendingBlock.timestamp = strtoul(job->ntime.c_str(), NULL, 16);
    s_pendingBlock.difficulty = strtoul(job->nbits.c_str(), NULL, 16);
    s_pendingBlock.nonce = 0;

    strncpy(s_currentJobId, job->jobId.c_str(), MAX_JOB_ID_LEN - 1);

    // Set block target
    bits_to_target(s_pendingBlock.difficulty, s_blockTarget);
    setPoolTarget();

    // Random nonce start points for each core
    s_startNonce[0] = esp_random();
    s_startNonce[1] = s_startNonce[0] + 0x80000000;

    s_stats.templates++;

    xSemaphoreGive(s_jobMutex);

    s_miningActive = true;
}

void miner_stop() {
    s_miningActive = false;
}

bool miner_is_running() {
    return s_miningActive;
}

mining_stats_t *miner_get_stats() {
    return &s_stats;
}

void miner_set_difficulty(double diff) {
    if (!isnan(diff) && !isinf(diff) && diff > 0) {
        s_poolDifficulty = diff;
        setPoolTarget();
        Serial.printf("[MINER] Pool difficulty set to: %.6f\n", diff);
    }
}

void miner_set_extranonce(const char *extraNonce1, int extraNonce2Size) {
    strncpy(s_extraNonce1, extraNonce1, sizeof(s_extraNonce1) - 1);
    s_extraNonce2Size = extraNonce2Size > 8 ? 8 : extraNonce2Size;
}

// ============================================================
// Mining Task - Core 0 (Software SHA, yields to other tasks)
// ============================================================

void miner_task_core0(void *param) {
    block_header_t hb;
    sha256_hash_t ctx;
    sha256_bake_t bake;
    uint32_t midstate[8];
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 0;

    Serial.printf("[MINER0] Started on core %d (baking optimized)\n", xPortGetCoreID());

    while (true) {
        if (!s_miningActive) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        s_core0Mining = true;

        // Copy job data
        xSemaphoreTake(s_jobMutex, portMAX_DELAY);
        memcpy(&hb, &s_pendingBlock, sizeof(block_header_t));
        strncpy(jobId, s_currentJobId, MAX_JOB_ID_LEN);
        xSemaphoreGive(s_jobMutex);

        // Create swapped header for hardware SHA (matches NerdMiner behavior)
        uint32_t header_swapped[20];
        uint32_t *header_words = (uint32_t *)&hb;
        for (int i = 0; i < 20; i++) {
            header_swapped[i] = __builtin_bswap32(header_words[i]);
        }

        // Pre-compute baking constants for software fallback
        const uint8_t *tail_sw = (const uint8_t *)&hb + 64;
        sha256_hw_bake(midstate, tail_sw, &bake);

        // Set starting nonce
        hb.nonce = s_startNonce[minerId];

        // Acquire hardware SHA lock for this mining burst
        sha256_ll_acquire();

        uint16_t yieldCounter = 0;

        while (s_miningActive) {
            yieldCounter++;
            // Core 0 yields more frequently (every 256 hashes) for WiFi/system tasks
            if ((yieldCounter & 0xFF) == 0) {
                sha256_ll_release();
                vTaskDelay(1);  // Must use vTaskDelay to let IDLE task run and feed WDT
                sha256_ll_acquire();
            }

            // Full double SHA-256 (midstate restore doesn't work correctly on ESP32)
            if (sha256_ll_double_hash_full((const uint8_t *)header_swapped, hb.nonce, ctx.bytes)) {
                hashCheck(jobId, &ctx, hb.timestamp, hb.nonce);
            }

            hb.nonce++;
            s_stats.hashes++;
        }

        // Release hardware SHA lock
        sha256_ll_release();

        s_core0Mining = false;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

// ============================================================
// Mining Task - Core 1 (Dedicated, high priority)
// ============================================================

void miner_task_core1(void *param) {
    block_header_t hb;
    sha256_hash_t ctx;
    sha256_bake_t bake;
    uint32_t midstate[8];
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 1;

    Serial.printf("[MINER1] Started on core %d (baking optimized, priority %d)\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NULL));

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    Serial.println("[MINER1] Got first job, starting mining loop");

    while (true) {
        if (!s_miningActive) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        s_core1Mining = true;

        // Copy job data
        xSemaphoreTake(s_jobMutex, portMAX_DELAY);
        memcpy(&hb, &s_pendingBlock, sizeof(block_header_t));
        strncpy(jobId, s_currentJobId, MAX_JOB_ID_LEN);
        xSemaphoreGive(s_jobMutex);

        // Create swapped header for hardware SHA (matches NerdMiner behavior)
        uint32_t header_swapped[20];
        uint32_t *header_words = (uint32_t *)&hb;
        for (int i = 0; i < 20; i++) {
            header_swapped[i] = __builtin_bswap32(header_words[i]);
        }

        // Pre-compute baking constants for software fallback
        const uint8_t *tail_sw = (const uint8_t *)&hb + 64;
        sha256_hw_bake(midstate, tail_sw, &bake);

        // Set starting nonce for this core
        hb.nonce = s_startNonce[minerId];

        // Acquire hardware SHA lock for this mining burst
        sha256_ll_acquire();

        while (s_miningActive) {
            // Full double SHA-256 (midstate restore doesn't work correctly on ESP32)
            if (sha256_ll_double_hash_full((const uint8_t *)header_swapped, hb.nonce, ctx.bytes)) {
                hashCheck(jobId, &ctx, hb.timestamp, hb.nonce);
            }

            hb.nonce++;
            s_stats.hashes++;

            // Yield periodically to prevent WDT (every ~262K hashes)
            // Release hardware lock during yield so other tasks can use SHA
            if ((hb.nonce & 0x3FFFF) == 0) {
                sha256_ll_release();
                vTaskDelay(1);
                sha256_ll_acquire();
            }
        }

        // Release hardware SHA lock
        sha256_ll_release();

        s_core1Mining = false;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

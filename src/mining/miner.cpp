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
#include "sha256_pipelined.h"  // Pipelined assembly mining (Core 1)
#include "sha256_soft.h"  // Pure software SHA-256 (Core 0, no hardware)
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
// Mining Task - Core 0 (Software SHA-256, no hardware contention)
// ============================================================

void miner_task_core0(void *param) {
    block_header_t hb;
    sha256_hash_t ctx;
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 0;
    uint32_t yieldCounter = 0;
    uint8_t hash_raw[32];

    Serial.printf("[MINER0] Started on core %d (SOFTWARE SHA, priority %d)\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NULL));

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    Serial.println("[MINER0] Got first job, starting software mining loop");

    while (true) {
        if (!s_miningActive) {
            s_core0Mining = false;
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        s_core0Mining = true;

        // Copy job data under mutex
        xSemaphoreTake(s_jobMutex, portMAX_DELAY);
        memcpy(&hb, &s_pendingBlock, sizeof(block_header_t));
        strncpy(jobId, s_currentJobId, MAX_JOB_ID_LEN);
        hb.nonce = s_startNonce[minerId];
        xSemaphoreGive(s_jobMutex);

        // Create byte-swapped header for software SHA (same format as hardware)
        uint32_t header_swapped[20];
        uint32_t *header_words = (uint32_t *)&hb;
        for (int i = 0; i < 19; i++) {  // Swap all except nonce (we'll update it in loop)
            header_swapped[i] = __builtin_bswap32(header_words[i]);
        }

        while (s_miningActive) {
            // Update nonce in swapped format
            header_swapped[19] = __builtin_bswap32(hb.nonce);

            // Software double SHA-256 (no hardware, safe for Core 0)
            // Returns true if 16-bit early check passes
            if (sha256_soft_double((const uint8_t *)header_swapped, 80, hash_raw)) {
                // Early 16-bit check passed - convert to hardware-compatible format
                // Hardware format: reverse word order + byte-swap each word
                uint32_t *words = (uint32_t *)hash_raw;
                uint32_t *out = (uint32_t *)ctx.bytes;
                out[7] = __builtin_bswap32(words[0]);  // H0 -> out[7]
                out[6] = __builtin_bswap32(words[1]);  // H1 -> out[6]
                out[5] = __builtin_bswap32(words[2]);  // H2 -> out[5]
                out[4] = __builtin_bswap32(words[3]);  // H3 -> out[4]
                out[3] = __builtin_bswap32(words[4]);  // H4 -> out[3]
                out[2] = __builtin_bswap32(words[5]);  // H5 -> out[2]
                out[1] = __builtin_bswap32(words[6]);  // H6 -> out[1]
                out[0] = __builtin_bswap32(words[7]);  // H7 -> out[0]

                // Check against pool target and submit if valid
                hashCheck(jobId, &ctx, hb.timestamp, hb.nonce);
            }

            hb.nonce++;
            s_stats.hashes++;
            yieldCounter++;

            // Yield every 256 hashes to let monitor/WiFi tasks run
            if (yieldCounter >= CORE_0_YIELD_COUNT) {
                yieldCounter = 0;
                vTaskDelay(1);  // Must use vTaskDelay(1), not taskYIELD()
            }
        }

        s_core0Mining = false;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

// ============================================================
// Mining Task - Core 1 (Dedicated, high priority, pipelined ASM)
// ============================================================

#if defined(CONFIG_IDF_TARGET_ESP32)
// Pipelined assembly mining for standard ESP32 (Xtensa LX6)

// Software double SHA-256 for share verification (matches BitsyMiner pattern)
// Uses original un-swapped header - mbedtls does its own internal byte-swapping
// Output format matches ll_read_digest_if: word-wise byte swap, not byte reversal
static bool IRAM_ATTR verify_share_software(block_header_t *hdr, uint32_t nonce, sha256_hash_t *hash_out) {
    sha256_hash_t first_hash, second_hash;

    // Set the candidate nonce in the header
    hdr->nonce = nonce;

    // First SHA-256 of 80-byte header
    sha256(&first_hash, (const uint8_t *)hdr, 80);

    // Second SHA-256 of first hash (double SHA)
    sha256(&second_hash, first_hash.bytes, 32);

    // Format output to match ll_read_digest_if:
    // ESP32 hardware stores hash in reverse word order (H0 at index 7, H7 at index 0)
    // Each word is byte-swapped from big-endian (SHA output) to little-endian (CPU native)
    uint32_t *words = (uint32_t *)second_hash.bytes;
    uint32_t *out = (uint32_t *)hash_out->bytes;
    // Reverse word order AND byte-swap each word
    out[7] = __builtin_bswap32(words[0]);  // H0 -> out[7]
    out[6] = __builtin_bswap32(words[1]);  // H1 -> out[6]
    out[5] = __builtin_bswap32(words[2]);  // H2 -> out[5]
    out[4] = __builtin_bswap32(words[3]);  // H3 -> out[4]
    out[3] = __builtin_bswap32(words[4]);  // H4 -> out[3]
    out[2] = __builtin_bswap32(words[5]);  // H5 -> out[2]
    out[1] = __builtin_bswap32(words[6]);  // H6 -> out[1]
    out[0] = __builtin_bswap32(words[7]);  // H7 -> out[0]

    // Early check matches ll_read_digest_if: check upper bytes of out[7] (which is H0)
    // For valid share, H0's upper bytes (hash[31], hash[30]) should be zero
    return (hash_out->bytes[31] == 0 && hash_out->bytes[30] == 0);
}

void miner_task_core1(void *param) {
    block_header_t hb;
    sha256_hash_t ctx;
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 1;

    Serial.printf("[MINER1] Started on core %d (PIPELINED ASM, priority %d)\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NULL));

    // Initialize pipelined SHA hardware
    sha256_pipelined_init();

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    Serial.println("[MINER1] Got first job, starting pipelined mining loop");

    // SHA peripheral base address
    volatile uint32_t *sha_base = (volatile uint32_t *)0x3FF03000;  // SHA_TEXT_BASE

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

        // Create byte-swapped header for hardware SHA (pipelined mining)
        uint32_t header_swapped[20];
        uint32_t *header_words = (uint32_t *)&hb;
        for (int i = 0; i < 20; i++) {
            header_swapped[i] = __builtin_bswap32(header_words[i]);
        }

        // Set starting nonce (in swapped format for hardware)
        uint32_t nonce_swapped = __builtin_bswap32(s_startNonce[minerId]);

        // Initialize pipelined SHA
        sha256_pipelined_init();

        while (s_miningActive) {
            // Run pipelined assembly mining loop
            // Returns true when 16-bit early check passes (potential share)
            // Returns false when mining_flag becomes false
            bool candidate = sha256_pipelined_mine(
                sha_base,
                header_swapped,
                &nonce_swapped,
                &s_stats.hashes,
                &s_miningActive
            );

            if (!s_miningActive) break;

            if (candidate) {
                // Potential share found! Re-verify with HARDWARE SHA before submitting.
                // Use same function as Core 0 to ensure byte-for-byte matching.

                // The assembly incremented nonce BEFORE exiting, so use nonce-1
                uint32_t candidate_nonce_swapped = nonce_swapped - 1;
                uint32_t candidate_nonce_native = __builtin_bswap32(candidate_nonce_swapped);

                // CRITICAL: Acquire mutex before using SHA hardware (Core 0 may be using it!)
                sha256_ll_acquire();

                // Re-verify using same function as Core 0 (proven to work)
                if (sha256_ll_double_hash_full((const uint8_t *)header_swapped, candidate_nonce_native, ctx.bytes)) {
                    // Verified share - submit it
                    hashCheck(jobId, &ctx, hb.timestamp, candidate_nonce_native);
                }

                sha256_ll_release();

                // Re-init pipelined SHA hardware
                sha256_pipelined_init();
            }

            // Yield periodically to prevent WDT
            if ((nonce_swapped & 0x3FFFF) == 0) {
                vTaskDelay(1);
                sha256_pipelined_init();
            }
        }

        s_core1Mining = false;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

#else
// Fallback for ESP32-S3/C3: Use sequential HAL-based mining

void miner_task_core1(void *param) {
    block_header_t hb;
    sha256_hash_t ctx;
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 1;

    Serial.printf("[MINER1] Started on core %d (hardware SHA, priority %d)\n",
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

        // Create swapped header for hardware SHA
        uint32_t header_swapped[20];
        uint32_t *header_words = (uint32_t *)&hb;
        for (int i = 0; i < 20; i++) {
            header_swapped[i] = __builtin_bswap32(header_words[i]);
        }

        // Set starting nonce for this core
        hb.nonce = s_startNonce[minerId];

        // Acquire hardware SHA lock for this mining burst
        sha256_ll_acquire();

        while (s_miningActive) {
            // Full double SHA-256 with early 16-bit reject
            if (sha256_ll_double_hash_full((const uint8_t *)header_swapped, hb.nonce, ctx.bytes)) {
                hashCheck(jobId, &ctx, hb.timestamp, hb.nonce);
            }

            hb.nonce++;
            s_stats.hashes++;

            // Yield periodically to prevent WDT
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

#endif // CONFIG_IDF_TARGET_ESP32

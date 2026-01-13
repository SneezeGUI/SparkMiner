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

#if defined(CONFIG_IDF_TARGET_ESP32)
#include <soc/dport_reg.h>
#include <soc/hwcrypto_reg.h>
#endif

#include "miner.h"
#include "sha256_types.h"
#include "sha256_hw.h"  // Hardware SHA-256 wrapper
#include "sha256_ll.h"  // Low-level hardware SHA register access
#include "sha256_s3.h"  // S3-specific SHA (proven working with self-test)
#include "sha256_s3_dma.h"  // DMA-based SHA test
#include "sha256_asm.h"  // Pipelined assembly mining (Core 1) - ESP32
#include "sha256_pipelined_s3.h"  // Pipelined assembly mining (Core 1) - ESP32-S3
#include "miner_sha256.h"  // BitsyMiner software SHA-256 (verification + Core 0)
#include "../stratum/stratum.h"
#include "board_config.h"

// ============================================================
// Constants
// ============================================================
#define MAX_DIFFICULTY 0x1d00ffff

// ============================================================
// Globals
// ============================================================

// Mining state
static volatile bool s_miningActive = false;
static volatile bool s_core0Mining = false;
static volatile bool s_core1Mining = false;

// Hardware SHA mutex for dual-core sharing
// Core 1 holds this during pipelined mining bursts
// Core 0 can grab it during Core 1's yield periods
static SemaphoreHandle_t s_shaMutex = NULL;
static volatile bool s_core1HasSha = false;  // Fast check to avoid mutex overhead

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

// DEBUG: Per-core hash counters to verify counting (non-static for extern access)
volatile uint64_t s_core0Hashes = 0;
volatile uint64_t s_core1Hashes = 0;

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

static void calculateMerkleRoot(uint8_t *root, uint8_t *coinbaseHash, const stratum_job_t *job) {
    uint8_t merklePair[64];
    memcpy(merklePair, coinbaseHash, 32);

    for (int i = 0; i < job->merkleBranchCount; i++) {
        hexToBytes(&merklePair[32], job->merkleBranches[i], 64);
        // NerdMiner does NOT reverse merkle branches

        double_sha256_merkle(merklePair, merklePair);
        // NerdMiner does NOT reverse intermediate merkle results
    }
    memcpy(root, merklePair, 32);
}

static void createCoinbaseHash(uint8_t *hash, const stratum_job_t *job) {
    uint8_t coinbase[512];
    size_t cbLen = 0;

    // Coinbase1 (now char array)
    size_t cb1Len = strlen(job->coinBase1);
    hexToBytes(coinbase, job->coinBase1, cb1Len);
    cbLen += cb1Len / 2;

    // ExtraNonce1 (from job struct now)
    size_t en1Len = strlen(job->extraNonce1);
    hexToBytes(&coinbase[cbLen], job->extraNonce1, en1Len);
    cbLen += en1Len / 2;

    // ExtraNonce2
    char en2Hex[17];
    encodeExtraNonce(en2Hex, s_extraNonce2Size, s_extraNonce2);
    hexToBytes(&coinbase[cbLen], en2Hex, s_extraNonce2Size * 2);
    cbLen += s_extraNonce2Size;

    // Coinbase2 (now char array)
    size_t cb2Len = strlen(job->coinBase2);
    hexToBytes(&coinbase[cbLen], job->coinBase2, cb2Len);
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
    s_shaMutex = xSemaphoreCreateMutex();  // For dual-core hardware SHA sharing
    s_stats.startTime = millis();

    // Initialize hardware SHA-256 peripheral
    sha256_hw_init();

    // Run DMA-based SHA test at startup
    sha256_s3_dma_test();

    Serial.println("[MINER] Initialized (Hardware SHA-256 via direct register access)");
    Serial.println("[MINER] Dual-core hardware SHA sharing enabled");
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

    // Build block header (using char arrays now - no heap allocation)
    s_pendingBlock.version = strtoul(job->version, NULL, 16);
    hexToBytes(s_pendingBlock.prev_hash, job->prevHash, 64);
    swapBytesInWords(s_pendingBlock.prev_hash, 32); // Swap bytes within each 4-byte word (NerdMiner does this)

    // Create coinbase hash and merkle root
    uint8_t coinbaseHash[32];
    createCoinbaseHash(coinbaseHash, job);

    calculateMerkleRoot(s_pendingBlock.merkle_root, coinbaseHash, job);

    s_pendingBlock.timestamp = strtoul(job->ntime, NULL, 16);
    s_pendingBlock.difficulty = strtoul(job->nbits, NULL, 16);
    s_pendingBlock.nonce = 0;

    strncpy(s_currentJobId, job->jobId, MAX_JOB_ID_LEN - 1);

    // Debug: print header bytes
    char en2Hex[17];
    encodeExtraNonce(en2Hex, s_extraNonce2Size, s_extraNonce2);
    Serial.printf("[MINER] New job: %s, diff=%08x\n", s_currentJobId, s_pendingBlock.difficulty);
    Serial.printf("[MINER] en2=%s, ntime=%s, version=%s\n", en2Hex, job->ntime, job->version);
    Serial.printf("[MINER] Header bytes 0-7: %02x%02x%02x%02x %02x%02x%02x%02x\n",
        ((uint8_t*)&s_pendingBlock)[0], ((uint8_t*)&s_pendingBlock)[1],
        ((uint8_t*)&s_pendingBlock)[2], ((uint8_t*)&s_pendingBlock)[3],
        ((uint8_t*)&s_pendingBlock)[4], ((uint8_t*)&s_pendingBlock)[5],
        ((uint8_t*)&s_pendingBlock)[6], ((uint8_t*)&s_pendingBlock)[7]);

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

double miner_get_difficulty() {
    return s_poolDifficulty;
}

void miner_set_extranonce(const char *extraNonce1, int extraNonce2Size) {
    strncpy(s_extraNonce1, extraNonce1, sizeof(s_extraNonce1) - 1);
    s_extraNonce2Size = extraNonce2Size > 8 ? 8 : extraNonce2Size;
}

// ============================================================
// Mining Task - Core 0 (Hybrid: Hardware SHA when available, Software fallback)
// ============================================================

void miner_task_core0(void *param) {
    block_header_t hb;
    sha256_hash_t ctx;
    sha256_hash_t sw_midstate;  // Software midstate for fallback
    uint32_t hw_midstate[8];    // Hardware midstate for opportunistic HW SHA
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 0;
    uint32_t yieldCounter = 0;
    uint32_t hwHashes = 0;  // Track hardware SHA usage
    uint32_t swHashes = 0;  // Track software SHA usage

    Serial.printf("[MINER0] Started on core %d (HYBRID HW/SW SHA, priority %d)\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NULL));

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    Serial.println("[MINER0] Got first job, starting hybrid mining (HW when Core 1 yields)");

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

        // Always compute SOFTWARE midstate (for fallback and verification)
        miner_sha256_midstate(&sw_midstate, &hb);

        // Prepare byte-swapped header for hardware SHA
        uint32_t header_swapped[20];
        uint32_t *header_words = (uint32_t *)&hb;
        for (int i = 0; i < 20; i++) {
            header_swapped[i] = __builtin_bswap32(header_words[i]);
        }

        // Try to compute hardware midstate if we can grab the mutex
        bool hasHwMidstate = false;
        if (!s_core1HasSha && xSemaphoreTake(s_shaMutex, 0) == pdTRUE) {
            sha256_ll_acquire();
            sha256_ll_midstate(hw_midstate, (const uint8_t *)header_swapped);
            sha256_ll_release();
            xSemaphoreGive(s_shaMutex);
            hasHwMidstate = true;
        }

        while (s_miningActive) {
            // Pure software SHA - no hardware contention with Core 1
            if (miner_sha256_header(&sw_midstate, &ctx, &hb)) {
                hashCheck(jobId, &ctx, hb.timestamp, hb.nonce);
            }
            hb.nonce++;
            s_stats.hashes++;
            s_core0Hashes++;  // DEBUG: Track Core 0 contribution
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
    block_header_t hbVerify;  // BitsyMiner pattern: keep UNSWAPPED copy for verification
    sha256_hash_t ctx;
    sha256_hash_t midstate;
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 1;

    Serial.printf("[MINER1] Started on core %d (PIPELINED ASM v3, priority %d)\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NULL));

    // Enable SHA peripheral clock and clear reset
    DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA);
    DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    Serial.println("[MINER1] Got first job, starting pipelined mining v3");

    // SHA peripheral base address
    volatile uint32_t *sha_base = (volatile uint32_t *)0x3FF03000;  // SHA_TEXT_BASE

    while (true) {
        if (!s_miningActive) {
            s_core1HasSha = false;  // Release SHA indicator when not mining
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        s_core1Mining = true;

        // Copy job data
        xSemaphoreTake(s_jobMutex, portMAX_DELAY);
        memcpy(&hb, &s_pendingBlock, sizeof(block_header_t));
        memcpy(&hbVerify, &s_pendingBlock, sizeof(block_header_t));  // Keep UNSWAPPED for verification!
        strncpy(jobId, s_currentJobId, MAX_JOB_ID_LEN);
        xSemaphoreGive(s_jobMutex);

        // BitsyMiner pattern: Compute SOFTWARE midstate on UNSWAPPED header (for verification)
        miner_sha256_midstate(&midstate, &hbVerify);

        // Create byte-swapped header for hardware SHA (pipelined mining)
        uint32_t header_swapped[20];
        uint32_t *header_words = (uint32_t *)&hb;
        for (int i = 0; i < 20; i++) {
            header_swapped[i] = __builtin_bswap32(header_words[i]);
        }

        // Set starting nonce (in swapped format for hardware)
        uint32_t nonce_swapped = __builtin_bswap32(s_startNonce[minerId]);

        // Acquire SHA mutex and set fast-check flag
        xSemaphoreTake(s_shaMutex, portMAX_DELAY);
        s_core1HasSha = true;

        // Re-initialize SHA hardware before loop
        DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA);
        DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);

        while (s_miningActive) {
            // Run pipelined assembly mining loop v3 (working version)
            bool candidate = sha256_pipelined_mine_v3(
                sha_base,
                header_swapped,
                &nonce_swapped,
                &s_stats.hashes,
                &s_miningActive
            );

            if (!s_miningActive) break;

            if (candidate) {
                // BitsyMiner pattern: The assembly incremented nonce BEFORE exiting, so use nonce-1
                uint32_t candidate_nonce_swapped = nonce_swapped - 1;
                uint32_t candidate_nonce_native = __builtin_bswap32(candidate_nonce_swapped);

                // BitsyMiner CRITICAL pattern: Verify with SOFTWARE SHA on UNSWAPPED header
                // This is what the pool computes, so hashes MUST match!
                hbVerify.nonce = candidate_nonce_native;
                if (miner_sha256_header(&midstate, &ctx, &hbVerify)) {
                    // SOFTWARE verified share - submit it
                    hashCheck(jobId, &ctx, hbVerify.timestamp, candidate_nonce_native);
                }

                // Re-init pipelined SHA hardware
                DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA);
                DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);
            }

            // Yield periodically to prevent WDT
            // The ASM function returns every ~65k hashes (on partial match),
            // so we yield every 16 iterations (approx 1M hashes)
            static uint32_t loop_iter = 0;
            if (++loop_iter >= 16) {
                loop_iter = 0;
                vTaskDelay(1);
                // Re-init after yield
                DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA);
                DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);
            }
        }

        // Release SHA mutex when done
        s_core1HasSha = false;
        xSemaphoreGive(s_shaMutex);

        s_core1Mining = false;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include <sha/sha_dma.h>  // For esp_sha_acquire/release_hardware
// ESP32-S3: Optimized pipelined assembly mining with MIDSTATE CACHING (v2)
// Key optimizations:
// 1. Hardware midstate computed ONCE per job (not per nonce!)
// 2. Block 2 template prepared once, only nonce changes
// 3. Double-hash padding leverages zeros from block 2

void miner_task_core1(void *param) {
    block_header_t hb;
    block_header_t hbVerify;  // BitsyMiner pattern: keep UNSWAPPED copy for verification
    sha256_hash_t ctx;
    sha256_hash_t sw_midstate;  // SOFTWARE midstate for verification
    uint32_t hw_midstate[8];    // HARDWARE midstate for mining (NEW!)
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 1;

    Serial.printf("[MINER1] Started on core %d (S3 Optimized ASM v2 + Midstate Cache, priority %d)\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NULL));

    // Initialize S3 pipelined SHA hardware
    sha256_pipelined_s3_init();

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    Serial.println("[MINER1] Got first job, starting S3 optimized assembly mining (v2 with midstate)");

    while (true) {
        if (!s_miningActive) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        s_core1Mining = true;

        // Copy job data
        xSemaphoreTake(s_jobMutex, portMAX_DELAY);
        memcpy(&hb, &s_pendingBlock, sizeof(block_header_t));
        memcpy(&hbVerify, &s_pendingBlock, sizeof(block_header_t));  // Keep UNSWAPPED for verification!
        strncpy(jobId, s_currentJobId, MAX_JOB_ID_LEN);
        xSemaphoreGive(s_jobMutex);

        // BitsyMiner pattern: Compute SOFTWARE midstate on UNSWAPPED header (for verification)
        miner_sha256_midstate(&sw_midstate, &hbVerify);

        // ========================================
        // BYTESWAP32 all 20 words of header for hardware SHA
        // ========================================
        uint32_t header_swapped[20];
        uint32_t *header_words = (uint32_t *)&hb;
        for (int i = 0; i < 20; i++) {
            header_swapped[i] = __builtin_bswap32(header_words[i]);
        }

        // ========================================
        // OPTIMIZATION v3: Compute hardware midstate ONCE per job!
        // Also initialize persistent zeros in SHA_TEXT
        // ========================================
        esp_sha_acquire_hardware();
        sha256_s3_compute_midstate(header_swapped, hw_midstate);
        sha256_s3_init_zeros();  // Set persistent zeros for block 2 padding

        // Prepare block 2 template (words 16-18: last 4 bytes merkle, timestamp, nbits)
        // Word 19 (nonce) will be set per iteration
        uint32_t block2_template[3];
        block2_template[0] = header_swapped[16];  // merkle_root tail (swapped)
        block2_template[1] = header_swapped[17];  // timestamp (swapped)
        block2_template[2] = header_swapped[18];  // nbits (swapped)

        // Nonce in big-endian format for hardware SHA
        uint32_t nonce_swapped = __builtin_bswap32(s_startNonce[minerId]);

        #ifdef DEBUG_MINING
        Serial.printf("[S3-V3] Midstate cached, zeros persistent, starting batched-copy loop\n");
        static uint32_t s3_call_count = 0;
        uint64_t hashes_before = s_stats.hashes;
        #endif

        while (s_miningActive) {
            // Run ULTRA-OPTIMIZED pipelined assembly mining loop (v3)
            // - Midstate restore (same as v2)
            // - Batched register loads for SHA_H copy (pipeline memory)
            // - Persistent zeros (skip writing 10 zeros per iteration)
            #ifdef DEBUG_MINING
            s3_call_count++;
            #endif

            bool candidate = sha256_pipelined_mine_s3_v3(
                hw_midstate,
                block2_template,
                &nonce_swapped,
                &s_stats.hashes,
                &s_miningActive
            );

            #ifdef DEBUG_MINING
            if ((s3_call_count & 0x7FFFF) == 0) {  // Every ~512K calls
                uint64_t hashes_now = s_stats.hashes;
                Serial.printf("[S3-V3] calls=%u, hashes=%llu\n", s3_call_count, hashes_now);
            }
            #endif

            if (!s_miningActive) break;

            if (candidate) {
                // BitsyMiner pattern: The assembly incremented nonce BEFORE exiting
                uint32_t candidate_nonce_swapped = nonce_swapped - 1;
                uint32_t candidate_nonce_native = __builtin_bswap32(candidate_nonce_swapped);

                // BitsyMiner CRITICAL: Verify with SOFTWARE SHA on UNSWAPPED header
                hbVerify.nonce = candidate_nonce_native;
                if (miner_sha256_header(&sw_midstate, &ctx, &hbVerify)) {
                    hashCheck(jobId, &ctx, hbVerify.timestamp, candidate_nonce_native);
                }
            }

            // Yield periodically to prevent WDT
            // The ASM function returns every ~65k hashes (on partial match),
            // so we yield every 16 iterations (approx 1M hashes)
            static uint32_t loop_iter = 0;
            if (++loop_iter >= 16) {
                loop_iter = 0;
                esp_sha_release_hardware();
                vTaskDelay(1);
                esp_sha_acquire_hardware();
            }
        }

        esp_sha_release_hardware();
        s_core1Mining = false;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

#else
// Fallback for ESP32-C3/S2: Use sequential HAL-based mining with Midstate Optimization

void miner_task_core1(void *param) {
    block_header_t hb;
    sha256_hash_t ctx;
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 1;

    Serial.printf("[MINER1] Started on core %d (Hardware SHA Midstate, priority %d)\n",
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

        // Prepare midstate variables
        uint32_t midstate[8];
        uint8_t *header_bytes = (uint8_t *)header_swapped;

        // Acquire hardware SHA lock for this mining burst
        sha256_ll_acquire();

        // Compute midstate once for the block
        sha256_ll_midstate(midstate, header_bytes);

        while (s_miningActive) {
            // Optimized midstate mining
            // Uses pre-computed midstate and only hashes the tail (last 16 bytes + padding)
            // header_bytes[64] is the start of the 2nd chunk (tail)
            if (sha256_ll_double_hash(midstate, &header_bytes[64], hb.nonce, ctx.bytes)) {
                hashCheck(jobId, &ctx, hb.timestamp, hb.nonce);
            }

            hb.nonce++;
            s_stats.hashes++;

            // Yield periodically to prevent WDT (every ~1M nonces)
            if ((hb.nonce & 0xFFFFF) == 0) {
                sha256_ll_release();
                vTaskDelay(1);
                sha256_ll_acquire();
                // Recompute midstate after yield just in case hardware state was lost (unlikely but safe)
                sha256_ll_midstate(midstate, header_bytes);
            }
        }

        // Release hardware SHA lock
        sha256_ll_release();

        s_core1Mining = false;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

#endif // CONFIG_IDF_TARGET_ESP32

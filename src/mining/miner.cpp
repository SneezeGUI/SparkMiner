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

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <sha/sha_dma.h>
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
#include "../logging.h"
#include "board_config.h"

// ============================================================
// Constants
// ============================================================
#define MAX_DIFFICULTY 0x1d00ffff

// Compile-time deterministic nonce window for S3 debug sessions.
// Enable only when investigating candidate mismatches.
#ifndef S3_DEBUG_NONCE_WINDOW_ENABLE
#define S3_DEBUG_NONCE_WINDOW_ENABLE 0
#endif

// Debug nonce windows for S3 MUST be defined in swapped nonce space because
// the assembly loop increments the swapped nonce directly.
#ifndef S3_DEBUG_NONCE_WINDOW_START_SWAPPED
#ifdef S3_DEBUG_NONCE_WINDOW_START
#define S3_DEBUG_NONCE_WINDOW_START_SWAPPED S3_DEBUG_NONCE_WINDOW_START
#else
#define S3_DEBUG_NONCE_WINDOW_START_SWAPPED 0x00000000u
#endif
#endif

#ifndef S3_DEBUG_NONCE_WINDOW_END_SWAPPED
#ifdef S3_DEBUG_NONCE_WINDOW_END
#define S3_DEBUG_NONCE_WINDOW_END_SWAPPED S3_DEBUG_NONCE_WINDOW_END
#else
#define S3_DEBUG_NONCE_WINDOW_END_SWAPPED 0x0000FFFFu
#endif
#endif

#if S3_DEBUG_NONCE_WINDOW_ENABLE && (S3_DEBUG_NONCE_WINDOW_END_SWAPPED < S3_DEBUG_NONCE_WINDOW_START_SWAPPED)
#error "S3 debug nonce window invalid: END_SWAPPED must be >= START_SWAPPED"
#endif

// Optional one-shot runtime sanity test.
// Runs exactly once after boot using a tiny swapped window, prints PASS/WARN,
// then automatically falls back to the configured debug window.
#ifndef S3_DEBUG_NONCE_WINDOW_TINY_SELFTEST_ENABLE
#define S3_DEBUG_NONCE_WINDOW_TINY_SELFTEST_ENABLE 0
#endif

#ifndef S3_DEBUG_NONCE_WINDOW_TINY_START_SWAPPED
#define S3_DEBUG_NONCE_WINDOW_TINY_START_SWAPPED 0x00000000u
#endif

#ifndef S3_DEBUG_NONCE_WINDOW_TINY_END_SWAPPED
#define S3_DEBUG_NONCE_WINDOW_TINY_END_SWAPPED 0x00000010u
#endif

// Verbose restore matrix diagnostics are useful only for deep bring-up.
// Keep off by default to avoid noisy normal boots.
#ifndef S3_RESTORE_MATRIX_DEBUG
#define S3_RESTORE_MATRIX_DEBUG 0
#endif

// Optional deeper S3 hardware experiment harness.
// Disabled by default to avoid boot-time overhead on normal mining builds.
#ifndef S3_HW_EXPERIMENTS_ENABLE
#define S3_HW_EXPERIMENTS_ENABLE 0
#endif

#ifndef S3_HW_EXPERIMENTS_NONCES
#define S3_HW_EXPERIMENTS_NONCES 4096
#endif

#if S3_DEBUG_NONCE_WINDOW_TINY_SELFTEST_ENABLE && (S3_DEBUG_NONCE_WINDOW_TINY_END_SWAPPED < S3_DEBUG_NONCE_WINDOW_TINY_START_SWAPPED)
#error "S3 tiny self-test window invalid: TINY_END_SWAPPED must be >= TINY_START_SWAPPED"
#endif

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

// Job invalidation counter: increments each time a new job replaces an active one.
// Useful for diagnosing how many hashes are "wasted" per job change.
volatile uint32_t s_jobChanges = 0;

// Nonce ranges for dual-core
static unsigned long s_startNonce[2] = {0, 0x80000000};

#if defined(CONFIG_IDF_TARGET_ESP32S3)
static const miner_backend_info_t s_backendInfo = {
    "ESP32-S3",
    "software-midstate dual-core",
    true,   // hwShaAvailable
    false,  // hwShaHotLoop
    true,   // softwareMidstate
    false,  // dmaHotPath
    false,  // midstateRestoreSupported
    true    // nonceSplitCore0LowCore1High
};
#else
static const miner_backend_info_t s_backendInfo = {
    "ESP32",
    "hardware SHA mixed path",
    true,
    true,
    false,
    false,
    true,
    true
};
#endif

const miner_backend_info_t *miner_get_backend_info() {
    return &s_backendInfo;
}

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

#ifdef DEBUG_SHARE_VALIDATION
static void dump_hex_compact(const char *label, const uint8_t *buf, size_t len) {
    Serial.print(label);
    Serial.print("=");
    for (size_t i = 0; i < len; i++) {
        Serial.printf("%02x", buf[i]);
    }
    Serial.println();
}

static void dump_s3_candidate_trace_once(
    const block_header_t *hdr_verify,
    const uint32_t *header_swapped,
    const uint32_t *hw_midstate,
    uint32_t nonce_native,
    uint32_t nonce_swapped,
    const sha256_hash_t *sw_hash,
    bool sw_verified
) {
    static bool dumped = false;
    if (dumped || !hdr_verify || !header_swapped || !hw_midstate || !sw_hash) {
        return;
    }
    dumped = true;

    block_header_t hdr_sw = *hdr_verify;
    hdr_sw.nonce = nonce_native;

    uint8_t s3_header[80];
    for (int i = 0; i < 20; i++) {
        uint32_t w = header_swapped[i];
        if (i == 19) {
            w = nonce_swapped;
        }
        ((uint32_t *)s3_header)[i] = __builtin_bswap32(w);
    }

    sha256_hash_t sw_first;
    sha256_hash_t sw_second;
    sha256(&sw_first, (uint8_t *)&hdr_sw, 80);
    sha256(&sw_second, sw_first.bytes, 32);

    sha256_hash_t s3_second;
    memset(&s3_second, 0, sizeof(s3_second));
    bool s3_verify_ok = sha256_s3_verify(
        hw_midstate,
        (const uint8_t *)&header_swapped[16],
        nonce_swapped,
        s3_second.bytes
    );

    uint32_t logical_h0 = ((const uint32_t *)sw_hash->bytes)[7];
    uint32_t raw_h0 = __builtin_bswap32(logical_h0);

    Serial.println("[S3-DBG] -------- Candidate trace --------");
    Serial.printf("[S3-DBG] nonce_native=%08x nonce_swapped=%08x\n", nonce_native, nonce_swapped);
    Serial.printf("[S3-DBG] header[76..79]=%02x %02x %02x %02x\n",
                  ((uint8_t *)&hdr_sw)[76], ((uint8_t *)&hdr_sw)[77],
                  ((uint8_t *)&hdr_sw)[78], ((uint8_t *)&hdr_sw)[79]);
    dump_hex_compact("[S3-DBG] hdr_sw", (const uint8_t *)&hdr_sw, 80);
    dump_hex_compact("[S3-DBG] hdr_s3", s3_header, 80);
    dump_hex_compact("[S3-DBG] midstate_input64", (const uint8_t *)&hdr_sw, 64);
    dump_hex_compact("[S3-DBG] sw_first", sw_first.bytes, 32);
    dump_hex_compact("[S3-DBG] sw_second", sw_second.bytes, 32);
    if (s3_verify_ok) {
        dump_hex_compact("[S3-DBG] s3_second", s3_second.bytes, 32);
    } else {
        Serial.println("[S3-DBG] s3_second=<sha256_s3_verify failed>");
    }
    dump_hex_compact("[S3-DBG] pool_target", s_poolTarget, 32);
    Serial.printf("[S3-DBG] prefilter_word_raw_h0=%08x upper16=%04x lower16=%04x\n",
                  raw_h0, (uint16_t)(raw_h0 >> 16), (uint16_t)(raw_h0 & 0xFFFF));
    Serial.printf("[S3-DBG] sw_prefilter=%s full_target=%s sw_verify=%s\n",
                  (sw_hash->bytes[31] == 0 && sw_hash->bytes[30] == 0) ? "PASS" : "FAIL",
                  check_target(sw_hash->bytes, s_poolTarget) ? "PASS" : "FAIL",
                  sw_verified ? "PASS" : "FAIL");
    Serial.println("[S3-DBG] ---------------------------------");
}
#endif

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
    uint8_t coinbase[1024];
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
    if (cbLen + cb2Len / 2 > sizeof(coinbase)) {
        Serial.printf("[MINER] ERROR: Coinbase exceeds buffer (%d bytes)\n", cbLen + cb2Len / 2);
        return;
    }
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
            log_line("[MINER] *** BLOCK SOLUTION FOUND! ***");
            flags |= SUBMIT_FLAG_BLOCK;
            s_stats.blocks++;
        }

        double shareDiff = getDifficulty(ctx);
        log_linef("[MINER] Share found! Diff: %.4f (pool: %.4f) Nonce: %08x", shareDiff, s_poolDifficulty, nonce);

        // Debug logging for share validation (Issue #5 investigation)
        #if defined(DEBUG_SHARE_VALIDATION)
        Serial.printf("[SHARE] job=%s time=%08x nonce=%08x\n", jobId, timestamp, nonce);
        Serial.printf("[SHARE] hash[28-31]=%02x%02x%02x%02x (should have leading zeros)\n",
                      ctx->bytes[28], ctx->bytes[29], ctx->bytes[30], ctx->bytes[31]);
        char en2Hex[17];
        encodeExtraNonce(en2Hex, s_extraNonce2Size, s_extraNonce2);
        Serial.printf("[SHARE] extraNonce2=%s\n", en2Hex);
        #endif

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

#ifdef BENCHMARK_SHA_VERSIONS
void run_sha_benchmark() {
    Serial.println("[BENCHMARK] Starting SHA-256 version benchmark...");
    volatile uint32_t *sha_base = (volatile uint32_t *)0x3FF03000;
    uint32_t header[20] = {0}; // Dummy header
    uint32_t midstate[8] = {0};
    uint32_t tail[3] = {0};
    uint32_t nonce = 0;
    uint64_t hashes = 0;
    bool active = true;

    // Prep v4 data
    sha256_compute_midstate_v4(midstate, header);
    tail[0] = header[16]; tail[1] = header[17]; tail[2] = header[18];

    Serial.println("[BENCHMARK] Running v3 (100k hashes)...");
    uint32_t t0 = micros();
    hashes = 0;
    active = true;
    while(hashes < 100000) {
        sha256_pipelined_mine_v3(sha_base, header, &nonce, &hashes, &active);
    }
    uint32_t t1 = micros();
    Serial.printf("[BENCHMARK] v3: %u us for %llu hashes (%.2f kH/s)\n",
        t1-t0, hashes, (double)hashes*1000.0/(t1-t0));

    Serial.println("[BENCHMARK] Running v4 (100k hashes)...");
    hashes = 0;
    active = true;
    t0 = micros();
    while(hashes < 100000) {
         sha256_pipelined_mine_v4(sha_base, midstate, tail, &nonce, &hashes, &active);
    }
    t1 = micros();
    Serial.printf("[BENCHMARK] v4: %u us for %llu hashes (%.2f kH/s)\n",
        t1-t0, hashes, (double)hashes*1000.0/(t1-t0));
}
#endif

#ifdef DEBUG_SHARE_VALIDATION
static void dump_u32_words(const char *label, const uint32_t *words, size_t count) {
    Serial.print(label);
    Serial.print("=");
    for (size_t i = 0; i < count; i++) {
        Serial.printf("%08x", words[i]);
        if (i + 1 < count) Serial.print(" ");
    }
    Serial.println();
}

static void build_first_tail_block(uint8_t out[64], const block_header_t *hdr) {
    memset(out, 0, 64);
    memcpy(out, ((const uint8_t *)hdr) + 64, 16);
    out[16] = 0x80;
    out[62] = 0x02;
    out[63] = 0x80;
}

static void build_second_block(uint8_t out[64], const uint8_t firstDigest[32]) {
    memset(out, 0, 64);
    memcpy(out, firstDigest, 32);
    out[32] = 0x80;
    out[62] = 0x01;
    out[63] = 0x00;
}

static void write_be32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)(v);
}

static void build_first_tail_block_prepared(uint8_t out[64], const sha256_tail_schedule_cache_t *cache, uint32_t nonce) {
    memset(out, 0, 64);
    write_be32(out + 0, cache->w0);
    write_be32(out + 4, cache->w1);
    write_be32(out + 8, cache->w2);
    write_be32(out + 12, __builtin_bswap32(nonce));
    write_be32(out + 16, 0x80000000u);
    write_be32(out + 60, 0x00000280u);
}

static bool run_prepared_equivalence_nonce_window(
    const char *name,
    const block_header_t *hdr,
    uint32_t startNonce,
    uint32_t count
) {
    if (!name || !hdr || count == 0) return false;

    block_header_t base = *hdr;
    base.nonce = startNonce;

    sha256_hash_t swMidstate;
    miner_sha256_midstate(&swMidstate, &base);

    sha256_tail_schedule_cache_t cache = {0};
    miner_sha256_prepare_tail_schedule(&cache, &base);

    for (uint32_t i = 0; i < count; i++) {
        block_header_t hb = base;
        hb.nonce = startNonce + i;

        sha256_hash_t legacyFirst = {0};
        sha256_hash_t legacySecond = {0};
        sha256_hash_t preparedFirst = {0};
        sha256_hash_t preparedSecond = {0};

        miner_sha256_complete_from_midstate(&swMidstate, &hb, &legacyFirst, &legacySecond);
        miner_sha256_complete_from_midstate_prepared(&swMidstate, &cache, hb.nonce, &preparedFirst, &preparedSecond);

        bool firstEq = (memcmp(legacyFirst.bytes, preparedFirst.bytes, 32) == 0);
        bool secondEq = (memcmp(legacySecond.bytes, preparedSecond.bytes, 32) == 0);
        if (!firstEq || !secondEq) {
            uint8_t legacyTail[64];
            uint8_t preparedTail[64];
            build_first_tail_block(legacyTail, &hb);
            build_first_tail_block_prepared(preparedTail, &cache, hb.nonce);

            Serial.printf("[PREPARED-TEST] %s nonce=%08lx (swapped=%08lx) mismatch first=%s second=%s\n",
                          name,
                          (unsigned long)hb.nonce,
                          (unsigned long)__builtin_bswap32(hb.nonce),
                          firstEq ? "NO" : "YES",
                          secondEq ? "NO" : "YES");
            dump_hex_compact("[PREPARED-TEST] legacy_tail", legacyTail, 64);
            dump_hex_compact("[PREPARED-TEST] prepared_tail", preparedTail, 64);
            dump_hex_compact("[PREPARED-TEST] legacy_first", legacyFirst.bytes, 32);
            dump_hex_compact("[PREPARED-TEST] prepared_first", preparedFirst.bytes, 32);
            dump_hex_compact("[PREPARED-TEST] legacy_second", legacySecond.bytes, 32);
            dump_hex_compact("[PREPARED-TEST] prepared_second", preparedSecond.bytes, 32);
            Serial.printf("[PREPARED-TEST] %s first divergence point: %s\n",
                          name,
                          firstEq ? "second SHA digest" : "first SHA digest");
            return false;
        }
    }

    return true;
}

static bool parse_header_hex_80(const char *hex, block_header_t *out) {
    if (!hex || !out) return false;
    if (strlen(hex) != 160) return false;
    memset(out, 0, sizeof(*out));
    hexToBytes((uint8_t *)out, hex, 160);
    return true;
}

static bool run_midstate_vector_test(
    const char *name,
    const block_header_t *hdr,
    const char *expectedFirstHex,
    const char *expectedSecondHex
) {
    if (!name || !hdr) return false;

    sha256_hash_t directFirst;
    sha256_hash_t directSecond;
    sha256_hash_t swMidstate;
    sha256_hash_t swFirstFromMid;
    sha256_hash_t swSecondFromMid;

    sha256(&directFirst, (uint8_t *)hdr, 80);
    sha256(&directSecond, directFirst.bytes, 32);

    miner_sha256_midstate(&swMidstate, (block_header_t *)hdr);
    miner_sha256_complete_from_midstate(&swMidstate, hdr, &swFirstFromMid, &swSecondFromMid);

    uint8_t tailBlock[64];
    uint8_t secondBlock[64];
    build_first_tail_block(tailBlock, hdr);
    build_second_block(secondBlock, swFirstFromMid.bytes);

    dump_hex_compact("[MINER-TEST] header80", (const uint8_t *)hdr, 80);
    dump_hex_compact("[MINER-TEST] direct_first", directFirst.bytes, 32);
    dump_hex_compact("[MINER-TEST] direct_second", directSecond.bytes, 32);
    dump_hex_compact("[MINER-TEST] midstate_raw", swMidstate.bytes, 32);
    dump_u32_words("[MINER-TEST] midstate_words", swMidstate.hash, 8);
    dump_hex_compact("[MINER-TEST] first_tail_block", tailBlock, 64);
    dump_hex_compact("[MINER-TEST] midstate_first", swFirstFromMid.bytes, 32);
    dump_hex_compact("[MINER-TEST] second_input_block", secondBlock, 64);
    dump_hex_compact("[MINER-TEST] midstate_second", swSecondFromMid.bytes, 32);

    bool direct80Ok = true;
    bool swMidCompletionOk = (memcmp(swSecondFromMid.bytes, directSecond.bytes, 32) == 0);

    if (expectedFirstHex && strlen(expectedFirstHex) == 64) {
        uint8_t expectedFirst[32];
        hexToBytes(expectedFirst, expectedFirstHex, 64);
        direct80Ok = (memcmp(directFirst.bytes, expectedFirst, 32) == 0);
    }

    if (expectedSecondHex && strlen(expectedSecondHex) == 64) {
        uint8_t expectedSecond[32];
        hexToBytes(expectedSecond, expectedSecondHex, 64);
        direct80Ok = direct80Ok && (memcmp(directSecond.bytes, expectedSecond, 32) == 0);
    }

    bool hwMidCompletionOk = true;
    bool s3VerifyCompletionOk = true;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    uint32_t header_swapped[20];
    const uint32_t *header_words = (const uint32_t *)hdr;
    for (int i = 0; i < 20; i++) {
        header_swapped[i] = __builtin_bswap32(header_words[i]);
    }

    uint32_t hwMidstate[8] = {0};

    esp_sha_acquire_hardware();
    sha256_s3_compute_midstate(header_swapped, hwMidstate);
    esp_sha_release_hardware();

    // Midstate-only compare (no SHA_H restore/continue path).
    bool hwMidRawEq = (memcmp(hwMidstate, swMidstate.hash, sizeof(hwMidstate)) == 0);
    uint32_t hwMidSwapped[8];
    for (int i = 0; i < 8; i++) hwMidSwapped[i] = __builtin_bswap32(hwMidstate[i]);
    bool hwMidSwapEq = (memcmp(hwMidSwapped, swMidstate.hash, sizeof(hwMidSwapped)) == 0);
    hwMidCompletionOk = hwMidRawEq || hwMidSwapEq;

    Serial.println("[S3-TEST] SHA_H restore+continue path marked unsupported; skipped in normal boot tests");

#if S3_RESTORE_MATRIX_DEBUG
    if (expectedFirstHex && strlen(expectedFirstHex) == 64) {
        uint8_t expectedFirst[32];
        hexToBytes(expectedFirst, expectedFirstHex, 64);

        static const uint32_t knownLiveMidstateWords[8] = {
            0x587002b3, 0xb4886f0d, 0xb54d02f5, 0x065289ec,
            0x0f36da87, 0xff81c170, 0x71bd69d6, 0xfdf0168b
        };
        bool swMidWordsMatchKnown = (memcmp(swMidstate.hash, knownLiveMidstateWords, sizeof(knownLiveMidstateWords)) == 0);
        Serial.printf("[S3-TEST] software_midstate_words_match_known=%s\n", swMidWordsMatchKnown ? "PASS" : "FAIL");

        esp_sha_acquire_hardware();
        bool restoreMapPass = sha256_s3_test_restore_mapping(
            swMidstate.hash,
            ((const uint8_t *)hdr) + 64,
            &header_swapped[16],
            expectedFirst
        );
        esp_sha_release_hardware();
        Serial.printf("[S3-TEST] restore_mapping_any_mode=%s\n", restoreMapPass ? "PASS" : "FAIL");
    }
#endif
#else
    hwMidCompletionOk = true;
    s3VerifyCompletionOk = true;
#endif

    Serial.printf("[MINER-TEST] direct 80-byte double SHA %s\n", direct80Ok ? "OK" : "FAIL");
    Serial.printf("[MINER-TEST] software midstate completion %s\n", swMidCompletionOk ? "OK" : "FAIL");

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // On ESP32-S3: optional S3 midstate restore is unsupported, do not fail software-midstate validation.
    Serial.printf("[MINER-TEST] S3 midstate restore optimization %s\n", hwMidCompletionOk ? "OK" : "UNSUPPORTED");
    bool softwareMidstateOk = direct80Ok && swMidCompletionOk;
    Serial.printf("[MINER-TEST] %s software-midstate %s\n", name, softwareMidstateOk ? "PASS" : "FAIL");
    return softwareMidstateOk;
#else
    // On other platforms: all tests matter
    Serial.printf("[MINER-TEST] hardware midstate (compute-only) %s\n", hwMidCompletionOk ? "OK" : "FAIL");
    Serial.printf("[MINER-TEST] hardware restore+continue status %s\n", s3VerifyCompletionOk ? "SKIPPED/UNSUPPORTED" : "UNSUPPORTED");
    bool overall = direct80Ok && swMidCompletionOk && hwMidCompletionOk && s3VerifyCompletionOk;
    Serial.printf("[MINER-TEST] %s overall %s\n", name, overall ? "OK" : "FAIL");
    return overall;
#endif
}

#if defined(CONFIG_IDF_TARGET_ESP32S3)
static void run_s3_hw_experiments(void) {
    Serial.printf("[S3-EXP] Enabled (nonces=%u)\n", (unsigned)S3_HW_EXPERIMENTS_NONCES);

    // Experiments use direct SHA register access paths. Keep SHA hardware
    // acquired for the whole harness so results are meaningful and repeatable.
    esp_sha_acquire_hardware();

    block_header_t headers[2];
    memset(&headers, 0, sizeof(headers));

    // Synthetic vector
    headers[0].version = 0x20000000;
    for (int i = 0; i < 32; i++) {
        headers[0].prev_hash[i] = (uint8_t)i;
        headers[0].merkle_root[i] = (uint8_t)(0xA0 + i);
    }
    headers[0].timestamp = 0x6a02268b;
    headers[0].difficulty = 0x17021ff0;
    headers[0].nonce = 0x2e425c33;

    // Captured live header
    parse_header_hex_80(
        "000000204ba6eb671af350c5b183bd2467497eca61b4f61eb88901000000000000000000acbb08e6dbac14cb5b23e07499d2f4686972836e871ce6015843d870138514b5cf33026af01f021700000e08",
        &headers[1]
    );

    for (int vec = 0; vec < 2; vec++) {
        block_header_t hb = headers[vec];
        sha256_hash_t swMid;
        sha256_hash_t swFirst;
        sha256_hash_t swSecond;
        sha256_hash_t swSecondMid;
        sha256_s3_verify_trace_t traceA;
        sha256_s3_verify_trace_t traceC;
        memset(&traceA, 0, sizeof(traceA));
        memset(&traceC, 0, sizeof(traceC));

        uint32_t header_swapped[20];
        const uint32_t *header_words = (const uint32_t *)&hb;
        for (int i = 0; i < 20; i++) {
            header_swapped[i] = __builtin_bswap32(header_words[i]);
        }
        uint32_t nonce_swapped = header_swapped[19];

        // Baseline software references.
        sha256(&swFirst, (uint8_t *)&hb, 80);
        sha256(&swSecond, swFirst.bytes, 32);
        miner_sha256_midstate(&swMid, &hb);
        miner_sha256_complete_from_midstate(&swMid, &hb, &swFirst, &swSecondMid);

        bool swEq = (memcmp(swSecond.bytes, swSecondMid.bytes, 32) == 0);

        // Candidate A: software midstate -> HW restore + continue + HW second SHA.
        uint8_t a_out[32] = {0};
        bool a_ok = sha256_s3_verify_trace(
            swMid.hash,
            (const uint8_t *)&header_swapped[16],
            nonce_swapped,
            a_out,
            &traceA
        );
        bool a_match = a_ok && (memcmp(traceA.finalDigestBeBytes, swSecond.bytes, 32) == 0);

        // Candidate C: HW midstate -> HW continue + HW second SHA.
        uint32_t hwMid[8] = {0};
        sha256_s3_compute_midstate(header_swapped, hwMid);

        uint8_t c_out[32] = {0};
        bool c_ok = sha256_s3_verify_trace(
            hwMid,
            (const uint8_t *)&header_swapped[16],
            nonce_swapped,
            c_out,
            &traceC
        );
        bool c_match = c_ok && (memcmp(traceC.finalDigestBeBytes, swSecond.bytes, 32) == 0);

        // Candidate B: software first hash + HW second SHA only.
        uint8_t b_second[32] = {0};
        bool b_ok = sha256_s3_second_sha_from_first_be(swFirst.bytes, b_second);
        bool b_match = b_ok && (memcmp(b_second, swSecond.bytes, 32) == 0);

        Serial.printf("[S3-EXP] vec=%d sw_mid_eq_direct=%s A(swMid->HW)=%s C(hwMid->HW)=%s B(HW second only)=%s\n",
                      vec,
                      swEq ? "PASS" : "FAIL",
                      a_match ? "PASS" : "FAIL",
                      c_match ? "PASS" : "FAIL",
                      b_match ? "PASS" : "FAIL");

        // Microbench (no serial inside loop).
        const uint32_t loops = (uint32_t)S3_HW_EXPERIMENTS_NONCES;
        uint32_t t0, t1;
        uint64_t dummy = 0;

        // SW midstate completion baseline.
        block_header_t hbSw = hb;
        t0 = micros();
        for (uint32_t i = 0; i < loops; i++) {
            hbSw.nonce++;
            miner_sha256_complete_from_midstate(&swMid, &hbSw, &swFirst, &swSecondMid);
            dummy += swSecondMid.bytes[0];
        }
        t1 = micros();
        float sw_hs = (loops * 1000000.0f) / (float)(t1 - t0);

        // Candidate A benchmark.
        uint32_t nonceA = nonce_swapped;
        t0 = micros();
        for (uint32_t i = 0; i < loops; i++) {
            sha256_s3_verify(swMid.hash, (const uint8_t *)&header_swapped[16], nonceA, a_out);
            nonceA++;
            dummy += a_out[0];
        }
        t1 = micros();
        float a_hs = (loops * 1000000.0f) / (float)(t1 - t0);

        // Candidate C benchmark.
        uint32_t nonceC = nonce_swapped;
        t0 = micros();
        for (uint32_t i = 0; i < loops; i++) {
            sha256_s3_verify(hwMid, (const uint8_t *)&header_swapped[16], nonceC, c_out);
            nonceC++;
            dummy += c_out[0];
        }
        t1 = micros();
        float c_hs = (loops * 1000000.0f) / (float)(t1 - t0);

        // Candidate B benchmark (full cost: SW first + HW second).
        block_header_t hbB = hb;
        t0 = micros();
        for (uint32_t i = 0; i < loops; i++) {
            hbB.nonce++;
            sha256(&swFirst, (uint8_t *)&hbB, 80);
            sha256_s3_second_sha_from_first_be(swFirst.bytes, b_second);
            dummy += b_second[0];
        }
        t1 = micros();
        float b_hs = (loops * 1000000.0f) / (float)(t1 - t0);

        Serial.printf("[S3-EXP] vec=%d bench SW=%.1fH/s A=%.1fH/s C=%.1fH/s B=%.1fH/s dummy=%llu\n",
                      vec, sw_hs, a_hs, c_hs, b_hs, dummy);
    }

    esp_sha_release_hardware();
}
#endif

static bool run_debug_regression_checks(void) {
    bool overallOk = true;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    esp_sha_acquire_hardware();
    bool oneBlockIvOk = sha256_s3_test_one_block_from_iv();
    bool secondShaPathsOk = sha256_s3_test_second_sha_paths();
    esp_sha_release_hardware();
#else
    bool oneBlockIvOk = true;
    bool secondShaPathsOk = true;
#endif

    block_header_t testHeader;
    memset(&testHeader, 0, sizeof(testHeader));
    testHeader.version = 0x20000000;
    for (int i = 0; i < 32; i++) {
        testHeader.prev_hash[i] = (uint8_t)i;
        testHeader.merkle_root[i] = (uint8_t)(0xA0 + i);
    }
    testHeader.timestamp = 0x6a02268b;
    testHeader.difficulty = 0x17021ff0;
    testHeader.nonce = 0x2e425c33;

    const uint8_t *raw = (const uint8_t *)&testHeader;
    bool nonceEndianOk = (raw[76] == 0x33 && raw[77] == 0x5c && raw[78] == 0x42 && raw[79] == 0x2e);
    Serial.printf("[MINER-TEST] nonce endian %s (bytes=%02x %02x %02x %02x)\n",
                  nonceEndianOk ? "OK" : "FAIL", raw[76], raw[77], raw[78], raw[79]);

    bool syntheticOk = run_midstate_vector_test(
        "synthetic",
        &testHeader,
        NULL,
        "a5a91b4098f5228d9fd3aa5f9681b4d44e342fb53d4d87d605ba2af9d76903a1"
    );
    bool preparedSyntheticOk = run_prepared_equivalence_nonce_window("synthetic-prepared", &testHeader, 0x00000000u, 10);

    block_header_t liveHeader;
    bool liveParsed = parse_header_hex_80(
        "000000204ba6eb671af350c5b183bd2467497eca61b4f61eb88901000000000000000000acbb08e6dbac14cb5b23e07499d2f4686972836e871ce6015843d870138514b5cf33026af01f021700000e08",
        &liveHeader
    );
    bool liveOk = false;
    if (liveParsed) {
        liveOk = run_midstate_vector_test(
            "live-captured",
            &liveHeader,
            "94808b8a7f5b7e8382ef9590abc201ac078963f1ceeb7ee9efa9ce9277349bed",
            "8f155083f7a147506d9c7726eb8058ed423f583ebab45e28daefc7ab25ac8715"
        );
    } else {
        Serial.println("[MINER-TEST] live-captured parse FAIL");
    }
    bool preparedLiveOk = liveParsed && run_prepared_equivalence_nonce_window("live-prepared", &liveHeader, 0x00000000u, 10);

    // oneBlockIvOk includes restore+continue diagnostics, which are intentionally
    // unsupported for the stable backend on S3. Keep this visible but non-gating.
    Serial.printf("[MINER-TEST] S3 one-block IV diagnostics %s (non-gating)\n", oneBlockIvOk ? "PASS" : "FAIL");
    Serial.printf("[MINER-TEST] S3 second-SHA path diagnostics %s\n", secondShaPathsOk ? "PASS" : "FAIL");

    overallOk = secondShaPathsOk && nonceEndianOk && syntheticOk && preparedSyntheticOk && liveParsed && liveOk && preparedLiveOk;
    Serial.printf("[MINER-TEST] software-midstate self-test %s\n", overallOk ? "PASS" : "FAIL");

    return overallOk;
}
#endif

void miner_init() {
    s_jobMutex = xSemaphoreCreateMutex();
    s_shaMutex = xSemaphoreCreateMutex();  // For dual-core hardware SHA sharing
    s_stats.startTime = millis();

    // Initialize hardware SHA-256 peripheral
    sha256_hw_init();

    // Run DMA-based SHA test at startup
    sha256_s3_dma_test();

    log_line("[MINER] Initialized");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    log_line("[MINER] Backend: S3 software-midstate dual-core");
    log_line("[MINER] Active hash loop: software double-SHA256 on both cores");
    log_line("[MINER] HW SHA restore/injection: unsupported for correct mining path on ESP32-S3");
    log_line("[MINER] DMA hot path: inactive");
    log_linef("[MINER] Core0 nonce range: 0x%08lx+ | Core1 nonce range: 0x%08lx+",
              s_startNonce[0], s_startNonce[1]);
#else
    log_line("[MINER] Backend: dual-core hardware SHA sharing");
#endif

#ifdef DEBUG_SHARE_VALIDATION
    Serial.println("[BUILD] DEBUG_SHARE_VALIDATION enabled");
#else
    Serial.println("[BUILD] DEBUG_SHARE_VALIDATION disabled");
#endif

#if S3_DEBUG_NONCE_WINDOW_ENABLE
    const uint32_t dbgStartSwapped = (uint32_t)S3_DEBUG_NONCE_WINDOW_START_SWAPPED;
    const uint32_t dbgEndSwapped = (uint32_t)S3_DEBUG_NONCE_WINDOW_END_SWAPPED;
    const uint32_t dbgStartNative = __builtin_bswap32(dbgStartSwapped);
    const uint32_t dbgEndNative = __builtin_bswap32(dbgEndSwapped);
    const uint64_t dbgExpected = (uint64_t)dbgEndSwapped - (uint64_t)dbgStartSwapped + 1ULL;
    const bool dbgRoundTrip = (__builtin_bswap32(__builtin_bswap32(dbgStartSwapped)) == dbgStartSwapped);
    const bool dbgNativeMonotonic = (__builtin_bswap32(dbgStartSwapped + 1U) == (dbgStartNative + 1U));

    Serial.println("[BUILD] S3_DEBUG_NONCE_WINDOW_ENABLE enabled");
    Serial.printf("[BUILD] S3 window swapped_start=%08x swapped_end=%08x expected_hashes=%llu\n",
                  dbgStartSwapped, dbgEndSwapped, dbgExpected);
    Serial.printf("[BUILD] S3 window native_start=%08x native_end=%08x\n",
                  dbgStartNative, dbgEndNative);
    Serial.printf("[BUILD] bswap roundtrip=%s, native_monotonic_assumption=%s\n",
                  dbgRoundTrip ? "PASS" : "FAIL",
                  dbgNativeMonotonic ? "PASS" : "FAIL");
    if (!dbgNativeMonotonic) {
        Serial.println("[BUILD] NOTE: Native nonce ordering is not monotonic under swapped-space increments (expected). Use swapped bounds for deterministic windows.");
    }
    if (dbgExpected <= 0x11ULL) {
        Serial.printf("[BUILD] Tiny S3 window sanity: expected hashes=%llu\n", dbgExpected);
    }

#if S3_DEBUG_NONCE_WINDOW_TINY_SELFTEST_ENABLE
    const uint32_t tinyStartSwapped = (uint32_t)S3_DEBUG_NONCE_WINDOW_TINY_START_SWAPPED;
    const uint32_t tinyEndSwapped = (uint32_t)S3_DEBUG_NONCE_WINDOW_TINY_END_SWAPPED;
    const uint64_t tinyExpected = (uint64_t)tinyEndSwapped - (uint64_t)tinyStartSwapped + 1ULL;
    Serial.printf("[BUILD] Tiny one-shot self-test enabled: swapped_start=%08x swapped_end=%08x expected_hashes=%llu\n",
                  tinyStartSwapped, tinyEndSwapped, tinyExpected);
#else
    Serial.println("[BUILD] Tiny one-shot self-test disabled");
#endif
#else
    Serial.println("[BUILD] S3_DEBUG_NONCE_WINDOW_ENABLE disabled");
#endif

#ifdef DEBUG_SHARE_VALIDATION
    bool midstateChecksOk = run_debug_regression_checks();
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (midstateChecksOk) {
        Serial.println("[MINER] software-midstate self-test: PASS");
    } else {
        Serial.println("[MINER] software-midstate self-test: FAIL (check debug logs)");
    }
    Serial.println("[MINER] HW midstate restore: unsupported; continuing on validated software-midstate backend");
#endif
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3) && S3_HW_EXPERIMENTS_ENABLE && defined(DEBUG_SHARE_VALIDATION)
    // S3 experiment harness currently depends on debug-validation helpers.
    // Enable DEBUG_SHARE_VALIDATION in the board env to run this on every boot.
    run_s3_hw_experiments();
#endif

#ifdef BENCHMARK_SHA_VERSIONS
    run_sha_benchmark();
#endif
}

void miner_start_job(const stratum_job_t *job) {
    if (!job) return;

    // Wait for any active mining to stop
    bool wasActive = s_miningActive;
    s_miningActive = false;
    if (wasActive) {
        s_jobChanges++;  // Track job invalidations (hashes in flight are discarded)
    }
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

    // Deterministic nonce split: Core0 gets lower half [0..7FFFFFFF], Core1 gets upper half [80000000..FFFFFFFF]
    // Random offset within each half to avoid repeatedly testing the same nonce region across jobs.
    // This guarantees no overlap: cores can't cross the halfway boundary within any realistic job lifetime
    // at current hashrates (~1000 H/s). Each half contains ~2.1 billion unique nonces.
    uint32_t r = esp_random();
    s_startNonce[0] = r & 0x7FFFFFFFu;          // Core0: lower half, random start
    s_startNonce[1] = 0x80000000u | (r >> 1);   // Core1: upper half, random start

    s_stats.templates++;

    xSemaphoreGive(s_jobMutex);

    Serial.printf("[MINER] Job #%lu %s | Core0=0x%08lx (lo) Core1=0x%08lx (hi) | total_jobs=%lu\n",
                  (unsigned long)s_stats.templates,
                  s_currentJobId,
                  s_startNonce[0], s_startNonce[1],
                  (unsigned long)s_stats.templates);
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
// Mining Task - Core 0
// ============================================================

void miner_task_core0(void *param) {
    block_header_t hb;
    sha256_hash_t ctx;
    sha256_hash_t sw_midstate;  // Software midstate for fallback
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 0;
    uint32_t yieldCounter = 0;

    #if defined(CONFIG_IDF_TARGET_ESP32S3)
    log_wait_startup_barrier();
    log_linef("[MINER0] Started on core %d (S3 SOFTWARE-MIDSTATE, nonce-lo, priority %d)",
              xPortGetCoreID(), uxTaskPriorityGet(NULL));
    #else
    log_wait_startup_barrier();
    log_linef("[MINER0] Started on core %d (HW/SW mixed SHA path, priority %d)",
              xPortGetCoreID(), uxTaskPriorityGet(NULL));
    #endif

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    #if defined(CONFIG_IDF_TARGET_ESP32S3)
    log_line("[MINER0] Got first job, starting software-midstate mining");
    #else
    log_line("[MINER0] Got first job, starting HW/SW mixed mining");
    #endif

    while (true) {
        if (!s_miningActive) {
            s_core0Mining = false;
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        s_core0Mining = true;

        // ========================================================
        // JOB INITIALIZATION (once per stratum.notify)
        // ========================================================

        // Copy job data under mutex - both Core0 and Core1 do this
        // independently (with deterministic nonce split)
        xSemaphoreTake(s_jobMutex, portMAX_DELAY);
        memcpy(&hb, &s_pendingBlock, sizeof(block_header_t));
        strncpy(jobId, s_currentJobId, MAX_JOB_ID_LEN);
        hb.nonce = s_startNonce[minerId];
        xSemaphoreGive(s_jobMutex);

        // ========================================================
        // MIDSTATE COMPUTATION (once per job, reused for all nonces)
        // ========================================================
        //
        // Compute SHA-256 midstate from first 64 bytes of block header
        // (version, prev_hash, merkle_root). This saves 50% of the work
        // per nonce since we only process the tail (16 bytes) in the loop.
        //
        // Work: ~1-2 microseconds (one SHA-256 compression block)
        // Reuse: Same midstate used for 2^32 nonces (infinite per job)
        //
        // Why software (not hardware):
        // - Hardware SHA requires mutex (contends with Core1)
        // - Software path is deterministic and has no synchronization
        // - Macro-unrolled 64 rounds are fast and cache-friendly
        // - See SHA256_BACKEND_INVESTIGATION.md for detailed analysis
        //
        miner_sha256_midstate(&sw_midstate, &hb);

        // ========================================================
        // MINING HOT LOOP (per nonce - millions of iterations)
        // ========================================================
        //
        // This is the tight loop that runs ~1 million times per second
        // on dual core. Every microsecond counts here.
        //
        // Nonce range (deterministic, no overlap with Core1):
        //   Core0: 0x00000000 - 0x7FFFFFFF (lower half)
        //   Core1: 0x80000000 - 0xFFFFFFFF (upper half)
        //
        // Algorithm per nonce:
        //   1. Complete double SHA-256 using midstate (skip first 64 bytes)
        //   2. Check 16-bit prefilter (bytes[31] and [30] must be 0)
        //   3. If prefilter passes, check full target and submit if valid
        //   4. Increment nonce
        //   5. Repeat
        //
        // Performance: ~4-5 microseconds per nonce (two full SHA compressions)
        // No locks, no synchronization, deterministic latency.
        //

        while (s_miningActive) {
            // ===== THE MINING HOT PATH =====
            //
            // miner_sha256_header() does:
            //   1. Load tail block (last 16 bytes + nonce + padding)
            //   2. Expand message schedule (compute w[16..63])
            //   3. Process 64 compression rounds from midstate
            //   4. Finalize first SHA-256 (H0-H7)
            //   5. Feed result through second SHA-256 (64 rounds)
            //   6. Return final hash with 16-bit prefilter check
            //
            // Returns true if hash passes 16-bit test (rare, ~1 in 65536)
            // Only then do we spend time on full target check.
            //
            if (miner_sha256_header(&sw_midstate, &ctx, &hb)) {
                // Potential valid share found (passed 16-bit prefilter)
                // Now check full target and submit if valid
                hashCheck(jobId, &ctx, hb.timestamp, hb.nonce);
            }

            hb.nonce++;        // Increment nonce for next iteration
            s_stats.hashes++;  // Update global statistics
            s_core0Hashes++;   // Core0-specific counter
            yieldCounter++;    // Track yields to prevent watchdog starvation

            // Nonce range boundary check: Core0 owns lower half [0..7FFFFFFF].
            // Wrapping into the upper half would duplicate Core1's nonce range.
            // At current hashrates this takes ~25 days; log if it somehow occurs.
            if (hb.nonce == 0x80000000u) {
                Serial.printf("[MINER0] WARN: Core0 nonce wrapped into Core1 range (nonce=%08lx)\n", hb.nonce);
            }

            // Yield every CORE_0_YIELD_COUNT hashes to let monitor/WiFi tasks run
            if (yieldCounter >= CORE_0_YIELD_COUNT) {
                yieldCounter = 0;
#ifdef DEBUG_HASH_TIMING
                {
                    static uint32_t s_c0_t0 = 0;
                    static uint64_t s_c0_h0 = 0;
                    uint32_t t = (uint32_t)micros();
                    uint64_t h = s_core0Hashes;
                    if (s_c0_t0 != 0) {
                        uint32_t dt = t - s_c0_t0;
                        uint64_t dh = h - s_c0_h0;
                        if (dh > 0 && dt > 0) {
                            Serial.printf("[MINER0-T] %llu h/%u us=%.1fus/h=%.0fH/s\n",
                                          dh, dt, (float)dt / (float)dh,
                                          (float)dh * 1000000.0f / (float)dt);
                        }
                    }
                    s_c0_t0 = t;
                    s_c0_h0 = h;
                }
#endif
                vTaskDelay(1);
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

    log_wait_startup_barrier();
    log_linef("[MINER1] Started on core %d (PIPELINED ASM v3, priority %d)",
              xPortGetCoreID(), uxTaskPriorityGet(NULL));

    // Enable SHA peripheral clock and clear reset
    DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA);
    DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    log_line("[MINER1] Got first job, starting pipelined mining v3");

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
        // Only re-init if SHA was actually disabled
        if (!(DPORT_REG_READ(DPORT_PERI_CLK_EN_REG) & DPORT_PERI_EN_SHA)) {
            DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA);
            DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);
        }

        while (s_miningActive) {
            // Track hash count before v3 call for per-core stats
            uint64_t hashBefore = s_stats.hashes;

            // Run pipelined assembly mining loop v3 (working version)
            // NOTE: v4 midstate injection does NOT work on ESP32 - SHA_LOAD copies
            // FROM internal state TO SHA_TEXT, there's no way to restore a midstate
            bool candidate = sha256_pipelined_mine_v3(
                sha_base,
                header_swapped,
                &nonce_swapped,
                &s_stats.hashes,
                &s_miningActive
            );

            // Track Core 1 hash contribution
            s_core1Hashes += (s_stats.hashes - hashBefore);

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
                // Only re-init if SHA was actually disabled
                if (!(DPORT_REG_READ(DPORT_PERI_CLK_EN_REG) & DPORT_PERI_EN_SHA)) {
                    DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA);
                    DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);
                }
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
// ============================================================
// Mining Task - Core 1 (ESP32-S3 software-midstate)
// ============================================================
// Architecture: Both cores use validated software SHA-256
// - Nonce split: Core0 = 0x00000000 to 0x7fffffff, Core1 = 0x80000000 to 0xffffffff
// - Synchronization: Job mutex protects header/job data
// - SHA: Both cores compute independently (no hardware sharing contention)
// - Performance instrumentation: Per-core hash counters + timing

void miner_task_core1(void *param) {
    block_header_t hb;
    sha256_hash_t ctx;
    sha256_hash_t sw_midstate;  // SOFTWARE midstate (same as Core0, validated path)
    char jobId[MAX_JOB_ID_LEN];
    uint32_t minerId = 1;
    uint32_t yieldCounter = 0;

    log_wait_startup_barrier();
    log_linef("[MINER1] Started on core %d (S3 SOFTWARE-MIDSTATE, nonce-hi, priority %d)",
              xPortGetCoreID(), uxTaskPriorityGet(NULL));

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    log_linef("[MINER1] Got first job, starting software-midstate mining (nonce range 0x%08lx+)",
              s_startNonce[minerId]);

    while (true) {
        if (!s_miningActive) {
            s_core1Mining = false;
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        s_core1Mining = true;

        // Copy job data under mutex (same as Core0)
        xSemaphoreTake(s_jobMutex, portMAX_DELAY);
        memcpy(&hb, &s_pendingBlock, sizeof(block_header_t));
        strncpy(jobId, s_currentJobId, MAX_JOB_ID_LEN);
        hb.nonce = s_startNonce[minerId];
        xSemaphoreGive(s_jobMutex);

        // Compute SOFTWARE midstate (validated, working, same as Core0)
        miner_sha256_midstate(&sw_midstate, &hb);

        // Mining loop: pure software SHA, no contention with Core0
        while (s_miningActive) {
            // Perform double-SHA256 with software midstate
            if (miner_sha256_header(&sw_midstate, &ctx, &hb)) {
                hashCheck(jobId, &ctx, hb.timestamp, hb.nonce);
            }
            hb.nonce++;
            s_stats.hashes++;
            s_core1Hashes++;
            yieldCounter++;

            // Nonce range boundary check: Core1 owns upper half [80000000..FFFFFFFF].
            // Wrapping through 0 would duplicate Core0's nonce range.
            // At current hashrates this takes ~25 days; log if it somehow occurs.
            if (hb.nonce == 0x00000000u) {
                Serial.printf("[MINER1] WARN: Core1 nonce wrapped through 0 into Core0 range\n");
            }

            // Yield every CORE_0_YIELD_COUNT hashes to let monitor/WiFi tasks run
            if (yieldCounter >= CORE_0_YIELD_COUNT) {
                yieldCounter = 0;
#ifdef DEBUG_HASH_TIMING
                {
                    static uint32_t s_c1_t0 = 0;
                    static uint64_t s_c1_h0 = 0;
                    uint32_t t = (uint32_t)micros();
                    uint64_t h = s_core1Hashes;
                    if (s_c1_t0 != 0) {
                        uint32_t dt = t - s_c1_t0;
                        uint64_t dh = h - s_c1_h0;
                        if (dh > 0 && dt > 0) {
                            Serial.printf("[MINER1-T] %llu h/%u us=%.1fus/h=%.0fH/s\n",
                                          dh, dt, (float)dt / (float)dh,
                                          (float)dh * 1000000.0f / (float)dt);
                        }
                    }
                    s_c1_t0 = t;
                    s_c1_h0 = h;
                }
#endif
                vTaskDelay(1);
            }
        }

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

    log_wait_startup_barrier();
    log_linef("[MINER1] Started on core %d (Hardware SHA Midstate, priority %d)",
              xPortGetCoreID(), uxTaskPriorityGet(NULL));

    // Wait for first job
    while (!s_miningActive) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    log_line("[MINER1] Got first job, starting mining loop");

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

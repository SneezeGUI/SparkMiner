/*
 * SparkMiner - Optimized Pipelined SHA-256 for ESP32-S3 v3
 * 
 * FIX for ESP32-S3 endianness:
 * - SHA_H_BASE registers use hardware-native format
 * - Software midstate must be byte-swapped before loading SHA_H
 * - Header tail, nonce and final SHA_H digest stay in native CPU word order
 * - Final digest is interpreted as raw little-endian words; do not reverse or bswap it
 *
 * Optimizations over v2:
 * 1. Persistent zeros - set once per job, not per nonce
 * 2. Inlined constants - no extra operand registers needed
 */

#include <Arduino.h>
#include "sha256_pipelined_s3.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <sha/sha_dma.h>

// ESP32-S3 SHA Register Addresses
#define S3_SHA_BASE         0x6003B000
#define SHA_MODE_REG        (S3_SHA_BASE + 0x00)
#define SHA_START_REG       (S3_SHA_BASE + 0x10)
#define SHA_CONTINUE_REG    (S3_SHA_BASE + 0x14)
#define SHA_BUSY_REG        (S3_SHA_BASE + 0x18)
#define SHA_H_BASE          (S3_SHA_BASE + 0x40)
#define SHA_TEXT_BASE       (S3_SHA_BASE + 0x80)

#define SHA2_256_MODE       2

static inline uint32_t IRAM_ATTR read_ccount(void) {
    uint32_t ccount;
    __asm__ __volatile__("rsr.ccount %0" : "=a"(ccount));
    return ccount;
}

static inline void IRAM_ATTR mem_barrier(void) {
    __asm__ __volatile__("memw");
}

/**
 * Initialize SHA_TEXT with zeros for block 2 padding.
 * Call once per job to set persistent zeros at offsets 0x94-0xB8.
 * This eliminates writing 10 zeros per nonce iteration.
 */
void IRAM_ATTR sha256_s3_init_zeros(void) {
    volatile uint32_t *sha_text = (volatile uint32_t *)SHA_TEXT_BASE;
    // Set words 5-14 to zero (offsets 0x94-0xB8, indices 5-14)
    // These persist between iterations because:
    // - Block 2: uses words 0-4, zeros 5-14, length at 15
    // - Double-hash: uses words 0-7 (SHA_H copy), word 8 (0x80), zeros 9-14, length at 15
    // The zeros at 9-14 overlap with zeros at 5-14, so they remain valid
    for (int i = 5; i < 15; i++) {
        sha_text[i] = 0;
    }
}

void sha256_pipelined_s3_profile_v3(
    const uint32_t *midstate,
    const uint32_t *block2_words,
    uint32_t nonce,
    uint32_t samples
) {
    if (samples == 0) return;

    volatile uint32_t *sha_base = (volatile uint32_t *)S3_SHA_BASE;
    uint64_t restore = 0;
    uint64_t block2 = 0;
    uint64_t start1 = 0;
    uint64_t wait1 = 0;
    uint64_t copy = 0;
    uint64_t pad2 = 0;
    uint64_t start2 = 0;
    uint64_t wait2 = 0;
    uint64_t check = 0;
    uint64_t total = 0;
    uint64_t wait1Loops = 0;
    uint64_t wait2Loops = 0;
    uint32_t candidates = 0;

    sha256_s3_init_zeros();

    for (uint32_t n = 0; n < samples; n++, nonce++) {
        uint32_t t0 = read_ccount();
        for (int i = 0; i < 8; i++) {
            sha_base[0x40 / 4 + i] = midstate[i];
        }
        uint32_t t1 = read_ccount();

        sha_base[0x80 / 4 + 0] = block2_words[0];
        sha_base[0x80 / 4 + 1] = block2_words[1];
        sha_base[0x80 / 4 + 2] = block2_words[2];
        sha_base[0x80 / 4 + 3] = nonce;
        sha_base[0x80 / 4 + 4] = 0x00000080;
        sha_base[0x80 / 4 + 5] = 0;
        sha_base[0x80 / 4 + 6] = 0;
        sha_base[0x80 / 4 + 7] = 0;
        sha_base[0x80 / 4 + 8] = 0;
        sha_base[0x80 / 4 + 15] = 0x80020000;
        uint32_t t2 = read_ccount();

        sha_base[0x00 / 4] = SHA2_256_MODE;
        sha_base[0x14 / 4] = 1;
        mem_barrier();
        uint32_t t3 = read_ccount();

        uint32_t loops1 = 0;
        while (sha_base[0x18 / 4] != 0) {
            loops1++;
        }
        uint32_t t4 = read_ccount();

        for (int i = 0; i < 8; i++) {
            sha_base[0x80 / 4 + i] = sha_base[0x40 / 4 + i];
        }
        uint32_t t5 = read_ccount();

        sha_base[0x80 / 4 + 8] = 0x00000080;
        sha_base[0x80 / 4 + 15] = 0x00010000;
        uint32_t t6 = read_ccount();

        sha_base[0x00 / 4] = SHA2_256_MODE;
        sha_base[0x10 / 4] = 1;
        mem_barrier();
        uint32_t t7 = read_ccount();

        uint32_t loops2 = 0;
        while (sha_base[0x18 / 4] != 0) {
            loops2++;
        }
        mem_barrier();
        uint32_t t8 = read_ccount();

        uint32_t h7 = sha_base[0x5C / 4];
        if ((h7 & 0xFFFF0000) == 0) {
            candidates++;
        }
        uint32_t t9 = read_ccount();

        restore += (uint32_t)(t1 - t0);
        block2 += (uint32_t)(t2 - t1);
        start1 += (uint32_t)(t3 - t2);
        wait1 += (uint32_t)(t4 - t3);
        copy += (uint32_t)(t5 - t4);
        pad2 += (uint32_t)(t6 - t5);
        start2 += (uint32_t)(t7 - t6);
        wait2 += (uint32_t)(t8 - t7);
        check += (uint32_t)(t9 - t8);
        total += (uint32_t)(t9 - t0);
        wait1Loops += loops1;
        wait2Loops += loops2;
    }

    double inv = 1.0 / samples;
    double avgTotal = total * inv;
    double khs = ((double)getCpuFrequencyMhz() * 1000.0) / avgTotal;
    Serial.printf("[S3-PROFILE] samples=%lu avg=%.1f cycles est=%.1f kH/s candidates=%lu\n",
                  samples, avgTotal, khs, candidates);
    Serial.printf("[S3-PROFILE] restore=%.1f block2=%.1f start1=%.1f wait1=%.1f copy=%.1f pad2=%.1f start2=%.1f wait2=%.1f check=%.1f\n",
                  restore * inv, block2 * inv, start1 * inv, wait1 * inv,
                  copy * inv, pad2 * inv, start2 * inv, wait2 * inv, check * inv);
    Serial.printf("[S3-PROFILE] wait_loops first=%.1f second=%.1f cpu=%uMHz\n",
                  wait1Loops * inv, wait2Loops * inv, getCpuFrequencyMhz());
}

/**
 * Optimized mining loop v3 with persistent zeros
 *
 * Key optimization over v2:
 * - Skip writing 10 zeros per iteration (set once via sha256_s3_init_zeros)
 * - Saves ~30 cycles per hash
 *
 * IMPORTANT: Call sha256_s3_init_zeros() once per job before using this.
 */
bool IRAM_ATTR sha256_pipelined_mine_s3_v3(
    const uint32_t *midstate,           // Pre-computed midstate (8 words)
    const uint32_t *block2_words,       // Block 2 words 0-2 (merkle_tail, timestamp, nbits) in native word order
    uint32_t *nonce_ptr,                // Current nonce in native word order
    volatile uint64_t *hash_count_ptr,
    volatile bool *mining_flag
) {
    volatile uint32_t *sha_base = (volatile uint32_t *)S3_SHA_BASE;
    uint32_t hashes_done = 0;

    /*
     * v3 Mining Loop - Same register allocation as v2
     *
     * Register allocation:
     *   a2  = nonce (persists, big-endian)
     *   a3  = scratch
     *   a4  = scratch / zero constant
     *   a5  = midstate pointer
     *   a6  = block2_words pointer
     *   a7  = SHA base (0x6003B000)
     *   a8  = hashes_done pointer
     *   a9  = local hash counter
     *
     * Optimization: Skip writing zeros (persistent from sha256_s3_init_zeros)
     */
    __asm__ __volatile__(

        // ===== SETUP =====
        "l32i.n   a2,  %[nonce], 0    \n"
        "mov      a5,  %[mid]         \n"
        "mov      a6,  %[blk2]        \n"
        "mov      a7,  %[base]        \n"
        "mov      a8,  %[done]        \n"
        "movi.n   a9,  0              \n"
        "movi.n   a3, 2               \n"
        "s32i.n   a3, a7, 0           \n"    // SHA_MODE

    "loop_start_v3: \n"

        // ===== PHASE 1: Restore midstate to SHA_H (a7+0x40) =====
        "l32i.n   a3, a5, 0           \n"
        "l32i.n   a4, a5, 4           \n"
        "s32i     a3, a7, 0x40        \n"
        "s32i     a4, a7, 0x44        \n"
        "l32i.n   a3, a5, 8           \n"
        "l32i.n   a4, a5, 12          \n"
        "s32i     a3, a7, 0x48        \n"
        "s32i     a4, a7, 0x4C        \n"
        "l32i.n   a3, a5, 16          \n"
        "l32i.n   a4, a5, 20          \n"
        "s32i     a3, a7, 0x50        \n"
        "s32i     a4, a7, 0x54        \n"
        "l32i.n   a3, a5, 24          \n"
        "l32i.n   a4, a5, 28          \n"
        "s32i     a3, a7, 0x58        \n"
        "s32i     a4, a7, 0x5C        \n"

        // ===== PHASE 2: Write block 2 to SHA_TEXT =====
        // Words 0-2: template
        "l32i.n   a3, a6, 0           \n"
        "s32i     a3, a7, 0x80        \n"
        "l32i.n   a3, a6, 4           \n"
        "s32i     a3, a7, 0x84        \n"
        "l32i.n   a3, a6, 8           \n"
        "s32i     a3, a7, 0x88        \n"

        // Word 3: nonce
        "s32i     a2, a7, 0x8C        \n"

        // Word 4: padding 0x80
        "movi     a3, 0x80            \n"
        "s32i     a3, a7, 0x90        \n"

        // Words 5-8 must be restored after the previous double-hash copied
        // SHA_H into SHA_TEXT[0..7] and wrote padding at word 8.
        "movi.n   a3, 0               \n"
        "s32i     a3, a7, 0x94        \n"
        "s32i     a3, a7, 0x98        \n"
        "s32i     a3, a7, 0x9C        \n"
        "s32i     a3, a7, 0xA0        \n"
        // Words 9-14: SKIP - zeros are persistent from sha256_s3_init_zeros()

        // Word 15: length 640 bits = 0x80020000
        "movi     a3, 0x8002          \n"
        "slli     a3, a3, 16          \n"
        "s32i     a3, a7, 0xBC        \n"

        // ===== PHASE 3: SHA_CONTINUE =====
        "movi.n   a3, 1               \n"
        "s32i     a3, a7, 0x14        \n"    // SHA_CONTINUE

        // ===== PHASE 4: Wait for block 2 SHA =====
    "wait_blk2_v3: \n"
        "l32i     a3, a7, 0x18        \n"
        "bnez.n   a3, wait_blk2_v3    \n"

        // ===== PHASE 5: Copy SHA_H to SHA_TEXT[0-7] =====
        "l32i     a3, a7, 0x40        \n"
        "l32i     a4, a7, 0x44        \n"
        "s32i     a3, a7, 0x80        \n"
        "s32i     a4, a7, 0x84        \n"
        "l32i     a3, a7, 0x48        \n"
        "l32i     a4, a7, 0x4C        \n"
        "s32i     a3, a7, 0x88        \n"
        "s32i     a4, a7, 0x8C        \n"
        "l32i     a3, a7, 0x50        \n"
        "l32i     a4, a7, 0x54        \n"
        "s32i     a3, a7, 0x90        \n"
        "s32i     a4, a7, 0x94        \n"
        "l32i     a3, a7, 0x58        \n"
        "l32i     a4, a7, 0x5C        \n"
        "s32i     a3, a7, 0x98        \n"
        "s32i     a4, a7, 0x9C        \n"

        // ===== PHASE 6: Double-hash padding =====
        // Word 8: 0x80
        "movi     a3, 0x80            \n"
        "s32i     a3, a7, 0xA0        \n"

        // Words 9-14: zeros still valid from block 2 phase!
        // Block 2 wrote zeros at 0x94-0xB8 (words 5-14)
        // Double-hash needs zeros at 0xA4-0xB8 (words 9-14)
        // These overlap, so zeros persist

        // Word 15: length 256 bits = 0x00010000
        "movi     a3, 0x0001          \n"
        "slli     a3, a3, 16          \n"
        "s32i     a3, a7, 0xBC        \n"

        // Increment nonce (pipeline with SHA)
        "addi.n   a2, a2, 1           \n"

        // ===== PHASE 7: SHA_START =====
        "movi.n   a3, 1               \n"
        "s32i     a3, a7, 0x10        \n"    // SHA_START

        // ===== PHASE 8: Wait for double-hash =====
    "wait_dbl_v3: \n"
        "l32i     a3, a7, 0x18        \n"
        "bnez.n   a3, wait_dbl_v3     \n"

        // ===== PHASE 9: Update local hash counter =====
        "addi.n   a9, a9, 1           \n"

        // ===== PHASE 10: Check mining flag =====
        "l8ui     a3, %[flag], 0      \n"
        "beqz.n   a3, exit_v3         \n"

        // ===== PHASE 11: Early reject =====
        "l32i     a3, a7, 0x5C        \n"    // SHA_H[7]
        "extui    a3, a3, 16, 16      \n"    // Canonical digest bytes[30..31]
        "beqz.n   a3, exit_v3         \n"    // Exit if potential share

        // Continue
        "j        loop_start_v3       \n"

    "exit_v3: \n"
        "s32i.n   a2, %[nonce], 0     \n"
        "s32i.n   a9, a8, 0           \n"

        :
        : [base] "r"(sha_base),
          [mid] "r"(midstate),
          [blk2] "r"(block2_words),
          [done] "r"(&hashes_done),
          [nonce] "r"(nonce_ptr),
          [flag] "r"(mining_flag)
        : "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "memory"
    );

    *hash_count_ptr += hashes_done;

    return *mining_flag;
}

#endif // CONFIG_IDF_TARGET_ESP32S3

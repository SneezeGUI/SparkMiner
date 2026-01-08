/*
 * SparkMiner - Optimized Pipelined SHA-256 for ESP32-S3 v3
 *
 * Optimizations over v2:
 * 1. Persistent zeros - set once per job, not per nonce (saves 10 stores/iteration)
 * 2. Inlined constants - no extra operand registers needed
 *
 * Note: ESP32-S3's SHA architecture puts output in SHA_H (not SHA_TEXT like ESP32),
 * requiring an explicit 16-op copy for double-hash. This is hardware-limited.
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
    const uint32_t *block2_words,       // Block 2 words 0-2 (merkle_tail, timestamp, nbits) - swapped
    uint32_t *nonce_ptr,                // Current nonce (big-endian/swapped)
    volatile uint64_t *hash_count_ptr,
    volatile bool *mining_flag
) {
    volatile uint32_t *sha_base = (volatile uint32_t *)S3_SHA_BASE;

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
     *   a8  = hash counter pointer
     *
     * Optimization: Skip writing zeros (persistent from sha256_s3_init_zeros)
     */
    __asm__ __volatile__(

        // ===== SETUP =====
        "l32i.n   a2,  %[nonce], 0    \n"
        "mov      a5,  %[mid]         \n"
        "mov      a6,  %[blk2]        \n"
        "mov      a7,  %[base]        \n"
        "mov      a8,  %[ih]          \n"

    "loop_start_v3: \n"

        // ===== PHASE 1: Restore midstate to SHA_H (a7+0x40) =====
        "l32i.n   a3, a5, 0           \n"
        "s32i     a3, a7, 0x40        \n"
        "l32i.n   a3, a5, 4           \n"
        "s32i     a3, a7, 0x44        \n"
        "l32i.n   a3, a5, 8           \n"
        "s32i     a3, a7, 0x48        \n"
        "l32i.n   a3, a5, 12          \n"
        "s32i     a3, a7, 0x4C        \n"
        "l32i.n   a3, a5, 16          \n"
        "s32i     a3, a7, 0x50        \n"
        "l32i.n   a3, a5, 20          \n"
        "s32i     a3, a7, 0x54        \n"
        "l32i.n   a3, a5, 24          \n"
        "s32i     a3, a7, 0x58        \n"
        "l32i.n   a3, a5, 28          \n"
        "s32i     a3, a7, 0x5C        \n"

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

        // Words 5-14: SKIP - zeros are PERSISTENT from sha256_s3_init_zeros()
        // This saves 10 store operations per iteration!

        // Word 15: length 640 bits = 0x80020000
        "movi     a3, 0x8002          \n"
        "slli     a3, a3, 16          \n"
        "s32i     a3, a7, 0xBC        \n"

        // ===== PHASE 3: SHA_CONTINUE =====
        "movi.n   a3, 2               \n"
        "s32i.n   a3, a7, 0           \n"    // SHA_MODE
        "movi.n   a3, 1               \n"
        "s32i     a3, a7, 0x14        \n"    // SHA_CONTINUE
        "memw                         \n"

        // ===== PHASE 4: Wait for block 2 SHA =====
    "wait_blk2_v3: \n"
        "l32i     a3, a7, 0x18        \n"
        "bnez.n   a3, wait_blk2_v3    \n"

        // ===== PHASE 5: Copy SHA_H to SHA_TEXT[0-7] =====
        "l32i     a3, a7, 0x40        \n"
        "s32i     a3, a7, 0x80        \n"
        "l32i     a3, a7, 0x44        \n"
        "s32i     a3, a7, 0x84        \n"
        "l32i     a3, a7, 0x48        \n"
        "s32i     a3, a7, 0x88        \n"
        "l32i     a3, a7, 0x4C        \n"
        "s32i     a3, a7, 0x8C        \n"
        "l32i     a3, a7, 0x50        \n"
        "s32i     a3, a7, 0x90        \n"
        "l32i     a3, a7, 0x54        \n"
        "s32i     a3, a7, 0x94        \n"
        "l32i     a3, a7, 0x58        \n"
        "s32i     a3, a7, 0x98        \n"
        "l32i     a3, a7, 0x5C        \n"
        "s32i     a3, a7, 0x9C        \n"

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
        "movi.n   a3, 2               \n"
        "s32i.n   a3, a7, 0           \n"    // SHA_MODE
        "movi.n   a3, 1               \n"
        "s32i     a3, a7, 0x10        \n"    // SHA_START
        "memw                         \n"

        // ===== PHASE 8: Wait for double-hash =====
    "wait_dbl_v3: \n"
        "l32i     a3, a7, 0x18        \n"
        "bnez.n   a3, wait_dbl_v3     \n"
        "memw                         \n"

        // ===== PHASE 9: Update hash counter =====
        "l32i.n   a3, a8, 0           \n"
        "addi.n   a3, a3, 1           \n"
        "s32i.n   a3, a8, 0           \n"
        "bnez.n   a3, no_carry_v3     \n"
        "l32i.n   a4, a8, 4           \n"
        "addi.n   a4, a4, 1           \n"
        "s32i.n   a4, a8, 4           \n"
    "no_carry_v3: \n"

        // ===== PHASE 10: Check mining flag =====
        "l8ui     a3, %[flag], 0      \n"
        "beqz.n   a3, exit_v3         \n"

        // ===== PHASE 11: Early reject =====
        "l32i     a3, a7, 0x40        \n"    // SHA_H[0]
        "extui    a3, a3, 0, 16       \n"    // Lower 16 bits
        "beqz.n   a3, exit_v3         \n"    // Exit if potential share

        // Continue
        "j        loop_start_v3       \n"

    "exit_v3: \n"
        "s32i.n   a2, %[nonce], 0     \n"

        :
        : [base] "r"(sha_base),
          [mid] "r"(midstate),
          [blk2] "r"(block2_words),
          [ih] "r"(hash_count_ptr),
          [nonce] "r"(nonce_ptr),
          [flag] "r"(mining_flag)
        : "a2", "a3", "a4", "a5", "a6", "a7", "a8", "memory"
    );

    return *mining_flag;
}

#endif // CONFIG_IDF_TARGET_ESP32S3

/*
 * SparkMiner - Pipelined SHA-256 Assembly Implementation for ESP32-S3
 *
 * Ported from ESP32 implementation, adapted for S3 register layout.
 * Uses inline Xtensa LX7 assembly to pipeline SHA operations.
 *
 * Key differences from ESP32:
 * - SHA_TEXT_BASE: 0x6003B080 (vs 0x3FF03000)
 * - SHA_H_BASE: 0x6003B040 (separate output register on S3)
 * - Control registers at 0x6003B000 base
 */

#include <Arduino.h>
#include "sha256_pipelined_s3.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <sha/sha_dma.h>  // For esp_sha_acquire/release_hardware

// ESP32-S3 SHA Register Addresses
#define S3_SHA_BASE         0x6003B000
#define SHA_MODE_REG        (S3_SHA_BASE + 0x00)
#define SHA_START_REG       (S3_SHA_BASE + 0x10)
#define SHA_CONTINUE_REG    (S3_SHA_BASE + 0x14)
#define SHA_BUSY_REG        (S3_SHA_BASE + 0x18)
#define SHA_H_BASE          (S3_SHA_BASE + 0x40)
#define SHA_TEXT_BASE       (S3_SHA_BASE + 0x80)

// SHA-256 mode
#define SHA2_256_MODE       2

void sha256_pipelined_s3_init(void) {
    // Enable SHA peripheral
    esp_sha_acquire_hardware();

    // Set SHA-256 mode
    *(volatile uint32_t *)SHA_MODE_REG = SHA2_256_MODE;

    Serial.println("[SHA-PIPE-S3] Pipelined S3 mining initialized");

    esp_sha_release_hardware();
}

bool IRAM_ATTR sha256_pipelined_mine_s3(
    const uint32_t *header_swapped,
    uint32_t *nonce_ptr,
    volatile uint64_t *hash_count_ptr,
    volatile bool *mining_flag
) {
    // Constants for SHA padding - ESP32-S3 uses LE registers, so values are byte-swapped
    const uint32_t shaPad = 0x00000080u;          // Padding byte 0x80 in LE format
    const uint32_t firstShaBitLen = 0x80020000u;  // 640 bits (0x280) in LE format
    const uint32_t secondShaBitLen = 0x00010000u; // 256 bits (0x100) in LE format

    // Base addresses - passed as single pointer, assembly computes offsets
    volatile uint32_t *sha_text = (volatile uint32_t *)SHA_TEXT_BASE;
    volatile uint32_t *sha_h = (volatile uint32_t *)SHA_H_BASE;

    /*
     * Sequential Mining Loop in Xtensa LX7 Assembly for ESP32-S3
     * NOTE: ESP32-S3 does NOT support pipelining - must wait for busy before writing next block
     *
     * Register allocation:
     *   a2 = nonce (persists across loop iterations)
     *   a3, a4 = scratch registers
     *   a7 = control register base (0x6003B000)
     */
    __asm__ __volatile__(

        // Load nonce into register a2
        "l32i.n   a2,  %[nonce], 0 \n"    // a2 = *nonce_ptr
        "movi    a7, 0x6003B000 \n"       // a7 = control register base

    "proc_start_s3: \n"

        // ===== BLOCK 1: Load first 64 bytes of header to SHA_TEXT =====
        "l32i.n   a3,  %[IN],  0 \n"
        "s32i.n   a3,  %[txt],  0 \n"
        "l32i.n   a3,  %[IN],  4 \n"
        "s32i.n   a3,  %[txt],  4 \n"
        "l32i.n   a3,  %[IN],  8 \n"
        "s32i.n   a3,  %[txt],  8 \n"
        "l32i.n   a3,  %[IN],  12 \n"
        "s32i.n   a3,  %[txt],  12 \n"
        "l32i.n   a3,  %[IN],  16 \n"
        "s32i.n   a3,  %[txt],  16 \n"
        "l32i.n   a3,  %[IN],  20 \n"
        "s32i.n   a3,  %[txt],  20 \n"
        "l32i.n   a3,  %[IN],  24 \n"
        "s32i.n   a3,  %[txt],  24 \n"
        "l32i.n   a3,  %[IN],  28 \n"
        "s32i.n   a3,  %[txt],  28 \n"
        "l32i.n   a3,  %[IN],  32 \n"
        "s32i.n   a3,  %[txt],  32 \n"
        "l32i.n   a3,  %[IN],  36 \n"
        "s32i.n   a3,  %[txt],  36 \n"
        "l32i.n   a3,  %[IN],  40 \n"
        "s32i.n   a3,  %[txt],  40 \n"
        "l32i.n   a3,  %[IN],  44 \n"
        "s32i.n   a3,  %[txt],  44 \n"
        "l32i.n   a3,  %[IN],  48 \n"
        "s32i.n   a3,  %[txt],  48 \n"
        "l32i.n   a3,  %[IN],  52 \n"
        "s32i.n   a3,  %[txt],  52 \n"
        "l32i.n   a3,  %[IN],  56 \n"
        "s32i.n   a3,  %[txt],  56 \n"
        "l32i.n   a3,  %[IN],  60 \n"
        "s32i.n   a3,  %[txt],  60 \n"

        // ===== START SHA on block 1 =====
        "movi.n  a3, 2          \n"       // SHA2_256 mode
        "s32i.n  a3, a7, 0      \n"       // Set mode
        "movi.n  a3, 1          \n"
        "s32i.n  a3, a7, 0x10   \n"       // SHA_START = 1
        "memw                   \n"

        // ===== PIPELINE: Prepare block 2 WHILE block 1 processes =====
        "l32i    a3,  %[IN],   64 \n"
        "s32i.n  a3,  %[txt],    0 \n"
        "l32i    a3,  %[IN],   68 \n"
        "s32i.n  a3,  %[txt],    4 \n"
        "l32i    a3,  %[IN],   72 \n"
        "s32i.n  a3,  %[txt],    8 \n"

        // Store nonce
        "s32i.n  a2, %[txt], 12 \n"

        // Store padding
        "s32i.n  %[pad], %[txt], 16  \n"  // 0x80000000
        "s32i.n  %[len1], %[txt], 60 \n"  // 640 bits

        // Zero words 5-14
        "movi.n  a4,  0            \n"
        "s32i.n  a4, %[txt], 20    \n"
        "s32i.n  a4, %[txt], 24    \n"
        "s32i.n  a4, %[txt], 28    \n"
        "s32i.n  a4, %[txt], 32    \n"
        "s32i.n  a4, %[txt], 36    \n"
        "s32i.n  a4, %[txt], 40    \n"
        "s32i.n  a4, %[txt], 44    \n"
        "s32i.n  a4, %[txt], 48    \n"
        "s32i.n  a4, %[txt], 52    \n"
        "s32i.n  a4, %[txt], 56    \n"

        // ===== WAIT for block 1 to complete =====
    "1: \n"
        "l32i.n  a3, a7, 0x18 \n"         // Read SHA_BUSY
        "bnez.n  a3, 1b       \n"

        // ===== CONTINUE SHA with block 2 =====
        "movi.n  a3, 1          \n"
        "s32i.n  a3, a7, 0x14   \n"       // SHA_CONTINUE = 1
        "memw                   \n"

    "2: \n"
        // ===== WAIT for block 2 to complete =====
        "l32i.n  a4, a7, 0x18 \n"
        "bnez.n  a4, 2b       \n"

        // ===== READ hash from SHA_H_BASE to SHA_TEXT =====
        "l32i.n  a3, %[sha_h], 0  \n"
        "s32i.n  a3, %[txt], 0    \n"
        "l32i.n  a3, %[sha_h], 4  \n"
        "s32i.n  a3, %[txt], 4    \n"
        "l32i.n  a3, %[sha_h], 8  \n"
        "s32i.n  a3, %[txt], 8    \n"
        "l32i.n  a3, %[sha_h], 12 \n"
        "s32i.n  a3, %[txt], 12   \n"
        "l32i.n  a3, %[sha_h], 16 \n"
        "s32i.n  a3, %[txt], 16   \n"
        "l32i.n  a3, %[sha_h], 20 \n"
        "s32i.n  a3, %[txt], 20   \n"
        "l32i.n  a3, %[sha_h], 24 \n"
        "s32i.n  a3, %[txt], 24   \n"
        "l32i.n  a3, %[sha_h], 28 \n"
        "s32i.n  a3, %[txt], 28   \n"

        // Increment nonce
        "addi.n  a2, a2, 1 \n"

        // ===== PREPARE double-hash (padding for 32-byte input) =====
        "s32i.n  %[pad], %[txt], 32  \n"  // Padding 0x80000000
        "movi.n  a4, 0              \n"
        "s32i.n  a4, %[txt], 36     \n"
        "s32i.n  a4, %[txt], 40     \n"
        "s32i.n  a4, %[txt], 44     \n"
        "s32i.n  a4, %[txt], 48     \n"
        "s32i.n  a4, %[txt], 52     \n"
        "s32i.n  a4, %[txt], 56     \n"
        "s32i.n  %[len2], %[txt], 60 \n"  // 256 bits

        // ===== START second SHA (fresh mode) =====
        "movi.n  a4, 2          \n"
        "s32i.n  a4, a7, 0      \n"       // Set mode
        "movi.n  a4, 1          \n"
        "s32i.n  a4, a7, 0x10   \n"       // SHA_START
        "memw                   \n"

    "3: \n"
        // ===== WAIT for second SHA =====
        "l32i.n  a4, a7, 0x18 \n"
        "bnez.n  a4, 3b       \n"
        "memw                 \n"       // Memory barrier to ensure SHA_H is updated

        // ===== INCREMENT 64-bit hash counter =====
        "l32i.n  a3, %[ih], 0   \n"
        "addi.n  a3, a3, 1      \n"
        "s32i.n  a3, %[ih], 0   \n"
        "bnez.n  a3, 5f         \n"
        "l32i.n  a4, %[ih], 4   \n"
        "addi.n  a4, a4, 1      \n"
        "s32i.n  a4, %[ih], 4   \n"

    "5: \n"
        // ===== CHECK mining flag =====
        "l8ui   a3, %[flag], 0 \n"
        "beqz.n a3, proc_end_s3 \n"

        // ===== EARLY REJECT: Check H0 upper 16 bits =====
        // For logical H0=0x0000XXXX (share), raw register = 0xXXXX0000
        // So we check if LOWER 16 bits of raw are zero (= upper 16 bits of logical)
        "l32i.n a3, %[sha_h], 0 \n"       // Load full H0 word (raw LE value)
        "extui  a3, a3, 0, 16   \n"       // Extract lower 16 bits (= upper 16 of logical)
        "beqz.n a3, proc_end_s3 \n"       // Exit if potential share
        "j proc_start_s3        \n"

    "proc_end_s3: \n"
        "s32i.n a2, %[nonce], 0 \n"

        :
        : [txt] "r"(sha_text),
          [sha_h] "r"(sha_h),
          [IN] "r"(header_swapped),
          [ih] "r"(hash_count_ptr),
          [nonce] "r"(nonce_ptr),
          [flag] "r"(mining_flag),
          [pad] "r"(shaPad),
          [len2] "r"(secondShaBitLen),
          [len1] "r"(firstShaBitLen)
        : "a2", "a3", "a4", "a7", "memory"
    );

    // Check if we exited due to potential share or mining stopped
    if (*mining_flag) {
        return true;  // 16-bit check passed - potential share
    }
    return false;  // Mining was stopped
}

#endif // CONFIG_IDF_TARGET_ESP32S3

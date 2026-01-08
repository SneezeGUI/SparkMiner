/*
 * SparkMiner - Optimized Pipelined SHA-256 Assembly v2 for ESP32
 *
 * Optimizations over v1:
 * 1. Unrolled zero loop - eliminates loop overhead (~20 cycles saved)
 * 2. Better pipelining - more work during SHA waits
 * 3. Reduced instruction count where possible
 *
 * Note: ESP32 doesn't support midstate caching (no writable state registers)
 * so we must reload block 1 every iteration. This is a hardware limitation.
 */

#include <Arduino.h>
#include "sha256_asm.h"

#if defined(CONFIG_IDF_TARGET_ESP32)

#include <soc/dport_reg.h>
#include <soc/hwcrypto_reg.h>
#include <hal/sha_ll.h>

#define SHA_START_OFFSET    0x90
#define SHA_CONTINUE_OFFSET 0x94
#define SHA_LOAD_OFFSET     0x98
#define SHA_BUSY_OFFSET     0x9C

bool IRAM_ATTR sha256_pipelined_mine_v2(
    volatile uint32_t *sha_base,
    const uint32_t *header_swapped,
    uint32_t *nonce_ptr,
    volatile uint64_t *hash_count_ptr,
    volatile bool *mining_flag
) {
    const uint32_t shaPad = 0x80000000u;
    const uint32_t firstShaBitLen = 0x00000280u;  // 640 bits
    const uint32_t secondShaBitLen = 0x00000100u; // 256 bits

    // Ensure SHA peripheral is enabled
    DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA);
    DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);

    /*
     * v2 Pipelined Mining Loop
     *
     * Key optimizations:
     * - Unrolled zero writes (no loop overhead)
     * - Maximized work during SHA wait periods
     * - Reduced instruction count
     *
     * Register allocation:
     *   a2 = nonce (big-endian, persists)
     *   a3, a4 = scratch
     *   a5 = SHA control base (sha_base + 0x90)
     *   a8 = zero constant (reused)
     */
    __asm__ __volatile__(

        // Setup
        "l32i.n   a2,  %[nonce], 0 \n"
        "addi     a5,  %[sb], 0x90 \n"
        "movi.n   a8,  0           \n"    // Persistent zero for efficiency

    "v2_start: \n"

        // ===== BLOCK 1: Load first 64 bytes =====
        // Load words 0-15 from header to SHA_TEXT
        "l32i.n    a3,  %[IN],  0  \n"
        "s32i.n    a3,  %[sb],  0  \n"
        "l32i.n    a3,  %[IN],  4  \n"
        "s32i.n    a3,  %[sb],  4  \n"
        "l32i.n    a3,  %[IN],  8  \n"
        "s32i.n    a3,  %[sb],  8  \n"
        "l32i.n    a3,  %[IN], 12  \n"
        "s32i.n    a3,  %[sb], 12  \n"
        "l32i.n    a3,  %[IN], 16  \n"
        "s32i.n    a3,  %[sb], 16  \n"
        "l32i.n    a3,  %[IN], 20  \n"
        "s32i.n    a3,  %[sb], 20  \n"
        "l32i.n    a3,  %[IN], 24  \n"
        "s32i.n    a3,  %[sb], 24  \n"
        "l32i.n    a3,  %[IN], 28  \n"
        "s32i.n    a3,  %[sb], 28  \n"
        "l32i.n    a3,  %[IN], 32  \n"
        "s32i.n    a3,  %[sb], 32  \n"
        "l32i.n    a3,  %[IN], 36  \n"
        "s32i.n    a3,  %[sb], 36  \n"
        "l32i.n    a3,  %[IN], 40  \n"
        "s32i.n    a3,  %[sb], 40  \n"
        "l32i.n    a3,  %[IN], 44  \n"
        "s32i.n    a3,  %[sb], 44  \n"
        "l32i.n    a3,  %[IN], 48  \n"
        "s32i.n    a3,  %[sb], 48  \n"
        "l32i.n    a3,  %[IN], 52  \n"
        "s32i.n    a3,  %[sb], 52  \n"
        "l32i.n    a3,  %[IN], 56  \n"
        "s32i.n    a3,  %[sb], 56  \n"
        "l32i.n    a3,  %[IN], 60  \n"
        "s32i.n    a3,  %[sb], 60  \n"

        // ===== START SHA on block 1 =====
        "movi.n  a3, 1             \n"
        "s32i.n  a3, a5, 0         \n"    // SHA_START
        "memw                      \n"

        // ===== PIPELINE: Prepare block 2 while SHA processes block 1 =====
        // Load words 16-18 (merkle_tail, timestamp, nbits)
        "l32i    a3,  %[IN], 64    \n"
        "s32i.n  a3,  %[sb],  0    \n"
        "l32i    a3,  %[IN], 68    \n"
        "s32i.n  a3,  %[sb],  4    \n"
        "l32i    a3,  %[IN], 72    \n"
        "s32i.n  a3,  %[sb],  8    \n"

        // Store nonce
        "s32i.n  a2,  %[sb], 12    \n"

        // Store padding
        "s32i.n  %[pad2], %[sb], 16 \n"  // 0x80000000

        // ===== UNROLLED ZEROS (words 5-14) - saves loop overhead =====
        "s32i.n  a8,  %[sb], 20    \n"   // word 5
        "s32i.n  a8,  %[sb], 24    \n"   // word 6
        "s32i.n  a8,  %[sb], 28    \n"   // word 7
        "s32i.n  a8,  %[sb], 32    \n"   // word 8
        "s32i.n  a8,  %[sb], 36    \n"   // word 9
        "s32i.n  a8,  %[sb], 40    \n"   // word 10
        "s32i.n  a8,  %[sb], 44    \n"   // word 11
        "s32i.n  a8,  %[sb], 48    \n"   // word 12
        "s32i.n  a8,  %[sb], 52    \n"   // word 13
        "s32i.n  a8,  %[sb], 56    \n"   // word 14

        // Store length
        "s32i.n  %[len1], %[sb], 60 \n"  // 640 bits

        // ===== WAIT for block 1 =====
    "v2_wait1: \n"
        "l32i.n  a3, a5, 12       \n"    // SHA_BUSY
        "bnez.n  a3, v2_wait1     \n"

        // ===== CONTINUE SHA with block 2 =====
        "movi.n  a3, 1            \n"
        "s32i.n  a3, a5, 4        \n"    // SHA_CONTINUE
        "memw                     \n"

        // ===== WAIT for block 2 =====
    "v2_wait2: \n"
        "l32i.n  a4, a5, 12       \n"
        "bnez.n  a4, v2_wait2     \n"

        // ===== LOAD intermediate result =====
        "movi.n  a4, 1            \n"
        "s32i.n  a4, a5, 8        \n"    // SHA_LOAD
        "memw                     \n"

        // Pipeline: increment nonce while waiting
        "addi.n  a2, a2, 1        \n"

        // ===== WAIT for load =====
    "v2_wait3: \n"
        "l32i.n  a4, a5, 12       \n"
        "bnez.n  a4, v2_wait3     \n"

        // ===== DOUBLE-HASH padding =====
        // Result is in SHA_TEXT[0-7], add padding at [8] and length at [15]
        "s32i.n  %[pad2], %[sb], 32 \n"  // 0x80000000 at word 8
        "s32i.n  %[len2], %[sb], 60 \n"  // 256 bits at word 15
        // Words 9-14 are already zero from block 2 phase!

        // ===== START double hash =====
        "movi.n  a4, 1            \n"
        "s32i.n  a4, a5, 0        \n"    // SHA_START
        "memw                     \n"

        // ===== UPDATE hash counter (while SHA processes) =====
        "l32i.n  a3, %[ih], 0     \n"
        "addi.n  a3, a3, 1        \n"
        "s32i.n  a3, %[ih], 0     \n"
        "bnez.n  a3, v2_no_carry  \n"
        "l32i.n  a4, %[ih], 4     \n"
        "addi.n  a4, a4, 1        \n"
        "s32i.n  a4, %[ih], 4     \n"
    "v2_no_carry: \n"

        // ===== WAIT for double hash =====
    "v2_wait4: \n"
        "l32i.n  a4, a5, 12       \n"
        "bnez.n  a4, v2_wait4     \n"

        // ===== LOAD final result =====
        "movi.n  a3, 1            \n"
        "s32i.n  a3, a5, 8        \n"    // SHA_LOAD
        "memw                     \n"

        // ===== WAIT for load =====
    "v2_wait5: \n"
        "l32i.n  a4, a5, 12       \n"
        "bnez.n  a4, v2_wait5     \n"

        // ===== CHECK mining flag =====
        "l8ui   a3, %[flag], 0    \n"
        "beqz.n a3, v2_end        \n"

        // ===== EARLY REJECT: Check H0 upper 16 bits =====
        "l16ui  a3, %[sb], 28     \n"    // Upper 16 bits of H0
        "beqz.n a3, v2_end        \n"    // Exit if potential share

        // Continue mining
        "j v2_start               \n"

    "v2_end: \n"
        "s32i.n a2, %[nonce], 0   \n"

        :
        : [sb] "r"(sha_base),
          [IN] "r"(header_swapped),
          [ih] "r"(hash_count_ptr),
          [nonce] "r"(nonce_ptr),
          [flag] "r"(mining_flag),
          [pad2] "r"(shaPad),
          [len2] "r"(secondShaBitLen),
          [len1] "r"(firstShaBitLen)
        : "a2", "a3", "a4", "a5", "a8", "memory"
    );

    return *mining_flag;
}

#endif // CONFIG_IDF_TARGET_ESP32

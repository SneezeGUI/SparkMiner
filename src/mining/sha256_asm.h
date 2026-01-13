#ifndef SHA256_ASM_H
#define SHA256_ASM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Only available on standard ESP32 (not S3/C3)
#if defined(CONFIG_IDF_TARGET_ESP32)

/**
 * Pipelined mining loop using optimized Xtensa assembly.
 *
 * @SparkMiner\.pio\libdeps\esp32-s3-mini\WiFiManager\examples\ParamsChildClass\ParamsChildClass.ino sha_base       SHA_TEXT_BASE register address
 * @SparkMiner\.pio\libdeps\esp32-s3-mini\WiFiManager\examples\ParamsChildClass\ParamsChildClass.ino header_swapped Pre-byteswapped 80-byte header
 * @SparkMiner\.pio\libdeps\esp32-s3-mini\WiFiManager\examples\ParamsChildClass\ParamsChildClass.ino nonce_ptr      Pointer to nonce (updated)
 * @SparkMiner\.pio\libdeps\esp32-s3-mini\WiFiManager\examples\ParamsChildClass\ParamsChildClass.ino hash_count_ptr Pointer to hash counter
 * @SparkMiner\.pio\libdeps\esp32-s3-mini\WiFiManager\examples\ParamsChildClass\ParamsChildClass.ino mining_flag    Pointer to mining active flag
 *
 * @return true if potential share found (16-bit early reject passed)
 */
bool sha256_pipelined_mine(
    volatile uint32_t *sha_base,
    const uint32_t *header_swapped,
    uint32_t *nonce_ptr,
    volatile uint64_t *hash_count_ptr,
    volatile bool *mining_flag
);

/**
 * Optimized pipelined mining v2 with unrolled zeros.
 * Saves ~20 cycles per hash by eliminating loop overhead.
 */
bool sha256_pipelined_mine_v2(
    volatile uint32_t *sha_base,
    const uint32_t *header_swapped,
    uint32_t *nonce_ptr,
    volatile uint64_t *hash_count_ptr,
    volatile bool *mining_flag
);

/**
 * Pipelined mining v3 - equivalent to v2.
 * Register caching was attempted but not achievable due to Xtensa constraints.
 * The SHA hardware is the bottleneck (~715 KH/s on CYD), not CPU.
 */
bool sha256_pipelined_mine_v3(
    volatile uint32_t *sha_base,
    const uint32_t *header_swapped,
    uint32_t *nonce_ptr,
    volatile uint64_t *hash_count_ptr,
    volatile bool *mining_flag
);

/**
 * Compute midstate for v4 mining.
 * Call ONCE per job to pre-compute hash state after first 64 bytes.
 *
 * @param midstate_out   Output: 8 x 32-bit state values
 * @param header_swapped Pre-byteswapped 80-byte header
 */
void sha256_compute_midstate_v4(
    uint32_t *midstate_out,
    const uint32_t *header_swapped
);

/**
 * Pipelined mining v4 with MIDSTATE INJECTION.
 * Instead of reloading Block 1 (64 bytes) every nonce, we:
 *   - Restore pre-computed midstate (8 words) via SHA_LOAD
 *   - Process only Block 2 (tail + nonce + padding)
 *
 * Expected improvement: ~30-40% vs v3 (matches NMMiner approach)
 *
 * @param sha_base       SHA_TEXT_BASE register address
 * @param midstate       Pre-computed midstate from sha256_compute_midstate_v4()
 * @param tail_swapped   Last 12 bytes (merkle_tail, time, nbits) - 3 words, swapped
 * @param nonce_ptr      Pointer to nonce (updated)
 * @param hash_count_ptr Pointer to hash counter
 * @param mining_flag    Pointer to mining active flag
 */
bool sha256_pipelined_mine_v4(
    volatile uint32_t *sha_base,
    const uint32_t *midstate,
    const uint32_t *tail_swapped,
    uint32_t *nonce_ptr,
    volatile uint64_t *hash_count_ptr,
    volatile bool *mining_flag
);

#endif // CONFIG_IDF_TARGET_ESP32

#ifdef __cplusplus
}
#endif

#endif // SHA256_ASM_H

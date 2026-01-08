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

#endif // CONFIG_IDF_TARGET_ESP32

#ifdef __cplusplus
}
#endif

#endif // SHA256_ASM_H

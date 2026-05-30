/*
 * ESP32-S3 SHA-256D Mining Header
 */

#ifndef SHA256_S3_MINING_H
#define SHA256_S3_MINING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mining loop with hardware SHA on ESP32-S3
 * 
 * @param midstate 8 SHA-256 midstate words from the first 64 header bytes
 * @param tail_template 3 words from header bytes 64..75: merkle tail, ntime, nbits
 * @param nonce_ptr pointer to current nonce (updated in place)
 * @param hash_count pointer to hash counter (incremented)
 * @param mining_flag stop flag
 * @param result_hash buffer for result hash (32 bytes)
 * @return true if candidate found (16-bit match)
 */
bool sha256_s3_mine(const uint32_t *midstate,
                    const uint32_t *tail_template,
                    uint32_t *nonce_ptr,
                    volatile uint64_t *hash_count,
                    volatile bool *mining_flag,
                    uint8_t *result_hash);

#ifdef __cplusplus
}
#endif

#endif // SHA256_S3_MINING_H

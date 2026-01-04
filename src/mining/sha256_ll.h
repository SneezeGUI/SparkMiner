/*
 * SparkMiner - Low-Level Hardware SHA-256 Implementation
 *
 * Ported from NerdMiner V2 by Bitmaker (GPL v3)
 * Originally from Blockstream Jade
 *
 * Direct register access to ESP32 SHA peripheral for maximum mining performance.
 * Supports both standard ESP32 (CYD) and ESP32-S3/C3.
 */

#ifndef SHA256_LL_H_
#define SHA256_LL_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Low-Level SHA Functions - Direct Register Access
// =============================================================================

/**
 * Initialize hardware SHA peripheral for mining.
 * Call once at startup.
 */
void sha256_ll_init(void);

/**
 * Acquire hardware SHA peripheral lock.
 * Call before entering mining loop.
 */
void sha256_ll_acquire(void);

/**
 * Release hardware SHA peripheral lock.
 * Call when exiting mining loop or yielding.
 */
void sha256_ll_release(void);

/**
 * Wait for SHA peripheral to finish current operation.
 */
void IRAM_ATTR sha256_ll_wait_idle(void);

/**
 * Compute midstate from first 64 bytes of block header.
 * Uses hardware SHA to compute initial hash state.
 *
 * @param midstate Output: 8 x 32-bit midstate values
 * @param header   Input: First 64 bytes of block header
 */
void IRAM_ATTR sha256_ll_midstate(uint32_t *midstate, const uint8_t *header);

/**
 * Perform double SHA-256 from midstate with early 16-bit reject.
 * This is the hot path - called once per nonce.
 *
 * @param midstate   Input: Pre-computed midstate from sha256_ll_midstate()
 * @param tail       Input: Last 16 bytes of block header (before nonce)
 * @param nonce      Input: 32-bit nonce to try
 * @param hash_out   Output: 32-byte double SHA-256 result (only valid if returns true)
 * @return true if hash passes 16-bit early reject (potential valid share)
 */
bool IRAM_ATTR sha256_ll_double_hash(const uint32_t *midstate, const uint8_t *tail,
                                      uint32_t nonce, uint8_t *hash_out);

/**
 * Perform full double SHA-256 without midstate (like NerdMiner).
 * Hashes both 64-byte blocks every call - slower but more reliable.
 *
 * @param header     Input: Full 80-byte block header (pre-swapped for big-endian SHA)
 * @param nonce      Input: 32-bit nonce to try
 * @param hash_out   Output: 32-byte double SHA-256 result (only valid if returns true)
 * @return true if hash passes 16-bit early reject (potential valid share)
 */
bool IRAM_ATTR sha256_ll_double_hash_full(const uint8_t *header, uint32_t nonce, uint8_t *hash_out);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_LL_H_ */

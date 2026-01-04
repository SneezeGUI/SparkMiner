/*
 * SparkMiner - Hardware SHA-256 Implementation
 * Uses ESP32 hardware SHA peripheral for maximum mining performance
 *
 * Based on NerdMiner V2 by Bitmaker (GPL v3)
 * Originally from Blockstream Jade
 *
 * Key optimizations:
 * - Direct register access to SHA peripheral
 * - Hardware computes all 64 compression rounds
 * - Early 16-bit reject on final hash
 */

#ifndef SHA256_HW_H
#define SHA256_HW_H

#include "sha256_types.h"
#include "sha256_ll.h"

// Context for hardware SHA operations
typedef struct {
    uint32_t digest[8];     // Current hash state / midstate
    uint8_t buffer[80];     // Full block header (80 bytes)
} sha256_hw_ctx_t;

// Pre-computed "bake" for a job (unused with hardware SHA, kept for API compat)
typedef struct {
    uint32_t data[15];      // Baked constants (unused)
} sha256_bake_t;

/**
 * Compute midstate from first 64 bytes of block header
 * Hardware-accelerated for ESP32-S3
 *
 * @param digest Output: 8 x 32-bit midstate values
 * @param dataIn Input: First 64 bytes of block header
 */
void IRAM_ATTR sha256_hw_midstate(uint32_t *digest, const uint8_t *dataIn);

/**
 * Pre-compute constants for a job ("baking")
 * Call once after midstate, before mining loop
 *
 * @param digest Input: midstate from sha256_hw_midstate()
 * @param dataIn Input: Bytes 64-76 of block header (timestamp, nbits, first nonce byte)
 * @param bake Output: Pre-computed constants for sha256_hw_hash_baked()
 */
void IRAM_ATTR sha256_hw_bake(const uint32_t *digest, const uint8_t *dataIn, sha256_bake_t *bake);

/**
 * Complete double SHA-256 using pre-baked constants
 * This is the hot path - called millions of times per second
 *
 * @param digest Input: midstate from sha256_hw_midstate()
 * @param dataIn Input: Last 16 bytes of block header (only nonce changes)
 * @param bake Input: Pre-computed constants from sha256_hw_bake()
 * @param hashOut Output: 32-byte double SHA-256 result
 * @return true if hash passes early 16-bit check (worth full verification)
 */
bool IRAM_ATTR sha256_hw_hash_baked(const uint32_t *digest, const uint8_t *dataIn,
                                    const sha256_bake_t *bake, uint8_t *hashOut);

/**
 * Standard double SHA-256 without baking (for verification, etc.)
 *
 * @param ctx Input: context with midstate in digest
 * @param dataIn Input: Last 16 bytes of block header
 * @param hashOut Output: 32-byte double SHA-256 result
 * @return true if hash passes early 16-bit check
 */
bool IRAM_ATTR sha256_hw_hash(sha256_hw_ctx_t *ctx, const uint8_t *dataIn, uint8_t *hashOut);

/**
 * Byte-reverse an array of 32-bit words
 * Used for endianness conversion
 */
void sha256_hw_byte_reverse(uint32_t *out, const uint32_t *in, uint32_t byteCount);

/**
 * Initialize hardware SHA peripheral
 * Call once at startup
 */
void sha256_hw_init(void);

/**
 * Single SHA-256 hash of arbitrary length data
 * Used for merkle root and coinbase hash calculations
 *
 * @param result Output: 32-byte hash result
 * @param data Input: Data to hash
 * @param len Input: Length of data in bytes
 */
void sha256(sha256_hash_t *result, const uint8_t *data, size_t len);

#endif // SHA256_HW_H

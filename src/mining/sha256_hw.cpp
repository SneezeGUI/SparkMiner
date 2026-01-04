/*
 * SparkMiner - Hardware SHA-256 Implementation
 * Uses ESP32 hardware SHA peripheral for maximum mining performance
 *
 * Ported from NerdMiner V2 by Bitmaker (GPL v3)
 * Originally from Blockstream Jade
 *
 * This implementation uses direct register access to the ESP32 SHA
 * hardware peripheral, achieving 300-500 KH/s on standard ESP32.
 */

#include <Arduino.h>
#include <string.h>
#include <mbedtls/sha256.h>
#include "sha256_hw.h"
#include "sha256_ll.h"
#include "sha256_types.h"

// =============================================================================
// Utility Functions
// =============================================================================

static uint32_t rotlFixed(uint32_t x, uint32_t y) {
    return (x << y) | (x >> (sizeof(y) * 8 - y));
}

static uint32_t byteReverseWord32(uint32_t value) {
    value = ((value & 0xFF00FF00) >> 8) | ((value & 0x00FF00FF) << 8);
    return rotlFixed(value, 16U);
}

void sha256_hw_byte_reverse(uint32_t *out, const uint32_t *in, uint32_t byteCount) {
    uint32_t count = byteCount / sizeof(uint32_t);
    for (uint32_t i = 0; i < count; i++) {
        out[i] = byteReverseWord32(in[i]);
    }
}

// =============================================================================
// Hardware SHA-256 Functions
// =============================================================================

/**
 * Initialize hardware SHA peripheral
 */
void sha256_hw_init(void) {
    sha256_ll_init();
    Serial.println("[SHA-HW] Hardware SHA-256 initialized");
}

/**
 * Compute midstate from first 64 bytes of block header
 * This uses the hardware SHA peripheral
 */
void IRAM_ATTR sha256_hw_midstate(uint32_t *digest, const uint8_t *dataIn) {
    // Use the low-level hardware function to compute midstate
    sha256_ll_midstate(digest, dataIn);
}

/**
 * Pre-compute constants for a job ("baking")
 *
 * With hardware SHA, we don't need the complex baking optimization
 * since the hardware computes all 64 rounds in ~80 clock cycles.
 * This function just stores the tail bytes for the mining loop.
 */
void IRAM_ATTR sha256_hw_bake(const uint32_t *digest, const uint8_t *dataIn, sha256_bake_t *bake) {
    // Store tail data (bytes 64-79 of header, excluding nonce position)
    // This is used by the mining loop to update only the nonce
    memcpy(bake->data, dataIn, 12);  // First 12 bytes of tail (timestamp, nbits, partial nonce)
}

/**
 * Complete double SHA-256 using hardware peripheral
 * This is the hot path - called millions of times per second
 *
 * Unlike the software "baking" approach which pre-computes SHA rounds,
 * the hardware SHA computes all 64 rounds in hardware, so we just
 * need to call the low-level double hash function.
 */
bool IRAM_ATTR sha256_hw_hash_baked(const uint32_t *digest, const uint8_t *dataIn,
                                    const sha256_bake_t *bake, uint8_t *hashOut) {
    // Extract nonce from dataIn (bytes 12-15 of the tail)
    uint32_t nonce = *(uint32_t *)(dataIn + 12);

    // Call low-level hardware double SHA-256
    // digest = midstate from first 64 bytes
    // dataIn = last 16 bytes of header (tail)
    // nonce = 32-bit nonce value
    return sha256_ll_double_hash(digest, dataIn, nonce, hashOut);
}

/**
 * Standard double SHA-256 without baking (for verification, etc.)
 */
bool IRAM_ATTR sha256_hw_hash(sha256_hw_ctx_t *ctx, const uint8_t *dataIn, uint8_t *hashOut) {
    // Use the last 16 bytes as tail, extract nonce
    uint32_t nonce = *(uint32_t *)(dataIn + 12);

    return sha256_ll_double_hash(ctx->digest, dataIn, nonce, hashOut);
}

// =============================================================================
// Single SHA-256 (for merkle root, coinbase hash, etc.)
// =============================================================================

/**
 * Single SHA-256 hash of arbitrary length data
 * Used for merkle root and coinbase hash calculations (not performance-critical)
 *
 * @param result Output: 32-byte hash result
 * @param data Input: Data to hash
 * @param len Input: Length of data in bytes
 */
void sha256(sha256_hash_t *result, const uint8_t *data, size_t len) {
    mbedtls_sha256(data, len, result->bytes, 0);  // 0 = SHA-256 (not SHA-224)
}

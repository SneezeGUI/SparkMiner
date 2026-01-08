#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3

// Initialize S3 SHA hardware
void sha256_s3_init(void);

// Compute midstate from first 64 bytes of header (call once per job)
void sha256_s3_midstate(uint32_t *midstate_out, const uint8_t *header_64bytes);

// Mine with midstate optimization
// Returns true if potential share found (caller must verify)
// header_tail = last 16 bytes of 80-byte header (timestamp, bits, nonce placeholder, padding)
// nonce_ptr = pointer to current nonce (updated by function)
// hash_count = pointer to hash counter (incremented)
// mining_flag = pointer to flag (stop when false)
bool sha256_s3_mine(
    const uint32_t *midstate,      // Pre-computed midstate (8 words)
    const uint8_t *header_tail,    // Last 16 bytes of header
    uint32_t *nonce_ptr,           // Current nonce (modified)
    volatile uint64_t *hash_count, // Hash counter
    volatile bool *mining_flag     // Stop flag
);

// Full double-hash for share verification
bool sha256_s3_verify(
    const uint32_t *midstate,
    const uint8_t *header_tail,
    uint32_t nonce,
    uint8_t *hash_out              // 32-byte output
);

#endif

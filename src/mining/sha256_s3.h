#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3

typedef struct {
    uint32_t restoredMidstateWords[8];
    uint32_t shaHAfterRestoreWords[8];
    uint32_t firstDigestRawWords[8];
    uint8_t firstDigestBytes[32];
    uint8_t finalDigestBeBytes[32];
    uint8_t secondInputBlockBytes[64];
    uint32_t secondDigestRawWords[8];
    uint8_t finalDigestBytes[32];
} sha256_s3_verify_trace_t;

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

// Isolated deterministic single-shot verify helper with detailed internal trace.
// This function does not use the mining loop and is intended for boot-time testing.
bool sha256_s3_verify_trace(
    const uint32_t *midstate,
    const uint8_t *header_tail,
    uint32_t nonce,
    uint8_t *hash_out,
    sha256_s3_verify_trace_t *trace
);

// Deterministic restore/tail diagnostic matrix:
// runs one first-pass compression (midstate + tail block) for restore modes A/B/C/D
// and tail modes T0/T1/T2, then compares against expected first digest.
bool sha256_s3_test_restore_mapping(
    const uint32_t *midstate_words,
    const uint8_t *canonical_header_tail16,
    const uint32_t *header_tail_words_swapped,
    const uint8_t *expected_first_digest
);

// One-block IV diagnostic for SHA-256 compression correctness.
// Uses a canonical pre-padded 64-byte block ("abc" block) and reports:
// 1) HW from IV (START)
// 2) SW one-block compression from IV
// 3) HW with explicit IV restore + CONTINUE
bool sha256_s3_test_one_block_from_iv(void);

// Deeper diagnostics for second-SHA-only acceleration path.
// Runs software + hardware comparisons for a known first-digest vector and
// reports where representation or state-resume mismatches occur.
bool sha256_s3_test_second_sha_paths(void);

// Hardware second-SHA only path.
// Input and output are canonical SHA-256 digest bytes (big-endian words H0..H7).
bool sha256_s3_second_sha_from_first_be(const uint8_t first_digest_be[32], uint8_t out_digest_be[32]);

#endif

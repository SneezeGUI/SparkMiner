/*
 * SparkMiner - BitsyMiner SHA-256 Implementation
 * Ported from BitsyMiner by Justin Williams (GPL v3)
 *
 * Optimized software SHA-256 with:
 * - Midstate caching (75% less work per hash)
 * - Early 16-bit reject optimization
 * - Macro-unrolled rounds for performance
 */
#ifndef MINER_SHA256_H
#define MINER_SHA256_H

#include <stdint.h>
#include <stddef.h>
#include "sha256_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Standard SHA-256 hash
 * Output is byte-swapped for little-endian comparison
 *
 * @param ctx Output hash result
 * @param msg Input message
 * @param len Message length in bytes
 */
void miner_sha256(sha256_hash_t *ctx, uint8_t *msg, size_t len);

/**
 * Compute SHA-256 midstate from first 64 bytes of block header
 * Call once per job, reuse for all nonce iterations
 *
 * @param ctx Output midstate (8 x 32-bit words)
 * @param hb Block header (80 bytes)
 */
void miner_sha256_midstate(sha256_hash_t *ctx, block_header_t *hb);

/**
 * Complete double SHA-256 using pre-computed midstate
 * Hashes tail (last 16 bytes + nonce) and performs double hash
 * Includes early 16-bit reject optimization
 *
 * @param midpoint Pre-computed midstate from miner_sha256_midstate()
 * @param ctx Output final hash result
 * @param hb Block header with current nonce
 * @return true if hash passes 16-bit check (potential share), false otherwise
 */
bool miner_sha256_header(sha256_hash_t *midpoint, sha256_hash_t *ctx, block_header_t *hb);

#ifdef __cplusplus
}
#endif

#endif // MINER_SHA256_H

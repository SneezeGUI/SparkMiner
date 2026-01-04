/*
 * Pure Software SHA-256 Implementation
 * No ESP32 hardware acceleration - for use on Core 0 to avoid contention
 */

#ifndef SHA256_SOFT_H
#define SHA256_SOFT_H

#include <stdint.h>
#include <stddef.h>

// Software double SHA-256 for mining
// Returns true if hash has 16+ leading zero bits (potential share)
bool sha256_soft_double(const uint8_t *data, size_t len, uint8_t *hash_out);

#endif // SHA256_SOFT_H

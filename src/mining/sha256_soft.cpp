/*
 * Pure Software SHA-256 Implementation
 * Based on FIPS 180-4 specification
 * No ESP32 hardware acceleration - for use on Core 0 to avoid contention
 */

#include "sha256_soft.h"
#include <string.h>
#include <esp_attr.h>

// SHA-256 initial hash values
static const uint32_t H_INIT[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

// SHA-256 round constants
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// Rotate right
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

// SHA-256 functions
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)       (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x)       (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x)      (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x)      (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

// Process a single 64-byte block
static void IRAM_ATTR sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t T1, T2;
    int i;

    // Prepare message schedule
    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (i = 16; i < 64; i++) {
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];
    }

    // Initialize working variables
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    // 64 rounds
    for (i = 0; i < 64; i++) {
        T1 = h + EP1(e) + CH(e, f, g) + K[i] + W[i];
        T2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    // Update state
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// Single SHA-256 hash of exactly 80 bytes (block header)
static void IRAM_ATTR sha256_80(const uint8_t data[80], uint8_t hash[32]) {
    uint32_t state[8];
    uint8_t block[64];
    int i;

    // Initialize state
    memcpy(state, H_INIT, sizeof(H_INIT));

    // Process first 64 bytes
    sha256_transform(state, data);

    // Prepare final block: remaining 16 bytes + padding + length
    memset(block, 0, 64);
    memcpy(block, data + 64, 16);        // Last 16 bytes of header
    block[16] = 0x80;                     // Padding bit
    // Length = 80 bytes = 640 bits = 0x280 in big-endian at end
    block[62] = 0x02;
    block[63] = 0x80;

    sha256_transform(state, block);

    // Output hash (big-endian)
    for (i = 0; i < 8; i++) {
        hash[i*4]     = (state[i] >> 24) & 0xff;
        hash[i*4 + 1] = (state[i] >> 16) & 0xff;
        hash[i*4 + 2] = (state[i] >> 8) & 0xff;
        hash[i*4 + 3] = state[i] & 0xff;
    }
}

// Single SHA-256 hash of exactly 32 bytes
static void IRAM_ATTR sha256_32(const uint8_t data[32], uint8_t hash[32]) {
    uint32_t state[8];
    uint8_t block[64];
    int i;

    // Initialize state
    memcpy(state, H_INIT, sizeof(H_INIT));

    // Prepare single block: 32 bytes data + padding + length
    memcpy(block, data, 32);
    block[32] = 0x80;                     // Padding bit
    memset(block + 33, 0, 64 - 33);
    // Length = 32 bytes = 256 bits = 0x100 in big-endian at end
    block[62] = 0x01;
    block[63] = 0x00;

    sha256_transform(state, block);

    // Output hash (big-endian)
    for (i = 0; i < 8; i++) {
        hash[i*4]     = (state[i] >> 24) & 0xff;
        hash[i*4 + 1] = (state[i] >> 16) & 0xff;
        hash[i*4 + 2] = (state[i] >> 8) & 0xff;
        hash[i*4 + 3] = state[i] & 0xff;
    }
}

// Double SHA-256 for mining: SHA256(SHA256(header))
// Returns true if hash has 16+ leading zero bits
bool IRAM_ATTR sha256_soft_double(const uint8_t *data, size_t len, uint8_t *hash_out) {
    uint8_t first_hash[32];

    // First SHA-256 of 80-byte header
    sha256_80(data, first_hash);

    // Second SHA-256 of first hash
    sha256_32(first_hash, hash_out);

    // Check for 16+ leading zero bits (first 2 bytes)
    return (hash_out[0] == 0 && hash_out[1] == 0);
}

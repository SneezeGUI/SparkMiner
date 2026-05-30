/*
 * ESP32-S3 SHA-256D Mining - Hardware Accelerated
 *
 * SHA_TEXT registers are little-endian CPU words. Write native uint32_t values
 * from the serialized Bitcoin header so the SHA engine sees the same byte
 * stream as the software implementation.
 */

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include "soc/hwcrypto_reg.h"
#include "sha256_s3_mining.h"

#define SHA2_256_MODE       2

// SHA256 initial hash values
static const uint32_t SHA256_IV[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

static inline void write_reg(uint32_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t read_reg(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline bool sha_wait_idle(void) {
    uint32_t timeout = 20000;
    while (read_reg(SHA_BUSY_REG) & 0x1) {
        if (--timeout == 0) return false;
    }
    return true;
}

/**
 * Process single SHA-256 block with hardware acceleration
 * 
 * @param midstate SHA-256 midstate words from bytes 0..63 of the header
 * @param tail 3 words from header bytes 64..75: merkle tail, ntime, nbits
 * @param digest output 32 bytes in normal SHA digest byte order
 */
static inline bool sha256_s3_process_raw(const uint32_t *midstate,
                                          const uint32_t *tail,
                                          uint32_t nonce,
                                          uint32_t *raw_digest,
                                          bool full_digest) {
    int i;
    
    // ===== HASH 1: Midstate + Tail =====
    for (i = 0; i < 8; i++) {
        write_reg(SHA_H_BASE + i * 4, midstate[i]);
    }
    
    // Header bytes 64..79 are: merkle[28..31], ntime, nbits, nonce.
    for (i = 0; i < 3; i++) {
        write_reg(SHA_TEXT_BASE + i * 4, tail[i]);
    }
    write_reg(SHA_TEXT_BASE + 0x0C, nonce);
    
    // Padding for 80-byte message
    write_reg(SHA_TEXT_BASE + 0x10, 0x00000080);
    write_reg(SHA_TEXT_BASE + 0x14, 0);
    write_reg(SHA_TEXT_BASE + 0x18, 0);
    write_reg(SHA_TEXT_BASE + 0x1C, 0);
    write_reg(SHA_TEXT_BASE + 0x20, 0);
    write_reg(SHA_TEXT_BASE + 0x3C, 0x80020000);  // 640 bits: 00 00 02 80
    
    write_reg(SHA_MODE_REG, SHA2_256_MODE);
    write_reg(SHA_CONTINUE_REG, 1);
    
    if (!sha_wait_idle()) return false;
    
    // ===== HASH 2: Double SHA =====
    // Preserve raw digest bytes for the second SHA.
    uint32_t h[8];
    for (i = 0; i < 8; i++) {
        h[i] = read_reg(SHA_H_BASE + i * 4);
    }
    
    // Write hash1 to SHA_TEXT (raw copy, no swap)
    for (i = 0; i < 8; i++) {
        write_reg(SHA_TEXT_BASE + i * 4, h[i]);
    }
    
    // Padding for 32-byte message
    write_reg(SHA_TEXT_BASE + 0x20, 0x00000080);
    write_reg(SHA_TEXT_BASE + 0x3C, 0x00010000);  // 256 bits: 00 00 01 00
    
    write_reg(SHA_MODE_REG, SHA2_256_MODE);
    write_reg(SHA_START_REG, 1);
    
    if (!sha_wait_idle()) return false;

    if (full_digest) {
        for (i = 0; i < 8; i++) {
            raw_digest[i] = read_reg(SHA_H_BASE + i * 4);
        }
    } else {
        raw_digest[7] = read_reg(SHA_H_BASE + 7 * 4);
    }

    return true;
}

static inline void format_raw_digest(const uint32_t *raw_digest, uint8_t *digest) {
    int i;
    
    // ESP32-S3 SHA_H words match the canonical SHA byte stream when read in
    // native little-endian byte order. The mining midstate is byte-swapped
    // before loading SHA_H; do not reverse or swap the final digest here.
    for (i = 0; i < 8; i++) {
        uint32_t v = raw_digest[i];
        digest[i*4+0] = v & 0xFF;
        digest[i*4+1] = (v >> 8) & 0xFF;
        digest[i*4+2] = (v >> 16) & 0xFF;
        digest[i*4+3] = (v >> 24) & 0xFF;
    }
}

/**
 * Mining loop - processes nonces sequentially
 * Returns true if candidate found (for external verification)
 */
bool sha256_s3_mine(const uint32_t *midstate,
                    const uint32_t *tail_template,
                    uint32_t *nonce_ptr,
                    volatile uint64_t *hash_count,
                    volatile bool *mining_flag,
                    uint8_t *result_hash) {
    
    uint32_t nonce = *nonce_ptr;
    uint32_t hashes_done = 0;

    // The first hash needs SHA_TEXT[9..14] zero. The second hash also needs
    // SHA_TEXT[9..14] zero. Keep those persistent and only rewrite words that
    // are changed by the previous second-hash copy.
    write_reg(SHA_TEXT_BASE + 0x24, 0);
    write_reg(SHA_TEXT_BASE + 0x28, 0);
    write_reg(SHA_TEXT_BASE + 0x2C, 0);
    write_reg(SHA_TEXT_BASE + 0x30, 0);
    write_reg(SHA_TEXT_BASE + 0x34, 0);
    write_reg(SHA_TEXT_BASE + 0x38, 0);
    
    while (*mining_flag) {
        uint32_t raw_digest[8];
        if (!sha256_s3_process_raw(midstate, tail_template, nonce, raw_digest, false)) {
            *nonce_ptr = nonce;
            *hash_count += hashes_done;
            return false;
        }
        
        hashes_done++;
        nonce++;
        
        // Check for candidate (16-bit early reject)
        if ((raw_digest[7] & 0xFFFF0000) == 0) {
            for (int i = 0; i < 7; i++) {
                raw_digest[i] = read_reg(SHA_H_BASE + i * 4);
            }
            format_raw_digest(raw_digest, result_hash);
            *nonce_ptr = nonce;
            *hash_count += hashes_done;
            return true;
        }
        
        // Yield periodically
        if ((nonce & 0xFFFF) == 0) {
            *nonce_ptr = nonce;
            *hash_count += hashes_done;
            return false;
        }
    }
    
    *nonce_ptr = nonce;
    *hash_count += hashes_done;
    return false;
}

#endif // CONFIG_IDF_TARGET_ESP32S3

/*
 * SparkMiner - Low-Level Hardware SHA-256 Implementation
 *
 * Ported from NerdMiner V2 by Bitmaker (GPL v3)
 * Originally from Blockstream Jade
 *
 * Direct register access to ESP32 SHA peripheral for maximum mining performance.
 */

#include <Arduino.h>
#include "sha256_ll.h"

// ESP-IDF SHA peripheral headers
#include <sha/sha_dma.h>
#include <hal/sha_hal.h>
#include <hal/sha_ll.h>

#if defined(CONFIG_IDF_TARGET_ESP32)
#include <sha/sha_parallel_engine.h>
#endif

// =============================================================================
// Platform-specific register definitions
// =============================================================================

#if defined(CONFIG_IDF_TARGET_ESP32)
// Standard ESP32 (Xtensa LX6) - Used by CYD
// Hash output goes to SHA_TEXT_BASE, needs byte swap
#define HASH_OUTPUT_BASE SHA_TEXT_BASE
#define SHA_BUSY_REG_ADDR SHA_256_BUSY_REG
#define NEEDS_BYTE_SWAP 1

#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
// ESP32-S2/S3/C3 - Hash output goes to SHA_H_BASE
#define HASH_OUTPUT_BASE SHA_H_BASE
#define SHA_BUSY_REG_ADDR SHA_BUSY_REG
#define NEEDS_BYTE_SWAP 0

#else
#error "Unsupported ESP32 variant for hardware SHA"
#endif

// =============================================================================
// Hardware Lock Functions
// =============================================================================

void sha256_ll_init(void) {
    Serial.println("[SHA-LL] Hardware SHA-256 initialized");
}

void sha256_ll_acquire(void) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    esp_sha_lock_engine(SHA2_256);
#else
    esp_sha_acquire_hardware();
    REG_WRITE(SHA_MODE_REG, SHA2_256);
#endif
}

void sha256_ll_release(void) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    esp_sha_unlock_engine(SHA2_256);
#else
    esp_sha_release_hardware();
#endif
}

void IRAM_ATTR sha256_ll_wait_idle(void) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    while (DPORT_REG_READ(SHA_256_BUSY_REG)) {}
#else
    while (REG_READ(SHA_BUSY_REG)) {}
#endif
}

// =============================================================================
// Low-Level Register Access Functions
// =============================================================================

#if defined(CONFIG_IDF_TARGET_ESP32)
// Standard ESP32 implementation

static inline void IRAM_ATTR ll_fill_text_block(const uint8_t *data) {
    uint32_t *data_words = (uint32_t *)data;
    uint32_t *reg = (uint32_t *)(SHA_TEXT_BASE);

    reg[0]  = data_words[0];
    reg[1]  = data_words[1];
    reg[2]  = data_words[2];
    reg[3]  = data_words[3];
    reg[4]  = data_words[4];
    reg[5]  = data_words[5];
    reg[6]  = data_words[6];
    reg[7]  = data_words[7];
    reg[8]  = data_words[8];
    reg[9]  = data_words[9];
    reg[10] = data_words[10];
    reg[11] = data_words[11];
    reg[12] = data_words[12];
    reg[13] = data_words[13];
    reg[14] = data_words[14];
    reg[15] = data_words[15];
}

static inline void IRAM_ATTR ll_fill_second_block(const uint8_t *tail, uint32_t nonce) {
    uint32_t *data_words = (uint32_t *)tail;
    uint32_t *reg = (uint32_t *)(SHA_TEXT_BASE);

    // First 12 bytes from tail (timestamp, nbits, nonce high bits)
    reg[0]  = data_words[0];
    reg[1]  = data_words[1];
    reg[2]  = data_words[2];
    // Nonce (Keep Little-Endian for SHA input stream) - but swap for Big Endian register!
    reg[3]  = __builtin_bswap32(nonce);
    // Padding: 0x80 followed by zeros, length = 640 bits (80 bytes)
    reg[4]  = 0x80000000;
    reg[5]  = 0x00000000;
    reg[6]  = 0x00000000;
    reg[7]  = 0x00000000;
    reg[8]  = 0x00000000;
    reg[9]  = 0x00000000;
    reg[10] = 0x00000000;
    reg[11] = 0x00000000;
    reg[12] = 0x00000000;
    reg[13] = 0x00000000;
    reg[14] = 0x00000000;
    reg[15] = 0x00000280;  // 640 bits in big-endian
}

static inline void IRAM_ATTR ll_fill_double_block(void) {
    // Prepare for second SHA (hash of first hash)
    // First 8 words already contain hash output, just add padding
    uint32_t *reg = (uint32_t *)(SHA_TEXT_BASE);

    reg[8]  = 0x80000000;
    reg[9]  = 0x00000000;
    reg[10] = 0x00000000;
    reg[11] = 0x00000000;
    reg[12] = 0x00000000;
    reg[13] = 0x00000000;
    reg[14] = 0x00000000;
    reg[15] = 0x00000100;  // 256 bits in big-endian
}

static inline bool IRAM_ATTR ll_read_digest_if(uint8_t *hash_out) {
    // Early reject: check last word (H0 - MSB) first
    // H0 contains the most significant bits of the hash (Big Endian)
    // For mining difficulty > 1, H0 must be 0
    DPORT_INTERRUPT_DISABLE();
    uint32_t first = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);

    static uint32_t debug_ctr = 0;
    bool debug_log = (debug_ctr++ % 500000 == 0);

    // Check top 16 bits.
    bool valid = (first & 0x0000FFFF) == 0;
    
    if (!valid && !debug_log) {
         DPORT_INTERRUPT_RESTORE();
         return false;
    }

    // Read full hash in Little-Endian word order (MSB at end of array)
    // This matches check_target() which expects MSB at index 31
    uint32_t *out = (uint32_t *)hash_out;
    
    // out[7] gets H0 (MSB of hash) -> hash[28..31]
    // We must swap bytes because SHA_TEXT_BASE is Big-Endian bytes, 
    // read as LE words (scrambled), and check_target expects MSB at hash[31].
    out[7] = __builtin_bswap32(first);
    out[6] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4));
    out[5] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4));
    out[4] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4));
    out[3] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4));
    out[2] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4));
    out[1] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4));
    out[0] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4)); // H7 (LSB)
    
    DPORT_INTERRUPT_RESTORE();

    if (debug_log) {
        Serial.printf("[DEBUG] first=%08x H[31]=%02x H[30]=%02x H[29]=%02x H[28]=%02x\n",
            first, hash_out[31], hash_out[30], hash_out[29], hash_out[28]);
    }
    
    if (!valid) return false;

    return true;
}

#else
// ESP32-S2/S3/C3 implementation

static inline void IRAM_ATTR ll_fill_second_block(const uint8_t *tail, uint32_t nonce) {
    uint32_t *data_words = (uint32_t *)tail;
    uint32_t *reg = (uint32_t *)(SHA_TEXT_BASE);

    REG_WRITE(&reg[0], data_words[0]);
    REG_WRITE(&reg[1], data_words[1]);
    REG_WRITE(&reg[2], data_words[2]);
    REG_WRITE(&reg[3], nonce);
    REG_WRITE(&reg[4], 0x00000080);
    REG_WRITE(&reg[5], 0x00000000);
    REG_WRITE(&reg[6], 0x00000000);
    REG_WRITE(&reg[7], 0x00000000);
    REG_WRITE(&reg[8], 0x00000000);
    REG_WRITE(&reg[9], 0x00000000);
    REG_WRITE(&reg[10], 0x00000000);
    REG_WRITE(&reg[11], 0x00000000);
    REG_WRITE(&reg[12], 0x00000000);
    REG_WRITE(&reg[13], 0x00000000);
    REG_WRITE(&reg[14], 0x00000000);
    REG_WRITE(&reg[15], 0x80020000);  // 640 bits, different byte order
}

static inline void IRAM_ATTR ll_write_digest(const uint32_t *midstate) {
    uint32_t *reg = (uint32_t *)(SHA_H_BASE);

    REG_WRITE(&reg[0], midstate[0]);
    REG_WRITE(&reg[1], midstate[1]);
    REG_WRITE(&reg[2], midstate[2]);
    REG_WRITE(&reg[3], midstate[3]);
    REG_WRITE(&reg[4], midstate[4]);
    REG_WRITE(&reg[5], midstate[5]);
    REG_WRITE(&reg[6], midstate[6]);
    REG_WRITE(&reg[7], midstate[7]);
}

static inline void IRAM_ATTR ll_fill_inter_block(void) {
    // Copy hash result from SHA_H_BASE to SHA_TEXT_BASE, add padding
    uint32_t *reg = (uint32_t *)(SHA_TEXT_BASE);

    DPORT_INTERRUPT_DISABLE();
    REG_WRITE(&reg[0], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4));
    REG_WRITE(&reg[1], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4));
    REG_WRITE(&reg[2], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4));
    REG_WRITE(&reg[3], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4));
    REG_WRITE(&reg[4], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4));
    REG_WRITE(&reg[5], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4));
    REG_WRITE(&reg[6], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4));
    REG_WRITE(&reg[7], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4));
    DPORT_INTERRUPT_RESTORE();

    REG_WRITE(&reg[8], 0x00000080);
    REG_WRITE(&reg[9], 0x00000000);
    REG_WRITE(&reg[10], 0x00000000);
    REG_WRITE(&reg[11], 0x00000000);
    REG_WRITE(&reg[12], 0x00000000);
    REG_WRITE(&reg[13], 0x00000000);
    REG_WRITE(&reg[14], 0x00000000);
    REG_WRITE(&reg[15], 0x00010000);  // 256 bits
}

static inline bool IRAM_ATTR ll_read_digest_if(uint8_t *hash_out) {
    DPORT_INTERRUPT_DISABLE();
    uint32_t last = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);

    // Early 16-bit reject
    if ((uint16_t)(last >> 16) != 0) {
        DPORT_INTERRUPT_RESTORE();
        return false;
    }

    uint32_t *out = (uint32_t *)hash_out;
    out[7] = last;
    out[0] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
    out[1] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
    out[2] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
    out[3] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
    out[4] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
    out[5] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
    out[6] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);
    DPORT_INTERRUPT_RESTORE();

    return true;
}

#endif

// =============================================================================
// Public API
// =============================================================================

void IRAM_ATTR sha256_ll_midstate(uint32_t *midstate, const uint8_t *header) {
    // Compute midstate (SHA-256 of first 64 bytes)
    // We use the HAL function for this since it's a one-time operation per job

#if defined(CONFIG_IDF_TARGET_ESP32)
    ll_fill_text_block(header);
    sha_ll_start_block(SHA2_256);
    sha256_ll_wait_idle();

    // CRITICAL: sha_ll_load copies the hash result FROM internal state TO SHA_TEXT_BASE
    // Without this, SHA_TEXT_BASE still contains the INPUT data, not the hash!
    sha_ll_load(SHA2_256);
    sha256_ll_wait_idle();

    // Now read midstate from SHA_TEXT_BASE
    DPORT_INTERRUPT_DISABLE();
    midstate[0] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4);
    midstate[1] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4);
    midstate[2] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4);
    midstate[3] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4);
    midstate[4] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4);
    midstate[5] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4);
    midstate[6] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4);
    midstate[7] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
    DPORT_INTERRUPT_RESTORE();

#else
    // ESP32-S3/C3: Use HAL to compute initial block
    sha_hal_hash_block(SHA2_256, header, 64/4, true);
    sha256_ll_wait_idle();
    sha_hal_read_digest(SHA2_256, midstate);
#endif
}

// Full double hash without midstate (like NerdMiner)
// header must be 80 bytes, pre-swapped for big-endian SHA
bool IRAM_ATTR sha256_ll_double_hash_full(const uint8_t *header, uint32_t nonce, uint8_t *hash_out) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    // === First SHA-256: Hash of 80-byte header ===

    // Block 1: First 64 bytes
    ll_fill_text_block(header);
    sha_ll_start_block(SHA2_256);
    sha256_ll_wait_idle();

    // Block 2: Last 16 bytes + nonce + padding
    ll_fill_second_block(header + 64, nonce);
    sha_ll_continue_block(SHA2_256);
    sha256_ll_wait_idle();
    sha_ll_load(SHA2_256);
    sha256_ll_wait_idle();

    // === Second SHA-256: Hash of first hash ===
    ll_fill_double_block();
    sha_ll_start_block(SHA2_256);
    sha256_ll_wait_idle();
    sha_ll_load(SHA2_256);

    return ll_read_digest_if(hash_out);
#else
    // For other ESP32 variants, use the existing implementation
    return false;
#endif
}

bool IRAM_ATTR sha256_ll_double_hash(const uint32_t *midstate, const uint8_t *tail,
                                      uint32_t nonce, uint8_t *hash_out) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    // === First SHA-256: Complete header hash ===
    // 1. Restore midstate (hash of first 64 bytes) into SHA hardware
    // On ESP32, we write the state to the text buffer and use sha_ll_load()
    uint32_t *reg = (uint32_t *)(SHA_TEXT_BASE);
    reg[0] = midstate[0];
    reg[1] = midstate[1];
    reg[2] = midstate[2];
    reg[3] = midstate[3];
    reg[4] = midstate[4];
    reg[5] = midstate[5];
    reg[6] = midstate[6];
    reg[7] = midstate[7];

    sha_ll_load(SHA2_256);
    sha256_ll_wait_idle();

    // 2. Process second block (tail + nonce + padding)
    ll_fill_second_block(tail, nonce);
    sha_ll_continue_block(SHA2_256);
    sha256_ll_wait_idle();
    sha_ll_load(SHA2_256);
    sha256_ll_wait_idle();

    // === Second SHA-256: Hash of first hash ===
    ll_fill_double_block();
    sha_ll_start_block(SHA2_256);
    sha256_ll_wait_idle();
    sha_ll_load(SHA2_256);

    return ll_read_digest_if(hash_out);

#else
    // ESP32-S3/C3 implementation
    ll_write_digest(midstate);
    ll_fill_second_block(tail, nonce);
    REG_WRITE(SHA_CONTINUE_REG, 1);

    sha_ll_load(SHA2_256);
    sha256_ll_wait_idle();

    ll_fill_inter_block();
    REG_WRITE(SHA_START_REG, 1);

    sha_ll_load(SHA2_256);
    sha256_ll_wait_idle();

    return ll_read_digest_if(hash_out);
#endif
}

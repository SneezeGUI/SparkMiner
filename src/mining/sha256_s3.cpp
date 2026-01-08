#include <Arduino.h>
#include "sha256_s3.h"
#include "sha256_ll.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3

// ESP-IDF SHA peripheral includes for hardware acquisition
#include <sha/sha_dma.h>

// ESP32-S3 SHA Register Definitions (from ESP-IDF hwcrypto_reg.h)
#define S3_SHA_BASE         0x6003B000
#define SHA_MODE_REG        (S3_SHA_BASE + 0x00)
#define SHA_START_REG       (S3_SHA_BASE + 0x10)
#define SHA_CONTINUE_REG    (S3_SHA_BASE + 0x14)
#define SHA_BUSY_REG        (S3_SHA_BASE + 0x18)
#define SHA_H_BASE          (S3_SHA_BASE + 0x40)
#define SHA_TEXT_BASE       (S3_SHA_BASE + 0x80)

// SHA-256 mode value
#define SHA2_256 2

static inline void IRAM_ATTR write_reg(uint32_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t IRAM_ATTR read_reg(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline bool IRAM_ATTR wait_idle() {
    uint32_t timeout = 20000;
    while (read_reg(SHA_BUSY_REG) != 0) {
        if (--timeout == 0) return false;
    }
    return true;
}

void sha256_s3_init(void) {
    Serial.println("[SHA-S3] Optimized S3 mining initialized (Direct Regs)");

    // CRITICAL: Acquire SHA hardware - enables clock and power to peripheral
    esp_sha_acquire_hardware();

    // Set SHA-256 mode
    write_reg(SHA_MODE_REG, SHA2_256);

    // Test SHA hardware with known input
    // SHA256("") = e3b0c442...
    // Padded block for empty message (big-endian format):
    // Word 0 = 0x80000000 (padding bit), Words 1-14 = 0, Word 15 = 0 (length=0)
    // ESP32 registers are Little Endian. To write 0x80 at byte 0, we must write 0x00000080.
    write_reg(SHA_TEXT_BASE + 0*4, 0x00000080);  // Padding bit in MSB (byte 0)
    for (int i = 1; i < 15; i++) {
        write_reg(SHA_TEXT_BASE + i*4, 0);
    }
    write_reg(SHA_TEXT_BASE + 15*4, 0);  // Length = 0 bits

    // Start fresh SHA
    write_reg(SHA_MODE_REG, SHA2_256);
    write_reg(SHA_START_REG, 1);

    if (!wait_idle()) {
        Serial.println("[SHA-S3] ERROR: Hardware timeout during self-test!");
        esp_sha_release_hardware();
        return;
    }

    // Read result
    uint32_t h0 = __builtin_bswap32(read_reg(SHA_H_BASE + 0x00));
    uint32_t h7 = __builtin_bswap32(read_reg(SHA_H_BASE + 0x1C));

    // SHA256("") H0 should be 0xe3b0c442
    Serial.printf("[SHA-S3] Self-test: H0=%08x H7=%08x (expected H0=e3b0c442)\n", h0, h7);

    if (h0 != 0xe3b0c442) {
        Serial.println("[SHA-S3] WARNING: SHA hardware self-test FAILED!");
        Serial.printf("[SHA-S3] Debug: SHA_TEXT_BASE=%08x SHA_H_BASE=%08x\n", (uint32_t)SHA_TEXT_BASE, (uint32_t)SHA_H_BASE);
    } else {
        Serial.println("[SHA-S3] SHA hardware self-test PASSED");
    }

    // Release hardware after self-test (mining task will acquire it again)
    esp_sha_release_hardware();
}

void sha256_s3_midstate(uint32_t *midstate_out, const uint8_t *header_64bytes) {
    const uint32_t *data = (const uint32_t *)header_64bytes;

    // 1. Write first 64 bytes to text buffer
    // ESP32 registers are little-endian - when we write our LE uint32_t values,
    // the byte order in memory matches what SHA-256 expects
    for (int i=0; i<16; i++) {
        write_reg(SHA_TEXT_BASE + i*4, data[i]);
    }

    // 2. Start SHA-256 (Fresh block)
    write_reg(SHA_MODE_REG, SHA2_256);
    write_reg(SHA_START_REG, 1);

    if (!wait_idle()) {
        Serial.println("[SHA-S3] CRITICAL: Midstate timeout");
        return;
    }

    // 3. Read result
    for (int i=0; i<8; i++) {
        midstate_out[i] = read_reg(SHA_H_BASE + i*4);
    }
}

// Status logging - once per minute
static uint32_t s_last_status_time = 0;
static uint64_t s_last_status_hashes = 0;

bool IRAM_ATTR sha256_s3_mine(
    const uint32_t *midstate,
    const uint8_t *header_tail,
    uint32_t *nonce_ptr,
    volatile uint64_t *hash_count,
    volatile bool *mining_flag
) {
    const uint32_t *tail_words = (const uint32_t*)header_tail;
    uint32_t nonce = *nonce_ptr;

    // Cache tail words
    uint32_t t0 = tail_words[0];
    uint32_t t1 = tail_words[1];
    uint32_t t2 = tail_words[2];

    // Mining loop - process batches of 64k nonces then yield
    for (uint32_t i = 0; i < 0x10000; i++) {
        if (!*mining_flag) {
            *nonce_ptr = nonce;
            return false;
        }

        // ========== HASH 1: Midstate + Tail ==========

        // Restore midstate
        write_reg(SHA_H_BASE + 0x00, midstate[0]);
        write_reg(SHA_H_BASE + 0x04, midstate[1]);
        write_reg(SHA_H_BASE + 0x08, midstate[2]);
        write_reg(SHA_H_BASE + 0x0C, midstate[3]);
        write_reg(SHA_H_BASE + 0x10, midstate[4]);
        write_reg(SHA_H_BASE + 0x14, midstate[5]);
        write_reg(SHA_H_BASE + 0x18, midstate[6]);
        write_reg(SHA_H_BASE + 0x1C, midstate[7]);

        // Write tail + nonce
        write_reg(SHA_TEXT_BASE + 0x00, t0);
        write_reg(SHA_TEXT_BASE + 0x04, t1);
        write_reg(SHA_TEXT_BASE + 0x08, t2);
        write_reg(SHA_TEXT_BASE + 0x0C, nonce);

        // Padding for 80-byte message (640 bits)
        write_reg(SHA_TEXT_BASE + 0x10, 0x00000080);
        write_reg(SHA_TEXT_BASE + 0x14, 0);
        write_reg(SHA_TEXT_BASE + 0x18, 0);
        write_reg(SHA_TEXT_BASE + 0x1C, 0);
        write_reg(SHA_TEXT_BASE + 0x20, 0);
        write_reg(SHA_TEXT_BASE + 0x24, 0);
        write_reg(SHA_TEXT_BASE + 0x28, 0);
        write_reg(SHA_TEXT_BASE + 0x2C, 0);
        write_reg(SHA_TEXT_BASE + 0x30, 0);
        write_reg(SHA_TEXT_BASE + 0x34, 0);
        write_reg(SHA_TEXT_BASE + 0x38, 0);
        write_reg(SHA_TEXT_BASE + 0x3C, 0x80020000);  // 640 bits (0x280) in BE (stored as LE word)

        // Start SHA (continue mode)
        write_reg(SHA_MODE_REG, SHA2_256);
        write_reg(SHA_CONTINUE_REG, 1);

        if (!wait_idle()) return false;

        // ========== HASH 2: Double SHA ==========

        // Read Hash1 result and write to text
        uint32_t h0 = read_reg(SHA_H_BASE + 0x00);
        uint32_t h1 = read_reg(SHA_H_BASE + 0x04);
        uint32_t h2 = read_reg(SHA_H_BASE + 0x08);
        uint32_t h3 = read_reg(SHA_H_BASE + 0x0C);
        uint32_t h4 = read_reg(SHA_H_BASE + 0x10);
        uint32_t h5 = read_reg(SHA_H_BASE + 0x14);
        uint32_t h6 = read_reg(SHA_H_BASE + 0x18);
        uint32_t h7 = read_reg(SHA_H_BASE + 0x1C);

        write_reg(SHA_TEXT_BASE + 0x00, h0);
        write_reg(SHA_TEXT_BASE + 0x04, h1);
        write_reg(SHA_TEXT_BASE + 0x08, h2);
        write_reg(SHA_TEXT_BASE + 0x0C, h3);
        write_reg(SHA_TEXT_BASE + 0x10, h4);
        write_reg(SHA_TEXT_BASE + 0x14, h5);
        write_reg(SHA_TEXT_BASE + 0x18, h6);
        write_reg(SHA_TEXT_BASE + 0x1C, h7);

        // Padding for 32-byte message (256 bits)
        write_reg(SHA_TEXT_BASE + 0x20, 0x00000080);
        write_reg(SHA_TEXT_BASE + 0x24, 0);
        write_reg(SHA_TEXT_BASE + 0x28, 0);
        write_reg(SHA_TEXT_BASE + 0x2C, 0);
        write_reg(SHA_TEXT_BASE + 0x30, 0);
        write_reg(SHA_TEXT_BASE + 0x34, 0);
        write_reg(SHA_TEXT_BASE + 0x38, 0);
        write_reg(SHA_TEXT_BASE + 0x3C, 0x00010000);  // 256 bits (0x100) in BE

        // Start SHA (fresh mode)
        write_reg(SHA_MODE_REG, SHA2_256);
        write_reg(SHA_START_REG, 1);

        if (!wait_idle()) return false;

        // Update hash count
        (*hash_count)++;

        // Quick check: H0 upper 16 bits should be 0 for valid share
        uint32_t h0_final = __builtin_bswap32(read_reg(SHA_H_BASE + 0x00));

        // Status log once per minute
        uint32_t now = millis();
        if (now - s_last_status_time >= 60000) {
            uint64_t current_hashes = *hash_count;
            uint64_t hashes_this_period = current_hashes - s_last_status_hashes;
            float hashrate = hashes_this_period / 60.0f;
            Serial.printf("[SHA-S3] Status: %.1f KH/s, nonce=%08x\n", hashrate / 1000.0f, nonce);
            s_last_status_time = now;
            s_last_status_hashes = current_hashes;
        }

        if ((h0_final >> 16) == 0) {
            // Potential share found!
            *nonce_ptr = nonce;
            return true;
        }

        nonce++;
    }

    // Yield after 64k hashes
    *nonce_ptr = nonce;
    return false;
}

bool sha256_s3_verify(
    const uint32_t *midstate,
    const uint8_t *header_tail,
    uint32_t nonce,
    uint8_t *hash_out
) {
    const uint32_t *tail_words = (const uint32_t*)header_tail;

    // 1. Restore Midstate
    write_reg(SHA_H_BASE + 0x00, midstate[0]);
    write_reg(SHA_H_BASE + 0x04, midstate[1]);
    write_reg(SHA_H_BASE + 0x08, midstate[2]);
    write_reg(SHA_H_BASE + 0x0C, midstate[3]);
    write_reg(SHA_H_BASE + 0x10, midstate[4]);
    write_reg(SHA_H_BASE + 0x14, midstate[5]);
    write_reg(SHA_H_BASE + 0x18, midstate[6]);
    write_reg(SHA_H_BASE + 0x1C, midstate[7]);

    // 2. Write Text + nonce
    write_reg(SHA_TEXT_BASE + 0x00, tail_words[0]);
    write_reg(SHA_TEXT_BASE + 0x04, tail_words[1]);
    write_reg(SHA_TEXT_BASE + 0x08, tail_words[2]);
    write_reg(SHA_TEXT_BASE + 0x0C, nonce);
    write_reg(SHA_TEXT_BASE + 0x10, 0x00000080); // Corrected padding (LE write of 0x80 byte)

    for (int i=5; i<15; i++) write_reg(SHA_TEXT_BASE + i*4, 0);
    write_reg(SHA_TEXT_BASE + 0x3C, 0x80020000); // Corrected length (640 bits)

    // 3. Start SHA
    write_reg(SHA_MODE_REG, SHA2_256);
    write_reg(SHA_CONTINUE_REG, 1);

    if (!wait_idle()) return false;

    // 4. Copy Result
    uint32_t h[8];
    for (int i=0; i<8; i++) h[i] = read_reg(SHA_H_BASE + i*4);

    for (int i=0; i<8; i++) write_reg(SHA_TEXT_BASE + i*4, h[i]);

    write_reg(SHA_TEXT_BASE + 0x20, 0x00000080); // Corrected padding
    for (int i=9; i<15; i++) write_reg(SHA_TEXT_BASE + i*4, 0);
    write_reg(SHA_TEXT_BASE + 0x3C, 0x00010000); // Corrected length (256 bits)

    // 5. Start SHA
    write_reg(SHA_START_REG, 1);

    if (!wait_idle()) return false;

    // 6. Read Output - reverse word order and byte-swap to match check_target expectations
    // check_target compares from bytes[31] down, so H0 (MSB) must be at bytes[28-31]
    uint32_t *out = (uint32_t *)hash_out;
    out[7] = __builtin_bswap32(read_reg(SHA_H_BASE + 0x00));  // H0 -> bytes[28-31]
    out[6] = __builtin_bswap32(read_reg(SHA_H_BASE + 0x04));  // H1 -> bytes[24-27]
    out[5] = __builtin_bswap32(read_reg(SHA_H_BASE + 0x08));  // H2 -> bytes[20-23]
    out[4] = __builtin_bswap32(read_reg(SHA_H_BASE + 0x0C));  // H3 -> bytes[16-19]
    out[3] = __builtin_bswap32(read_reg(SHA_H_BASE + 0x10));  // H4 -> bytes[12-15]
    out[2] = __builtin_bswap32(read_reg(SHA_H_BASE + 0x14));  // H5 -> bytes[8-11]
    out[1] = __builtin_bswap32(read_reg(SHA_H_BASE + 0x18));  // H6 -> bytes[4-7]
    out[0] = __builtin_bswap32(read_reg(SHA_H_BASE + 0x1C));  // H7 -> bytes[0-3]

    return true;
}

#endif

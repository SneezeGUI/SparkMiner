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

static void s3_log_words(const char *tag, const char *label, const uint32_t *words, size_t count) {
    Serial.printf("%s %s=", tag, label);
    for (size_t i = 0; i < count; i++) {
        Serial.printf("%08x", words[i]);
        if (i + 1 < count) Serial.print(" ");
    }
    Serial.println();
}

static void s3_log_bytes(const char *tag, const char *label, const uint8_t *bytes, size_t count) {
    Serial.printf("%s %s=", tag, label);
    for (size_t i = 0; i < count; i++) {
        Serial.printf("%02x", bytes[i]);
    }
    Serial.println();
}

static void s3_words_to_be_bytes(const uint32_t words[8], uint8_t out[32]) {
    for (int i = 0; i < 8; i++) {
        out[i * 4 + 0] = (uint8_t)(words[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(words[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(words[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(words[i]);
    }
}

static void s3_words_to_le_bytes(const uint32_t words[8], uint8_t out[32]) {
    for (int i = 0; i < 8; i++) {
        out[i * 4 + 0] = (uint8_t)(words[i]);
        out[i * 4 + 1] = (uint8_t)(words[i] >> 8);
        out[i * 4 + 2] = (uint8_t)(words[i] >> 16);
        out[i * 4 + 3] = (uint8_t)(words[i] >> 24);
    }
}

static void s3_build_second_sha_block_from_first_digest(const uint8_t first_digest_be[32], uint8_t block64[64]) {
    memset(block64, 0, 64);
    memcpy(block64, first_digest_be, 32);
    block64[32] = 0x80;
    block64[62] = 0x01;
    block64[63] = 0x00;
}

static void sha256_s3_transform_midstate_words(uint32_t out[8], const uint32_t in[8], int mode) {
    for (int i = 0; i < 8; i++) {
        switch (mode) {
            case 0: // A
                out[i] = in[i];
                break;
            case 1: // B
                out[i] = __builtin_bswap32(in[i]);
                break;
            case 2: // C
                out[i] = in[7 - i];
                break;
            case 3: // D
                out[i] = __builtin_bswap32(in[7 - i]);
                break;
            default:
                out[i] = in[i];
                break;
        }
    }
}

static const char *sha256_s3_mode_name(int mode) {
    switch (mode) {
        case 0: return "A";
        case 1: return "B";
        case 2: return "C";
        case 3: return "D";
        default: return "?";
    }
}

static const char *sha256_s3_tail_mode_name(int mode) {
    switch (mode) {
        case 0: return "T0";
        case 1: return "T1";
        case 2: return "T2";
        default: return "?";
    }
}

static inline uint32_t load_le32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_sw_compress_one_block_from_iv(const uint8_t block[64], uint8_t out_digest[32]) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = load_be32(block + (i * 4));
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = 0x6a09e667;
    uint32_t b = 0xbb67ae85;
    uint32_t c = 0x3c6ef372;
    uint32_t d = 0xa54ff53a;
    uint32_t e = 0x510e527f;
    uint32_t f = 0x9b05688c;
    uint32_t g = 0x1f83d9ab;
    uint32_t h = 0x5be0cd19;

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + k[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    uint32_t H[8];
    H[0] = 0x6a09e667 + a;
    H[1] = 0xbb67ae85 + b;
    H[2] = 0x3c6ef372 + c;
    H[3] = 0xa54ff53a + d;
    H[4] = 0x510e527f + e;
    H[5] = 0x9b05688c + f;
    H[6] = 0x1f83d9ab + g;
    H[7] = 0x5be0cd19 + h;

    for (int i = 0; i < 8; i++) {
        out_digest[i * 4 + 0] = (uint8_t)(H[i] >> 24);
        out_digest[i * 4 + 1] = (uint8_t)(H[i] >> 16);
        out_digest[i * 4 + 2] = (uint8_t)(H[i] >> 8);
        out_digest[i * 4 + 3] = (uint8_t)(H[i]);
    }
}

static bool sha256_s3_hw_run_one_block_from_iv(const uint8_t block[64], uint8_t out_digest[32]) {
    uint32_t text_words[16];
    for (int i = 0; i < 16; i++) {
        text_words[i] = load_le32(block + (i * 4));
    }

    s3_log_bytes("[S3-IV64]", "logical_block_bytes", block, 64);
    s3_log_words("[S3-IV64]", "actual_register_writes_words", text_words, 16);

    for (int i = 0; i < 16; i++) {
        write_reg(SHA_TEXT_BASE + (uint32_t)(i * 4), text_words[i]);
    }

    write_reg(SHA_MODE_REG, SHA2_256);
    write_reg(SHA_START_REG, 1);
    if (!wait_idle()) return false;

    uint32_t raw[8];
    for (int i = 0; i < 8; i++) raw[i] = read_reg(SHA_H_BASE + (i * 4));
    for (int i = 0; i < 8; i++) {
        uint32_t be = __builtin_bswap32(raw[i]);
        out_digest[i * 4 + 0] = (uint8_t)(be >> 24);
        out_digest[i * 4 + 1] = (uint8_t)(be >> 16);
        out_digest[i * 4 + 2] = (uint8_t)(be >> 8);
        out_digest[i * 4 + 3] = (uint8_t)(be);
    }
    return true;
}

static bool sha256_s3_hw_restore_iv_then_continue_one_block(const uint8_t block[64], uint8_t out_digest[32]) {
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint32_t text_words[16];
    for (int i = 0; i < 16; i++) {
        text_words[i] = load_le32(block + (i * 4));
    }

    s3_log_words("[S3-IV64]", "restore_iv_words", iv, 8);
    s3_log_words("[S3-IV64]", "restore_text_words", text_words, 16);
    s3_log_bytes("[S3-IV64]", "restore_block_bytes", block, 64);

    for (int i = 0; i < 8; i++) {
        write_reg(SHA_H_BASE + (uint32_t)(i * 4), iv[i]);
    }
    uint32_t h0_after_restore = read_reg(SHA_H_BASE + 0x00);
    Serial.printf("[S3-IV64] restore_step=after_iv_write h0=%08x\n", h0_after_restore);
    for (int i = 0; i < 16; i++) {
        write_reg(SHA_TEXT_BASE + (uint32_t)(i * 4), text_words[i]);
    }

    write_reg(SHA_MODE_REG, SHA2_256);
    uint32_t h0_after_mode = read_reg(SHA_H_BASE + 0x00);
    Serial.printf("[S3-IV64] restore_step=after_set_mode h0=%08x\n", h0_after_mode);
    write_reg(SHA_CONTINUE_REG, 1);
    if (!wait_idle()) return false;

    uint32_t raw[8];
    for (int i = 0; i < 8; i++) raw[i] = read_reg(SHA_H_BASE + (i * 4));
    s3_log_words("[S3-IV64]", "restore_digest_raw_words", raw, 8);
    for (int i = 0; i < 8; i++) {
        uint32_t be = __builtin_bswap32(raw[i]);
        out_digest[i * 4 + 0] = (uint8_t)(be >> 24);
        out_digest[i * 4 + 1] = (uint8_t)(be >> 16);
        out_digest[i * 4 + 2] = (uint8_t)(be >> 8);
        out_digest[i * 4 + 3] = (uint8_t)(be);
    }
    return true;
}

static void sha256_s3_build_tail_block_t0(uint8_t out[64], const uint8_t *canonical_header_tail16) {
    memset(out, 0, 64);
    memcpy(out, canonical_header_tail16, 16);
    out[16] = 0x80;
    out[62] = 0x02;
    out[63] = 0x80;
}

static void sha256_s3_build_tail_block_t1_from_t0(uint8_t out[64], const uint8_t t0[64]) {
    memcpy(out, t0, 64);
    // Swap only the first 4 message words (16 bytes), keep padding/length canonical.
    for (int w = 0; w < 4; w++) {
        uint8_t b0 = out[w * 4 + 0];
        uint8_t b1 = out[w * 4 + 1];
        uint8_t b2 = out[w * 4 + 2];
        uint8_t b3 = out[w * 4 + 3];
        out[w * 4 + 0] = b3;
        out[w * 4 + 1] = b2;
        out[w * 4 + 2] = b1;
        out[w * 4 + 3] = b0;
    }
}

static void sha256_s3_build_tail_block_t2(
    uint8_t out[64],
    const uint8_t *canonical_header_tail16,
    const uint32_t *header_tail_words_swapped
) {
    memset(out, 0, 64);
    if (header_tail_words_swapped) {
        // Interpret these as register-source words and serialize as little-endian bytes.
        for (int i = 0; i < 4; i++) {
            uint32_t w = header_tail_words_swapped[i];
            out[i * 4 + 0] = (uint8_t)(w & 0xFF);
            out[i * 4 + 1] = (uint8_t)((w >> 8) & 0xFF);
            out[i * 4 + 2] = (uint8_t)((w >> 16) & 0xFF);
            out[i * 4 + 3] = (uint8_t)((w >> 24) & 0xFF);
        }
    } else {
        // Fallback keeps T2 deterministic even if swapped words are unavailable.
        memcpy(out, canonical_header_tail16, 16);
    }
    out[16] = 0x80;
    out[62] = 0x02;
    out[63] = 0x80;
}

static void sha256_s3_build_text_words_from_tail_block(const uint8_t tail_block[64], uint32_t text_words[16]) {
    for (int i = 0; i < 16; i++) {
        text_words[i] = load_le32(tail_block + (i * 4));
    }
}

static void sha256_s3_restore_midstate(
    const uint32_t *midstate_words,
    int mode,
    uint32_t transformed_words[8],
    uint32_t readback_words[8]
) {
    if (!midstate_words || !transformed_words || !readback_words) return;

    Serial.printf("[S3-RESTORE] input_ptr=%p\n", (const void *)midstate_words);
    s3_log_words("[S3-RESTORE]", "input_words", midstate_words, 8);
    s3_log_bytes("[S3-RESTORE]", "input_bytes", (const uint8_t *)midstate_words, 32);

    uint32_t before_words[8];
    memcpy(before_words, midstate_words, sizeof(before_words));
    s3_log_words("[S3-RESTORE]", "before_words", before_words, 8);

    sha256_s3_transform_midstate_words(transformed_words, midstate_words, mode);
    Serial.printf("[S3-RESTORE] step_name=MODE_%s\n", sha256_s3_mode_name(mode));
    s3_log_words("[S3-RESTORE]", "after_words", transformed_words, 8);

    for (int i = 0; i < 8; i++) {
        uint32_t offset = (uint32_t)(i * 4);
        write_reg(SHA_H_BASE + offset, transformed_words[i]);
        Serial.printf("[S3-RESTORE] write SHA_H[%d] offset=0x%02x value=%08x\n", i, offset, transformed_words[i]);
        readback_words[i] = read_reg(SHA_H_BASE + offset);
        Serial.printf("[S3-RESTORE] read SHA_H[%d] value=%08x\n", i, readback_words[i]);
    }
}

static bool sha256_s3_first_from_midstate_mode(
    const uint32_t *midstate_words,
    const uint8_t tail_block[64],
    const char *tail_mode_name,
    int mode,
    uint8_t first_digest_out[32]
) {
    if (!midstate_words || !tail_block || !tail_mode_name || !first_digest_out) return false;

    uint32_t transformed[8] = {0};
    uint32_t readback[8] = {0};
    uint32_t text_words[16] = {0};

    sha256_s3_restore_midstate(midstate_words, mode, transformed, readback);

    s3_log_bytes("[S3-RESTORE]", "logical_tail_block_bytes", tail_block, 64);

    sha256_s3_build_text_words_from_tail_block(tail_block, text_words);
    s3_log_words("[S3-RESTORE]", "actual_register_writes_words", text_words, 16);
    Serial.printf("[S3-RESTORE] tail_mode=%s\n", tail_mode_name);

    for (int i = 0; i < 16; i++) {
        write_reg(SHA_TEXT_BASE + (uint32_t)(i * 4), text_words[i]);
    }

    // Check whether setting mode appears to clobber restored SHA_H state.
    uint32_t before_mode_h0 = read_reg(SHA_H_BASE + 0x00);
    write_reg(SHA_MODE_REG, SHA2_256);
    uint32_t after_mode_h0 = read_reg(SHA_H_BASE + 0x00);
    Serial.printf("[S3-RESTORE] step_name=SET_MODE before_h0=%08x after_h0=%08x\n", before_mode_h0, after_mode_h0);

    write_reg(SHA_CONTINUE_REG, 1);
    if (!wait_idle()) return false;

    uint32_t raw_first[8];
    for (int i = 0; i < 8; i++) {
        raw_first[i] = read_reg(SHA_H_BASE + (i * 4));
    }
    s3_log_words("[S3-RESTORE]", "s3_first_raw_words", raw_first, 8);

    uint32_t *out_words = (uint32_t *)first_digest_out;
    for (int i = 0; i < 8; i++) {
        out_words[i] = __builtin_bswap32(raw_first[i]);
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
    return sha256_s3_verify_trace(midstate, header_tail, nonce, hash_out, NULL);
}

bool sha256_s3_verify_trace(
    const uint32_t *midstate,
    const uint8_t *header_tail,
    uint32_t nonce,
    uint8_t *hash_out,
    sha256_s3_verify_trace_t *trace
) {
    const uint32_t *tail_words = (const uint32_t*)header_tail;

    // 1. Restore Midstate
    for (int i = 0; i < 8; i++) {
        if (trace) trace->restoredMidstateWords[i] = midstate[i];
        write_reg(SHA_H_BASE + (i * 4), midstate[i]);
    }

    if (trace) {
        for (int i = 0; i < 8; i++) {
            trace->shaHAfterRestoreWords[i] = read_reg(SHA_H_BASE + (i * 4));
        }
    }

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

    if (trace) {
        uint32_t *firstWords = (uint32_t *)trace->firstDigestBytes;
        for (int i = 0; i < 8; i++) {
            trace->firstDigestRawWords[i] = h[i];
            firstWords[i] = __builtin_bswap32(h[i]);
        }
    }

    for (int i=0; i<8; i++) write_reg(SHA_TEXT_BASE + i*4, h[i]);

    if (trace) {
        memset(trace->secondInputBlockBytes, 0, sizeof(trace->secondInputBlockBytes));
        memcpy(trace->secondInputBlockBytes, trace->firstDigestBytes, 32);
        trace->secondInputBlockBytes[32] = 0x80;
        trace->secondInputBlockBytes[62] = 0x01;
        trace->secondInputBlockBytes[63] = 0x00;
    }

    write_reg(SHA_TEXT_BASE + 0x20, 0x00000080); // Corrected padding
    for (int i=9; i<15; i++) write_reg(SHA_TEXT_BASE + i*4, 0);
    write_reg(SHA_TEXT_BASE + 0x3C, 0x00010000); // Corrected length (256 bits)

    // 5. Start SHA
    write_reg(SHA_START_REG, 1);

    if (!wait_idle()) return false;

    uint32_t rawFinal[8];
    for (int i = 0; i < 8; i++) {
        rawFinal[i] = read_reg(SHA_H_BASE + (i * 4));
        if (trace) trace->secondDigestRawWords[i] = rawFinal[i];
    }

    if (trace) {
        for (int i = 0; i < 8; i++) {
            uint32_t be = __builtin_bswap32(rawFinal[i]);
            trace->finalDigestBeBytes[i * 4 + 0] = (uint8_t)(be >> 24);
            trace->finalDigestBeBytes[i * 4 + 1] = (uint8_t)(be >> 16);
            trace->finalDigestBeBytes[i * 4 + 2] = (uint8_t)(be >> 8);
            trace->finalDigestBeBytes[i * 4 + 3] = (uint8_t)(be);
        }
    }

    // 6. Read Output - reverse word order and byte-swap to match check_target expectations
    // check_target compares from bytes[31] down, so H0 (MSB) must be at bytes[28-31]
    uint32_t *out = (uint32_t *)hash_out;
    out[7] = __builtin_bswap32(rawFinal[0]);  // H0 -> bytes[28-31]
    out[6] = __builtin_bswap32(rawFinal[1]);  // H1 -> bytes[24-27]
    out[5] = __builtin_bswap32(rawFinal[2]);  // H2 -> bytes[20-23]
    out[4] = __builtin_bswap32(rawFinal[3]);  // H3 -> bytes[16-19]
    out[3] = __builtin_bswap32(rawFinal[4]);  // H4 -> bytes[12-15]
    out[2] = __builtin_bswap32(rawFinal[5]);  // H5 -> bytes[8-11]
    out[1] = __builtin_bswap32(rawFinal[6]);  // H6 -> bytes[4-7]
    out[0] = __builtin_bswap32(rawFinal[7]);  // H7 -> bytes[0-3]

    if (trace) {
        memcpy(trace->finalDigestBytes, hash_out, 32);
    }

    return true;
}

bool sha256_s3_test_restore_mapping(
    const uint32_t *midstate_words,
    const uint8_t *canonical_header_tail16,
    const uint32_t *header_tail_words_swapped,
    const uint8_t *expected_first_digest
) {
    if (!midstate_words || !canonical_header_tail16 || !expected_first_digest) return false;

    uint8_t tailT0[64] = {0};
    uint8_t tailT1[64] = {0};
    uint8_t tailT2[64] = {0};
    sha256_s3_build_tail_block_t0(tailT0, canonical_header_tail16);
    sha256_s3_build_tail_block_t1_from_t0(tailT1, tailT0);
    sha256_s3_build_tail_block_t2(tailT2, canonical_header_tail16, header_tail_words_swapped);

    s3_log_bytes("[S3-MATRIX]", "tail_T0", tailT0, 64);
    s3_log_bytes("[S3-MATRIX]", "tail_T1", tailT1, 64);
    s3_log_bytes("[S3-MATRIX]", "tail_T2", tailT2, 64);

    bool t1eqt2 = (memcmp(tailT1, tailT2, 64) == 0);
    Serial.printf("[S3-MATRIX] tail_T1_equals_T2=%s\n", t1eqt2 ? "YES" : "NO");

    const uint8_t *tailBlocks[3] = {tailT0, tailT1, tailT2};

    bool anyPass = false;
    for (int restoreMode = 0; restoreMode < 4; restoreMode++) {
        for (int tailMode = 0; tailMode < 3; tailMode++) {
            uint8_t first_digest[32] = {0};
            bool ok = sha256_s3_first_from_midstate_mode(
                midstate_words,
                tailBlocks[tailMode],
                sha256_s3_tail_mode_name(tailMode),
                restoreMode,
                first_digest
            );
            bool match = ok && (memcmp(first_digest, expected_first_digest, 32) == 0);

            Serial.printf("[S3-MATRIX] restore=%s tail=%s first=",
                          sha256_s3_mode_name(restoreMode),
                          sha256_s3_tail_mode_name(tailMode));
            for (int i = 0; i < 32; i++) Serial.printf("%02x", first_digest[i]);
            Serial.printf(" match=%s\n", match ? "PASS" : "FAIL");

            anyPass = anyPass || match;
        }
    }

    Serial.printf("[S3-MATRIX] any_match=%s\n", anyPass ? "PASS" : "FAIL");

    return anyPass;
}

bool sha256_s3_test_one_block_from_iv(void) {
    // Canonical one-block SHA-256 message block for "abc" (already padded to 64 bytes).
    static const uint8_t block64[64] = {
        0x61,0x62,0x63,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18
    };
    static const uint8_t expected_digest[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };

    uint8_t hw_from_iv[32] = {0};
    uint8_t sw_from_iv[32] = {0};

    s3_log_bytes("[S3-IV64]", "block64", block64, 64);
    s3_log_bytes("[S3-IV64]", "expected_digest", expected_digest, 32);

    bool hwStartOk = sha256_s3_hw_run_one_block_from_iv(block64, hw_from_iv);
    sha256_sw_compress_one_block_from_iv(block64, sw_from_iv);

    bool hwStartMatch = hwStartOk && (memcmp(hw_from_iv, expected_digest, 32) == 0);
    bool swMatch = (memcmp(sw_from_iv, expected_digest, 32) == 0);

    s3_log_bytes("[S3-IV64]", "hw_from_iv", hw_from_iv, 32);
    Serial.printf("[S3-IV64] test=1 hw_full_64_from_iv match=%s\n", hwStartMatch ? "PASS" : "FAIL");

    s3_log_bytes("[S3-IV64]", "sw_compress_from_iv", sw_from_iv, 32);
    Serial.printf("[S3-IV64] test=2 sw_compress_from_iv match=%s\n", swMatch ? "PASS" : "FAIL");

    uint8_t hw_restore_continue[32] = {0};
    bool hwRestoreOk = sha256_s3_hw_restore_iv_then_continue_one_block(block64, hw_restore_continue);
    bool hwRestoreMatch = hwRestoreOk && (memcmp(hw_restore_continue, expected_digest, 32) == 0);
    s3_log_bytes("[S3-IV64]", "hw_restore_iv_plus_continue", hw_restore_continue, 32);
    Serial.printf("[S3-IV64] test=3 hw_restore_iv_plus_continue match=%s\n", hwRestoreMatch ? "PASS" : "FAIL");

    // Official API path for resume testing (esp_sha_write_digest_state + continue block).
    static const uint32_t iv_words[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint32_t api_raw[8] = {0};
    uint8_t api_raw_as_be[32] = {0};
    uint8_t api_raw_as_le[32] = {0};
    uint32_t iv_copy[8];
    memcpy(iv_copy, iv_words, sizeof(iv_copy));

    esp_sha_write_digest_state((esp_sha_type)SHA2_256, iv_copy);
    int api_ret = esp_sha_dma((esp_sha_type)SHA2_256, block64, 64, NULL, 0, false);
    esp_sha_read_digest_state((esp_sha_type)SHA2_256, api_raw);
    s3_words_to_be_bytes(api_raw, api_raw_as_be);
    s3_words_to_le_bytes(api_raw, api_raw_as_le);

    bool api_match_be = (memcmp(api_raw_as_be, expected_digest, 32) == 0);
    bool api_match_le = (memcmp(api_raw_as_le, expected_digest, 32) == 0);

    Serial.printf("[S3-IV64] test=4 api_resume ret=%d be_match=%s le_match=%s\n",
                  api_ret,
                  api_match_be ? "PASS" : "FAIL",
                  api_match_le ? "PASS" : "FAIL");
    s3_log_words("[S3-IV64]", "api_resume_raw_words", api_raw, 8);
    s3_log_bytes("[S3-IV64]", "api_resume_raw_as_be", api_raw_as_be, 32);
    s3_log_bytes("[S3-IV64]", "api_resume_raw_as_le", api_raw_as_le, 32);

    bool overall = hwStartMatch && swMatch && hwRestoreMatch;
    Serial.printf("[S3-IV64] overall=%s\n", overall ? "PASS" : "FAIL");
    return overall;
}

static bool sha256_s3_second_sha_from_first_be_internal(
    const uint8_t first_digest_be[32],
    uint8_t out_digest_be[32],
    bool verbose
) {
    if (!first_digest_be || !out_digest_be) return false;

    uint32_t text_words[8];
    for (int i = 0; i < 8; i++) {
        text_words[i] = load_le32(first_digest_be + (i * 4));
    }

    if (verbose) {
        s3_log_bytes("[S3-SHA2]", "first_digest_be", first_digest_be, 32);
        s3_log_words("[S3-SHA2]", "second_input_text_words", text_words, 8);
    }

    for (int i = 0; i < 8; i++) {
        write_reg(SHA_TEXT_BASE + (uint32_t)(i * 4), text_words[i]);
    }

    write_reg(SHA_TEXT_BASE + 0x20, 0x00000080);
    for (int i = 9; i < 15; i++) {
        write_reg(SHA_TEXT_BASE + (uint32_t)(i * 4), 0x00000000);
    }
    write_reg(SHA_TEXT_BASE + 0x3C, 0x00010000);

    write_reg(SHA_MODE_REG, SHA2_256);
    write_reg(SHA_START_REG, 1);
    if (!wait_idle()) return false;

    uint32_t raw[8];
    for (int i = 0; i < 8; i++) {
        raw[i] = read_reg(SHA_H_BASE + (uint32_t)(i * 4));
        uint32_t be = __builtin_bswap32(raw[i]);
        out_digest_be[i * 4 + 0] = (uint8_t)(be >> 24);
        out_digest_be[i * 4 + 1] = (uint8_t)(be >> 16);
        out_digest_be[i * 4 + 2] = (uint8_t)(be >> 8);
        out_digest_be[i * 4 + 3] = (uint8_t)(be);
    }

    if (verbose) {
        uint8_t raw_as_be[32] = {0};
        uint8_t raw_as_le[32] = {0};
        s3_words_to_be_bytes(raw, raw_as_be);
        s3_words_to_le_bytes(raw, raw_as_le);
        s3_log_words("[S3-SHA2]", "second_digest_raw_words", raw, 8);
        s3_log_bytes("[S3-SHA2]", "second_digest_raw_as_be", raw_as_be, 32);
        s3_log_bytes("[S3-SHA2]", "second_digest_raw_as_le", raw_as_le, 32);
        s3_log_bytes("[S3-SHA2]", "second_digest_out_be", out_digest_be, 32);
    }

    return true;
}

bool sha256_s3_second_sha_from_first_be(const uint8_t first_digest_be[32], uint8_t out_digest_be[32]) {
    return sha256_s3_second_sha_from_first_be_internal(first_digest_be, out_digest_be, false);
}

bool sha256_s3_test_second_sha_paths(void) {
    // first_digest_be is SHA256("abc")
    static const uint8_t first_digest_be[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };

    // expected_second_be is SHA256(SHA256("abc"))
    static const uint8_t expected_second_be[32] = {
        0x4f,0x8b,0x42,0xc2,0x2d,0xd3,0x72,0x9b,0x51,0x9b,0xa6,0xf6,0x8d,0x2d,0xa7,0xcc,
        0x5b,0x2d,0x60,0x6d,0x05,0xda,0xed,0x5a,0xd5,0x12,0x8c,0xc0,0x3e,0x6c,0x63,0x58
    };

    uint8_t second_block[64] = {0};
    uint8_t sw_second_from_block[32] = {0};
    uint8_t hw_second_from_block[32] = {0};
    uint8_t hw_second_fast[32] = {0};

    s3_build_second_sha_block_from_first_digest(first_digest_be, second_block);
    sha256_sw_compress_one_block_from_iv(second_block, sw_second_from_block);
    bool hw_block_ok = sha256_s3_hw_run_one_block_from_iv(second_block, hw_second_from_block);
    bool hw_fast_ok = sha256_s3_second_sha_from_first_be_internal(first_digest_be, hw_second_fast, true);

    bool sw_match = (memcmp(sw_second_from_block, expected_second_be, 32) == 0);
    bool hw_block_match = hw_block_ok && (memcmp(hw_second_from_block, expected_second_be, 32) == 0);
    bool hw_fast_match = hw_fast_ok && (memcmp(hw_second_fast, expected_second_be, 32) == 0);
    bool hw_block_vs_fast = hw_block_ok && hw_fast_ok && (memcmp(hw_second_from_block, hw_second_fast, 32) == 0);

    s3_log_bytes("[S3-SHA2]", "first_digest_be", first_digest_be, 32);
    s3_log_bytes("[S3-SHA2]", "second_block_bytes", second_block, 64);
    s3_log_bytes("[S3-SHA2]", "expected_second_be", expected_second_be, 32);
    s3_log_bytes("[S3-SHA2]", "sw_second_from_block", sw_second_from_block, 32);
    s3_log_bytes("[S3-SHA2]", "hw_second_from_block", hw_second_from_block, 32);
    s3_log_bytes("[S3-SHA2]", "hw_second_fast", hw_second_fast, 32);

    Serial.printf("[S3-SHA2] sw_ref_match=%s hw_block_match=%s hw_fast_match=%s hw_block_eq_fast=%s\n",
                  sw_match ? "PASS" : "FAIL",
                  hw_block_match ? "PASS" : "FAIL",
                  hw_fast_match ? "PASS" : "FAIL",
                  hw_block_vs_fast ? "PASS" : "FAIL");

    return sw_match && hw_block_match && hw_fast_match && hw_block_vs_fast;
}

#endif

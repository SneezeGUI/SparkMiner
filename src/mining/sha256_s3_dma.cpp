#include <Arduino.h>
#include "sha256_s3_dma.h"
#include <mbedtls/sha256.h>
#include <string.h>

void sha256_s3_dma_test() {
    Serial.println("[SHA-DMA] Starting Self-Test (mbedtls)...");

    const char *inputs[] = { "", "abc" };
    // SHA-256("")
    const char *expected_0 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    // SHA-256("abc")
    const char *expected_1 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    const char *expected[] = { expected_0, expected_1 };

    for (int i = 0; i < 2; i++) {
        unsigned char output[32];
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        
        // Use _ret variants for better error handling and compatibility with newer IDF
        int ret = 0;
        if ((ret = mbedtls_sha256_starts_ret(&ctx, 0)) != 0) {
             Serial.printf("[SHA-DMA] mbedtls_sha256_starts_ret failed: -0x%04x\n", -ret);
             mbedtls_sha256_free(&ctx);
             continue;
        }
        
        if ((ret = mbedtls_sha256_update_ret(&ctx, (const unsigned char*)inputs[i], strlen(inputs[i]))) != 0) {
             Serial.printf("[SHA-DMA] mbedtls_sha256_update_ret failed: -0x%04x\n", -ret);
             mbedtls_sha256_free(&ctx);
             continue;
        }
        
        if ((ret = mbedtls_sha256_finish_ret(&ctx, output)) != 0) {
             Serial.printf("[SHA-DMA] mbedtls_sha256_finish_ret failed: -0x%04x\n", -ret);
             mbedtls_sha256_free(&ctx);
             continue;
        }
        
        mbedtls_sha256_free(&ctx);

        Serial.printf("[SHA-DMA] Test '%s': ", inputs[i]);
        bool match = true;
        char buf[65];
        for (int j = 0; j < 32; j++) {
            sprintf(buf + j*2, "%02x", output[j]);
        }
        
        if (strcmp(buf, expected[i]) != 0) {
             match = false;
        }
        
        Serial.printf("%s %s\n", buf, match ? "PASS" : "FAIL");
        if (!match) {
            Serial.printf("Expected: %s\n", expected[i]);
        }
    }
}

void sha256_s3_dma_init() {
    Serial.println("[SHA-DMA] Initializing...");
    sha256_s3_dma_test();
}

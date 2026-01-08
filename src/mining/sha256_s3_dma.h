#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize and test the DMA-based SHA-256 implementation
void sha256_s3_dma_init(void);

// Run self-tests with known test vectors
void sha256_s3_dma_test(void);

#ifdef __cplusplus
}
#endif

/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright Theodore Robert Campbell Jr <trcjr@stupidfoot.com>
 *
 * SparkMiner - Baseline Performance Benchmark
 *
 * Measures cycle-level performance of current SHA-256 mining backend.
 * Purpose: Establish baseline (560 cycles/hash target) before optimizations.
 *
 * Enables: -DENABLE_BASELINE_BENCHMARK in platformio.ini
 */

#include <Arduino.h>
#include <esp_timer.h>
#include "miner_sha256.h"
#include "sha256_hw.h"
#include "miner.h"
#include "../logging.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Cycle Counter Utilities (Xtensa LX7)
// ============================================================

/*
 * Read Xtensa CPU cycle counter (CCOUNT register)
 * Returns 32-bit cycle count that increments every CPU clock
 * Note: Overflows every 2^32 cycles @ 240MHz = 17.9 seconds
 */
static inline uint32_t get_cpu_cycles() {
    uint32_t cycles;
    asm volatile("rsr %0, ccount" : "=r"(cycles));
    return cycles;
}

/*
 * Compute cycle delta, handling wraparound
 */
static inline uint32_t delta_cycles(uint32_t start, uint32_t end) {
    return end - start;  // Handles wrap naturally (unsigned arithmetic)
}

// ============================================================
// Test Vector: Synthetic header used for cross-validation
// Pattern matches existing MINER-TEST vectors
// ============================================================

// Synthetic 80-byte header (version=2, bytes fill 0x00-0xFF pattern)
static const uint8_t TEST_HEADER_BYTES[80] = {
    // Version (4 bytes, LE)
    0x00, 0x00, 0x00, 0x20,
    // Previous block hash (32 bytes)
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    // Merkle root (32 bytes)
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    // Timestamp (4 bytes, LE)
    0x8b, 0x26, 0x02, 0x6a,
    // Difficulty bits (4 bytes, LE)
    0xf0, 0x1f, 0x02, 0x17,
    // Nonce (4 bytes, LE)
    0x33, 0x5c, 0x42, 0x2e,
};

// ============================================================
// Baseline Benchmark Implementation
// ============================================================

/*
 * Single-pass hash timing test
 * Measures exact cycles for one double-SHA-256 computation
 */
struct hash_timing_t {
    uint32_t cycles_total;
    uint32_t cycles_midstate;
    uint32_t cycles_complete;
    bool correctness_ok;
};

static void benchmark_single_hash(hash_timing_t *result) {
    block_header_t hb = {0};
    sha256_hash_t midstate = {0};
    sha256_hash_t firstsha = {0};
    sha256_hash_t secondsha = {0};

    // Copy test header
    memcpy((uint8_t *)&hb, TEST_HEADER_BYTES, 80);

    // Warmup: Let CPU cache settle
    for (int i = 0; i < 10; i++) {
        miner_sha256_midstate(&midstate, &hb);
    }

    // Timer: Midstate computation
    uint32_t t_start = get_cpu_cycles();
    miner_sha256_midstate(&midstate, &hb);
    uint32_t t_mid = get_cpu_cycles();
    result->cycles_midstate = delta_cycles(t_start, t_mid);

    // Timer: Complete double SHA computation
    t_start = get_cpu_cycles();
    miner_sha256_complete_from_midstate(&midstate, &hb, &firstsha, &secondsha);
    uint32_t t_end = get_cpu_cycles();
    result->cycles_complete = delta_cycles(t_start, t_end);
    result->cycles_total = result->cycles_midstate + result->cycles_complete;

    // Cross-validate: direct double-SHA (same pattern as MINER-TEST)
    // sha256() is single-SHA; two calls = double-SHA
    sha256_hash_t ref_first = {0};
    sha256_hash_t ref_second = {0};
    sha256(&ref_first, (uint8_t *)&hb, 80);
    sha256(&ref_second, ref_first.bytes, 32);

    result->correctness_ok = (memcmp(secondsha.bytes, ref_second.bytes, 32) == 0);
}

/*
 * Batch timing: measure many hashes with varied nonces
 * Reports average cycles and throughput
 */
struct batch_timing_t {
    uint32_t num_hashes;
    uint64_t total_cycles;
    uint32_t avg_cycles_per_hash;
    float avg_us_per_hash;
    float throughput_khs;
    uint32_t cycles_min;
    uint32_t cycles_max;
    uint32_t correctness_failures;
};

static void benchmark_batch_legacy(uint32_t count, batch_timing_t *result) {
    block_header_t hb = {0};
    sha256_hash_t midstate = {0};
    sha256_hash_t firstsha = {0};
    sha256_hash_t secondsha = {0};
    uint32_t nonce = 0x00000000;

    memcpy((uint8_t *)&hb, TEST_HEADER_BYTES, 80);

    // Warmup
    for (int i = 0; i < 100; i++) {
        hb.nonce = i;
        miner_sha256_midstate(&midstate, &hb);
        miner_sha256_complete_from_midstate(&midstate, &hb, &firstsha, &secondsha);
    }

    result->num_hashes = count;
    result->total_cycles = 0;
    result->cycles_min = UINT32_MAX;
    result->cycles_max = 0;
    result->correctness_failures = 0;

    // Batch measurement: measure each hash individually for detailed stats
    for (uint32_t i = 0; i < count; i++) {
        hb.nonce = nonce++;
        miner_sha256_midstate(&midstate, &hb);

        uint32_t t_start = get_cpu_cycles();
        miner_sha256_complete_from_midstate(&midstate, &hb, &firstsha, &secondsha);
        uint32_t t_end = get_cpu_cycles();

        uint32_t delta = delta_cycles(t_start, t_end);
        result->total_cycles += delta;

        if (delta < result->cycles_min) result->cycles_min = delta;
        if (delta > result->cycles_max) result->cycles_max = delta;
    }

    // Correctness sampling (outside timed loop): compare against direct double-SHA.
    result->correctness_failures = 0;
    for (uint32_t i = 0; i < 64; i++) {
        block_header_t hv = hb;
        hv.nonce = i;

        sha256_hash_t ms = {0};
        sha256_hash_t first = {0};
        sha256_hash_t second = {0};
        sha256_hash_t ref_first = {0};
        sha256_hash_t ref_second = {0};

        miner_sha256_midstate(&ms, &hv);
        miner_sha256_complete_from_midstate(&ms, &hv, &first, &second);

        sha256(&ref_first, (uint8_t *)&hv, 80);
        sha256(&ref_second, ref_first.bytes, 32);

        if (memcmp(second.bytes, ref_second.bytes, 32) != 0) {
            result->correctness_failures++;
        }
    }

    result->avg_cycles_per_hash = result->total_cycles / count;
    result->avg_us_per_hash = (float)result->avg_cycles_per_hash / 240.0f;  // 240 MHz
    result->throughput_khs = 1000.0f / result->avg_us_per_hash;
}

static void benchmark_batch_prepared(uint32_t count, batch_timing_t *result) {
    block_header_t hb = {0};
    sha256_hash_t midstate = {0};
    sha256_hash_t firstsha = {0};
    sha256_hash_t secondsha = {0};
    sha256_tail_schedule_cache_t tail_cache = {0};
    uint32_t nonce = 0x00000000;

    memcpy((uint8_t *)&hb, TEST_HEADER_BYTES, 80);

    miner_sha256_midstate(&midstate, &hb);
    miner_sha256_prepare_tail_schedule(&tail_cache, &hb);

    for (int i = 0; i < 100; i++) {
        miner_sha256_complete_from_midstate_prepared(&midstate, &tail_cache, hb.nonce + i, &firstsha, &secondsha);
    }

    result->num_hashes = count;
    result->total_cycles = 0;
    result->cycles_min = UINT32_MAX;
    result->cycles_max = 0;
    result->correctness_failures = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t t_start = get_cpu_cycles();
        miner_sha256_complete_from_midstate_prepared(&midstate, &tail_cache, nonce++, &firstsha, &secondsha);
        uint32_t t_end = get_cpu_cycles();

        uint32_t delta = delta_cycles(t_start, t_end);
        result->total_cycles += delta;

        if (delta < result->cycles_min) result->cycles_min = delta;
        if (delta > result->cycles_max) result->cycles_max = delta;
    }

    // Correctness sampling (outside timed loop): compare prepared path to direct double-SHA.
    result->correctness_failures = 0;
    for (uint32_t i = 0; i < 64; i++) {
        block_header_t hv = hb;
        hv.nonce = i;

        sha256_hash_t ms = {0};
        sha256_hash_t first = {0};
        sha256_hash_t second = {0};
        sha256_hash_t ref_first = {0};
        sha256_hash_t ref_second = {0};
        sha256_tail_schedule_cache_t vc = {0};

        miner_sha256_midstate(&ms, &hv);
        miner_sha256_prepare_tail_schedule(&vc, &hv);
        miner_sha256_complete_from_midstate_prepared(&ms, &vc, hv.nonce, &first, &second);

        sha256(&ref_first, (uint8_t *)&hv, 80);
        sha256(&ref_second, ref_first.bytes, 32);

        if (memcmp(second.bytes, ref_second.bytes, 32) != 0) {
            result->correctness_failures++;
        }
    }

    result->avg_cycles_per_hash = result->total_cycles / count;
    result->avg_us_per_hash = (float)result->avg_cycles_per_hash / 240.0f;
    result->throughput_khs = 1000.0f / result->avg_us_per_hash;
}

/*
 * Dual-core simulation: measure per-core performance
 * Simulates mining loop with yield intervals
 */
struct dualcore_timing_t {
    uint32_t hashes_per_core;
    uint64_t cycles_core0;
    uint64_t cycles_core1;
    uint32_t avg_cycles_core0;
    uint32_t avg_cycles_core1;
    float khs_core0;
    float khs_core1;
    float khs_total;
};

static void benchmark_dualcore(uint32_t hashes_per_core, dualcore_timing_t *result) {
    block_header_t hb = {0};
    sha256_hash_t midstate = {0};
    sha256_hash_t firstsha = {0};
    sha256_hash_t secondsha = {0};

    memcpy((uint8_t *)&hb, TEST_HEADER_BYTES, 80);
    miner_sha256_midstate(&midstate, &hb);

    // Warmup
    for (int i = 0; i < 100; i++) {
        hb.nonce = (uint32_t)i;
        miner_sha256_complete_from_midstate(&midstate, &hb, &firstsha, &secondsha);
    }

    result->hashes_per_core = hashes_per_core;
    result->cycles_core0 = 0;
    result->cycles_core1 = 0;

    // Core 0 simulation: nonces 0x00000000 - 0x7FFFFFFF
    uint32_t nonce = 0x00000000;
    for (uint32_t i = 0; i < hashes_per_core; i++) {
        hb.nonce = nonce++;
        uint32_t t_start = get_cpu_cycles();
        miner_sha256_complete_from_midstate(&midstate, &hb, &firstsha, &secondsha);
        uint32_t t_end = get_cpu_cycles();

        result->cycles_core0 += delta_cycles(t_start, t_end);
    }

    // Core 1 simulation: nonces 0x80000000 - 0xFFFFFFFF
    nonce = 0x80000000;
    for (uint32_t i = 0; i < hashes_per_core; i++) {
        hb.nonce = nonce++;
        uint32_t t_start = get_cpu_cycles();
        miner_sha256_complete_from_midstate(&midstate, &hb, &firstsha, &secondsha);
        uint32_t t_end = get_cpu_cycles();

        result->cycles_core1 += delta_cycles(t_start, t_end);
    }

    result->avg_cycles_core0 = result->cycles_core0 / hashes_per_core;
    result->avg_cycles_core1 = result->cycles_core1 / hashes_per_core;
    result->khs_core0 = (240.0f * 1000.0f) / (float)result->avg_cycles_core0;
    result->khs_core1 = (240.0f * 1000.0f) / (float)result->avg_cycles_core1;
    result->khs_total = result->khs_core0 + result->khs_core1;
}

// ============================================================
// Public API: Run all benchmarks
// ============================================================

void baseline_benchmark_run() {
    Serial.println("\n========================================");
    Serial.println("SparkMiner Baseline Performance Benchmark");
    Serial.println("========================================\n");

    // Test 1: Single hash deterministic vector
    Serial.println("[TEST 1] Single Hash Timing");
    Serial.println("-------------------------------");
    hash_timing_t single_timing = {0};
    benchmark_single_hash(&single_timing);

    Serial.printf("  Midstate:     %u cycles\n", single_timing.cycles_midstate);
    Serial.printf("  Complete SHA: %u cycles (%.1f μs)\n",
                  single_timing.cycles_complete,
                  (float)single_timing.cycles_complete / 240.0f);
    Serial.printf("  Total:        %u cycles (%.1f μs)\n",
                  single_timing.cycles_total,
                  (float)single_timing.cycles_total / 240.0f);
    Serial.printf("  Correctness:  %s (test vector validation)\n",
                  single_timing.correctness_ok ? "PASS" : "FAIL");
    Serial.println();

    // Test 2: Batch timing (10k hashes)
    Serial.println("[TEST 2] Batch Timing (10,000 hashes)");
    Serial.println("-------------------------------");
    batch_timing_t batch_legacy = {0};
    benchmark_batch_legacy(10000, &batch_legacy);

    Serial.printf("  Legacy  min/max/avg: %u / %u / %u cycles/hash (%.1f KH/s)\n",
                  batch_legacy.cycles_min,
                  batch_legacy.cycles_max,
                  batch_legacy.avg_cycles_per_hash,
                  batch_legacy.throughput_khs);
    Serial.printf("  Legacy  correctness: %s (failures=%u/64)\n",
                  batch_legacy.correctness_failures == 0 ? "PASS" : "FAIL",
                  batch_legacy.correctness_failures);
    Serial.println();

    // Test 3: Dual-core simulation
    Serial.println("[TEST 3] Dual-Core Simulation (5,000 hashes/core)");
    Serial.println("-------------------------------");
    dualcore_timing_t dualcore_timing = {0};
    benchmark_dualcore(5000, &dualcore_timing);

    Serial.printf("  Core 0:\n");
    Serial.printf("    Avg cycles:  %u cycles/hash (%.1f μs/hash)\n",
                  dualcore_timing.avg_cycles_core0,
                  (float)dualcore_timing.avg_cycles_core0 / 240.0f);
    Serial.printf("    Throughput:  %.1f KH/s\n", dualcore_timing.khs_core0);
    Serial.printf("\n  Core 1:\n");
    Serial.printf("    Avg cycles:  %u cycles/hash (%.1f μs/hash)\n",
                  dualcore_timing.avg_cycles_core1,
                  (float)dualcore_timing.avg_cycles_core1 / 240.0f);
    Serial.printf("    Throughput:  %.1f KH/s\n", dualcore_timing.khs_core1);
    Serial.printf("\n  Total (simulated dual-core):\n");
    Serial.printf("    Combined:    %.1f KH/s\n", dualcore_timing.khs_total);
    Serial.println();

    // Summary & targets
    Serial.println("========================================");
    Serial.println("BASELINE SUMMARY");
    Serial.println("========================================");
    Serial.printf("Legacy per-hash:     %u cycles (%.1f μs)\n",
                  batch_legacy.avg_cycles_per_hash,
                  batch_legacy.avg_us_per_hash);
    Serial.printf("Legacy throughput:   %.1f KH/s per core (%.1f total)\n",
                  batch_legacy.throughput_khs,
                  batch_legacy.throughput_khs * 2.0f);
    Serial.println();
    Serial.println("TARGET AFTER OPTIMIZATIONS:");
    Serial.printf("OPT #1 (+15%%):  %.1f KH/s total (estimated)\n",
                  batch_legacy.throughput_khs * 2.0f * 1.15f);
    Serial.printf("OPT #1+#2 (+30%%): %.1f KH/s total (estimated)\n",
                  batch_legacy.throughput_khs * 2.0f * 1.30f);
    Serial.printf("OPT #1+#2+#3 (+45%%): %.1f KH/s total (estimated)\n",
                  batch_legacy.throughput_khs * 2.0f * 1.45f);
    Serial.println();
    Serial.println("Benchmark Complete!\n");
}

#ifdef __cplusplus
}
#endif

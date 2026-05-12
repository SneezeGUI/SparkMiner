/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright Theodore Robert Campbell Jr <trcjr@stupidfoot.com>
 *
 * SparkMiner - Baseline Performance Benchmark Header
 *
 * Public API for cycle-level performance measurement
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run complete baseline benchmark suite
 * Outputs to Serial console with detailed cycle and throughput metrics
 *
 * Tests performed:
 * 1. Single hash deterministic vector (correctness validation)
 * 2. Batch timing over 10,000 hashes (average performance)
 * 3. Dual-core simulation (per-core and combined throughput)
 */
void baseline_benchmark_run();

#ifdef __cplusplus
}
#endif

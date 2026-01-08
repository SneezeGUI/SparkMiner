/*
 * SparkMiner - Live Stats API Client
 * Fetches BTC price, network stats from public APIs
 *
 * APIs used:
 * - CoinGecko: BTC price (HTTPS - requires proxy or enableHttpsStats)
 * - Mempool.space: Block height, hashrate, difficulty, fees (HTTP - always works)
 * - Public Pool: Worker stats (HTTPS - requires proxy or enableHttpsStats)
 *
 * Proxy Support:
 * - Configure statsProxyUrl in settings to route HTTPS APIs through HTTP proxy
 * - Supports authenticated proxies: http://user:pass@host:port
 * - Supports rotating proxies (IP changes handled transparently)
 * - Health monitoring: auto-disables on failures, re-enables when healthy
 */

#ifndef LIVE_STATS_H
#define LIVE_STATS_H

#include <Arduino.h>

// ============================================================
// API URLs
// ============================================================

// HTTP APIs (always used - no SSL overhead)
#define API_BLOCK_HEIGHT    "http://mempool.space/api/blocks/tip/height"
#define API_HASHRATE        "http://mempool.space/api/v1/mining/hashrate/3d"
#define API_DIFFICULTY      "http://mempool.space/api/v1/difficulty-adjustment"
#define API_FEES            "http://mempool.space/api/v1/fees/recommended"

// HTTPS APIs (only used if proxy configured OR enableHttpsStats=true)
#define API_BTC_PRICE       "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd"
#define API_PUBLIC_POOL     "https://public-pool.io:40557/api/client/"

// ============================================================
// Update Intervals
// ============================================================
#define UPDATE_PRICE_MS     300000  // 5 minutes
#define UPDATE_BLOCK_MS     120000  // 2 minutes
#define UPDATE_NETWORK_MS   300000  // 5 minutes
#define UPDATE_FEES_MS      300000  // 5 minutes
#define UPDATE_POOL_MS      120000  // 2 minutes

// Proxy health check interval (when unhealthy)
#define PROXY_HEALTH_CHECK_MS  300000  // 5 minutes
#define PROXY_MAX_FAILURES     3       // Failures before marking unhealthy

// Live stats data structure
// NOTE: Using fixed char arrays instead of Arduino String to prevent heap fragmentation
typedef struct {
    // BTC Price
    float btcPriceUsd;
    uint32_t priceTimestamp;

    // Block info
    uint32_t blockHeight;
    uint32_t blockTimestamp;

    // Network stats (fixed char arrays - no heap allocation)
    char networkHashrate[24];   // Human readable (e.g., "450 EH/s")
    double networkHashrateRaw;  // Raw value in H/s
    char networkDifficulty[24]; // Human readable
    double difficultyRaw;
    float difficultyProgress;   // Progress to next adjustment (0-100)
    int32_t difficultyChange;   // Expected change percentage

    // Fees
    int fastestFee;
    int halfHourFee;
    int hourFee;
    int economyFee;
    int minimumFee;
    uint32_t feesTimestamp;

    // Pool stats (if using public-pool.io)
    int poolWorkersCount;
    char poolTotalHashrate[24];   // Fixed char array
    char poolBestDifficulty[24];  // Fixed char array

    // Status flags
    bool priceValid;
    bool blockValid;
    bool networkValid;
    bool feesValid;
    bool poolValid;
} live_stats_t;

/**
 * Initialize live stats module
 */
void live_stats_init();

/**
 * Update all stats (call periodically)
 * Only fetches data if update interval has passed
 */
void live_stats_update();

/**
 * Force update of all stats
 */
void live_stats_force_update();

/**
 * Get current live stats
 */
const live_stats_t* live_stats_get();

/**
 * Set wallet address for pool stats
 */
void live_stats_set_wallet(const char *wallet);

/**
 * FreeRTOS task for background updates
 */
void live_stats_task(void *param);

#endif // LIVE_STATS_H

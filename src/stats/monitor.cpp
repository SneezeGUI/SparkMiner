/*
 * SparkMiner - Monitor Task Implementation
 * Coordinates display updates and live stats fetching
 */

#include <Arduino.h>
#include <WiFi.h>
#include <board_config.h>
#include "monitor.h"
#include "live_stats.h"
#include "../display/display.h"
#include "../mining/miner.h"
#include "../stratum/stratum.h"
#include "../config/nvs_config.h"
#include "../config/wifi_manager.h"

// Update intervals
#define DISPLAY_UPDATE_MS   1000   // 1 second
#define STATS_UPDATE_MS     10000  // 10 seconds

static bool s_initialized = false;
static uint32_t s_lastDisplayUpdate = 0;
static uint32_t s_lastStatsUpdate = 0;
static uint32_t s_startTime = 0;

// ============================================================
// Helper Functions
// ============================================================

static void updateDisplayData(display_data_t *data) {
    // Get mining stats
    mining_stats_t *mstats = miner_get_stats();

    data->totalHashes = mstats->hashes;
    data->bestDifficulty = mstats->bestDifficulty;
    data->sharesAccepted = mstats->accepted;
    data->sharesRejected = mstats->rejected;
    data->templates = mstats->templates;
    data->blocks32 = mstats->matches32;
    data->blocksFound = mstats->blocks;
    data->uptimeSeconds = (millis() - s_startTime) / 1000;
    data->avgLatency = mstats->avgLatency;

    // Calculate hashrate with EMA smoothing
    static uint64_t lastHashes = 0;
    static uint32_t lastHashTime = 0;
    static double smoothedHashRate = 0.0;
    static bool firstSample = true;

    uint32_t now = millis();
    uint32_t elapsed = now - lastHashTime;

    if (elapsed >= 1000) {
        uint64_t deltaHashes = mstats->hashes - lastHashes;
        double instantRate = (double)deltaHashes * 1000.0 / elapsed;

        // Exponential moving average (alpha=0.15 for smooth but responsive updates)
        // Lower alpha = smoother but slower to respond
        // Higher alpha = more responsive but jumpier
        const double alpha = 0.15;

        if (firstSample) {
            smoothedHashRate = instantRate;
            firstSample = false;
        } else {
            smoothedHashRate = alpha * instantRate + (1.0 - alpha) * smoothedHashRate;
        }

        data->hashRate = smoothedHashRate;
        lastHashes = mstats->hashes;
        lastHashTime = now;
    }

    // Pool info
    data->poolConnected = stratum_is_connected();
    data->poolName = stratum_get_pool();

    // Get pool difficulty from miner
    // TODO: Get actual pool difficulty
    data->poolDifficulty = 0.0014;  // Default

    // Network info
    data->wifiConnected = (WiFi.status() == WL_CONNECTED);
    data->ipAddress = wifi_manager_get_ip();

    // Live stats
    const live_stats_t *lstats = live_stats_get();

    if (lstats->priceValid) {
        data->btcPrice = lstats->btcPriceUsd;
    }
    if (lstats->blockValid) {
        data->blockHeight = lstats->blockHeight;
    }
    if (lstats->networkValid) {
        data->networkHashrate = lstats->networkHashrate;
        data->networkDifficulty = lstats->networkDifficulty;
    }
    if (lstats->feesValid) {
        data->halfHourFee = lstats->halfHourFee;
    }

    // Pool stats (from API)
    if (lstats->poolValid) {
        data->poolWorkersTotal = lstats->poolWorkersCount;
        data->poolHashrate = lstats->poolTotalHashrate;
        data->addressBestDiff = lstats->poolBestDifficulty;
        // poolWorkersAddress would need separate API call for per-address count
        data->poolWorkersAddress = 1;  // Current device counts as 1
    }
}

// ============================================================
// Public API
// ============================================================

void monitor_init() {
    if (s_initialized) return;

    // Initialize live stats
    live_stats_init();

    // Set wallet for pool stats
    miner_config_t *config = nvs_config_get();
    if (config->wallet[0]) {
        live_stats_set_wallet(config->wallet);
    }

    // Note: display_init() is now called earlier in main.cpp
    // (before WiFi setup, so we can show AP config screen)

    s_startTime = millis();
    s_initialized = true;

    Serial.println("[MONITOR] Initialized");
}

void monitor_task(void *param) {
    Serial.printf("[MONITOR] Task started on core %d\n", xPortGetCoreID());

    if (!s_initialized) {
        monitor_init();
    }

    display_data_t displayData;
    memset(&displayData, 0, sizeof(displayData));

    while (true) {
        uint32_t now = millis();

        // Update live stats periodically
        if (now - s_lastStatsUpdate >= STATS_UPDATE_MS) {
            live_stats_update();
            s_lastStatsUpdate = now;
        }

        // Update display
        if (now - s_lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
            updateDisplayData(&displayData);

            #if USE_DISPLAY
                display_update(&displayData);

                // Check for touch input
                if (display_touched()) {
                    display_handle_touch();
                }
            #endif

            // Also print to serial for headless/debug
            static uint32_t lastSerialPrint = 0;
            if (now - lastSerialPrint >= 10000) {
                Serial.printf("[STATS] Hashrate: %.2f H/s | Shares: %u/%u | Ping: %u ms | Best: %.4f\n",
                    displayData.hashRate,
                    displayData.sharesAccepted,
                    displayData.sharesAccepted + displayData.sharesRejected,
                    displayData.avgLatency,
                    displayData.bestDifficulty);

                if (displayData.btcPrice > 0) {
                    Serial.printf("[STATS] BTC: $%.0f | Block: %u | Fee: %d sat/vB\n",
                        displayData.btcPrice,
                        displayData.blockHeight,
                        displayData.halfHourFee);
                }

                lastSerialPrint = now;
            }

            s_lastDisplayUpdate = now;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

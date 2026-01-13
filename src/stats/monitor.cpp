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
#include "../display/led_status.h"
#include "../mining/miner.h"
#include "../stratum/stratum.h"
#include "../config/nvs_config.h"
#include "../config/wifi_manager.h"

// Update intervals
#define DISPLAY_UPDATE_MS   1000    // 1 second
#define STATS_UPDATE_MS     10000   // 10 seconds
#define PERSIST_STATS_MS    3600000 // 1 hour - save to flash for persistence
#define EARLY_SAVE_MS       300000  // 5 minutes - initial save interval before first hourly
#define LED_UPDATE_MS       50      // 50ms for smooth LED animations

static bool s_initialized = false;
static uint32_t s_lastDisplayUpdate = 0;
static uint32_t s_lastStatsUpdate = 0;
static uint32_t s_lastPersistSave = 0;
static uint32_t s_lastLedUpdate = 0;
static uint32_t s_startTime = 0;
static bool s_earlySaveDone = false;      // Track if we've done the early save
static uint32_t s_lastAcceptedCount = 0;  // Track shares for first-share save
static uint32_t s_lastLedShareCount = 0;  // Track shares for LED flash

// Track session start values to calculate deltas for persistence
static uint64_t s_sessionStartHashes = 0;
static uint32_t s_sessionStartShares = 0;
static uint32_t s_sessionStartAccepted = 0;
static uint32_t s_sessionStartRejected = 0;
static uint32_t s_sessionStartBlocks = 0;

// ============================================================
// Helper Functions
// ============================================================

static void updateDisplayData(display_data_t *data) {
    // Get mining stats (current session)
    mining_stats_t *mstats = miner_get_stats();

    // Get persistent lifetime stats
    mining_persistence_t *pstats = nvs_stats_get();

    // Display lifetime totals (persistent + current session)
    data->totalHashes = pstats->lifetimeHashes + mstats->hashes;
    data->sharesAccepted = pstats->lifetimeAccepted + mstats->accepted;
    data->sharesRejected = pstats->lifetimeRejected + mstats->rejected;
    data->blocksFound = pstats->lifetimeBlocks + mstats->blocks;

    // Best difficulty: max of lifetime best and current session best
    data->bestDifficulty = (mstats->bestDifficulty > pstats->bestDifficultyEver)
                           ? mstats->bestDifficulty : pstats->bestDifficultyEver;

    // Session-only values (these make sense per-session)
    data->templates = mstats->templates;
    data->blocks32 = mstats->matches32;
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
    data->poolDifficulty = miner_get_difficulty();

    // Network info
    data->wifiConnected = (WiFi.status() == WL_CONNECTED);
    data->wifiRssi = data->wifiConnected ? WiFi.RSSI() : 0;
    data->ipAddress = wifi_manager_get_ip();

    // Live stats (thread-safe copy)
    live_stats_t lstats;
    live_stats_get_copy(&lstats);

    // Overwrite pool name if proxy provides a friendly one
    if (lstats.poolValid && strlen(lstats.poolName) > 0) {
        data->poolName = lstats.poolName;
    }

    if (lstats.poolValid) {
        data->poolFailovers = lstats.failovers;
    }
    
    // Also count internal backup failover as a failover event
    if (stratum_is_backup()) {
        data->poolFailovers++;
        // Ensure it's at least 1 if we are on backup, even if proxy says 0
        if (data->poolFailovers == 0) data->poolFailovers = 1;
    }

    if (lstats.priceValid) {
        data->btcPrice = lstats.btcPriceUsd;
    }
    if (lstats.blockValid) {
        data->blockHeight = lstats.blockHeight;
    }
    if (lstats.networkValid) {
        // Use strncpy for fixed char arrays (no heap allocation)
        strncpy(data->networkHashrate, lstats.networkHashrate, sizeof(data->networkHashrate) - 1);
        data->networkHashrate[sizeof(data->networkHashrate) - 1] = '\0';
        strncpy(data->networkDifficulty, lstats.networkDifficulty, sizeof(data->networkDifficulty) - 1);
        data->networkDifficulty[sizeof(data->networkDifficulty) - 1] = '\0';
    }
    if (lstats.feesValid) {
        data->halfHourFee = lstats.halfHourFee;
    }

    // Pool stats (from API)
    if (lstats.poolValid) {
        data->poolWorkersTotal = lstats.poolWorkersCount;
        // Use strncpy for fixed char arrays (no heap allocation)
        strncpy(data->poolHashrate, lstats.poolTotalHashrate, sizeof(data->poolHashrate) - 1);
        data->poolHashrate[sizeof(data->poolHashrate) - 1] = '\0';
        strncpy(data->addressBestDiff, lstats.poolBestDifficulty, sizeof(data->addressBestDiff) - 1);
        data->addressBestDiff[sizeof(data->addressBestDiff) - 1] = '\0';
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

    // Initialize LED status driver (for headless builds with RGB LED)
    #ifdef USE_LED_STATUS
        led_status_init();
        led_status_set(LED_STATUS_CONNECTING);
    #endif

    // Load persistent stats from NVS (initializes session count)
    mining_persistence_t *pstats = nvs_stats_get();
    Serial.printf("[MONITOR] Session #%lu | Lifetime: %llu hashes, %lu shares\n",
                  pstats->sessionCount, pstats->lifetimeHashes, pstats->lifetimeShares);

    // Set wallet for pool stats
    miner_config_t *config = nvs_config_get();
    if (config->wallet[0]) {
        live_stats_set_wallet(config->wallet);
    }

    // Note: display_init() is now called earlier in main.cpp
    // (before WiFi setup, so we can show AP config screen)

    s_startTime = millis();
    s_lastPersistSave = millis();
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
                
                if (displayData.poolName) {
                    Serial.printf("[STATS] Pool: %s (%d workers) %s\n", 
                        displayData.poolName, 
                        displayData.poolWorkersTotal,
                        (displayData.poolFailovers > 0) ? "[FAILOVER]" : "");
                }

                if (displayData.btcPrice > 0) {
                    Serial.printf("[STATS] BTC: $%.0f | Block: %u | Fee: %d sat/vB\n",
                        displayData.btcPrice,
                        displayData.blockHeight,
                        displayData.halfHourFee);
                }

                // DEBUG: Per-core hash contribution
                extern volatile uint64_t s_core0Hashes;
                extern volatile uint64_t s_core1Hashes;
                mining_stats_t *debugStats = miner_get_stats();
                uint64_t core1Est = debugStats->hashes - s_core0Hashes;
                Serial.printf("[DEBUG] Core0: %llu | Core1: %llu | Total: %llu\n",
                    s_core0Hashes, core1Est, debugStats->hashes);

                // Heap monitoring - track memory usage over time
                uint32_t freeHeap = ESP.getFreeHeap();
                uint32_t minFreeHeap = ESP.getMinFreeHeap();
                uint32_t maxAllocHeap = ESP.getMaxAllocHeap();

                // Always log heap stats for debugging memory leaks
                Serial.printf("[HEAP] Free: %lu | Min: %lu | MaxAlloc: %lu\n",
                    freeHeap, minFreeHeap, maxAllocHeap);

                // Warn if critically low (under 30KB)
                if (freeHeap < 30000) {
                    Serial.println("[HEAP] CRITICAL: Memory very low - may crash soon!");
                } else if (freeHeap < 50000) {
                    Serial.println("[HEAP] WARNING: Memory getting low");
                }

                lastSerialPrint = now;
            }

            s_lastDisplayUpdate = now;
        }

        // Update LED status for headless builds
        #ifdef USE_LED_STATUS
        if (now - s_lastLedUpdate >= LED_UPDATE_MS) {
            // Determine current status based on connection state
            if (!displayData.wifiConnected) {
                // Check if in AP mode
                if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
                    led_status_set(LED_STATUS_AP_MODE);
                } else {
                    led_status_set(LED_STATUS_CONNECTING);
                }
            } else if (!displayData.poolConnected) {
                led_status_set(LED_STATUS_CONNECTING);
            } else if (displayData.hashRate > 0) {
                led_status_set(LED_STATUS_MINING);
            }

            // Check for new share accepted - flash LED
            mining_stats_t *ledStats = miner_get_stats();
            if (ledStats->accepted > s_lastLedShareCount) {
                led_status_share_found();
                s_lastLedShareCount = ledStats->accepted;
            }

            // Check for block found - celebration!
            if (ledStats->blocks > 0) {
                static uint32_t lastBlockCount = 0;
                if (ledStats->blocks > lastBlockCount) {
                    led_status_block_found();
                    lastBlockCount = ledStats->blocks;
                }
            }

            // Update LED animation
            led_status_update();
            s_lastLedUpdate = now;
        }
        #endif

        // Persistence save logic with early save for new sessions
        // - Save on first accepted share (immediate feedback)
        // - Save every 5 minutes until first hourly save
        // - Save every hour after that (flash wear-leveling)
        mining_stats_t *mstats = miner_get_stats();
        bool shouldSave = false;
        const char *saveReason = nullptr;

        // Check for first share save (one-time trigger)
        if (!s_earlySaveDone && mstats->accepted > 0 && s_lastAcceptedCount == 0) {
            shouldSave = true;
            saveReason = "first share";
            s_lastAcceptedCount = mstats->accepted;
        }

        // Check for periodic save (early interval or standard interval)
        uint32_t saveInterval = s_earlySaveDone ? PERSIST_STATS_MS : EARLY_SAVE_MS;
        if (now - s_lastPersistSave >= saveInterval) {
            shouldSave = true;
            saveReason = s_earlySaveDone ? "hourly" : "early";
            if (!s_earlySaveDone) {
                s_earlySaveDone = true;  // Switch to hourly saves after first early save
            }
        }

        if (shouldSave) {
            uint32_t sessionSeconds = (now - s_startTime) / 1000;

            // Calculate session deltas (hashes added since last save)
            uint64_t sessionHashes = mstats->hashes - s_sessionStartHashes;
            uint32_t sessionShares = mstats->shares - s_sessionStartShares;
            uint32_t sessionAccepted = mstats->accepted - s_sessionStartAccepted;
            uint32_t sessionRejected = mstats->rejected - s_sessionStartRejected;
            uint32_t sessionBlocks = mstats->blocks - s_sessionStartBlocks;

            // Update persistent stats
            nvs_stats_update(sessionHashes, sessionShares, sessionAccepted,
                            sessionRejected, sessionBlocks, sessionSeconds,
                            mstats->bestDifficulty);

            // Update session start values for next delta calculation
            s_sessionStartHashes = mstats->hashes;
            s_sessionStartShares = mstats->shares;
            s_sessionStartAccepted = mstats->accepted;
            s_sessionStartRejected = mstats->rejected;
            s_sessionStartBlocks = mstats->blocks;

            Serial.printf("[MONITOR] Stats saved to NVS (%s)\n", saveReason);
            s_lastPersistSave = now;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

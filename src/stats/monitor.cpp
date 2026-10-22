#ifdef ADVANCED_LOCAL_CONTROLS
#include "local_settings.h"
#endif
/*
 * SparkMiner - Monitor Task Implementation
 * Coordinates display updates and live stats fetching
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <advanced_statistics.hpp>
#include <pool_address.hpp>
#include "advanced_web.h"
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
#define LED_UPDATE_MS       50      // 50ms for smooth LED animations

static bool s_initialized = false;
static uint32_t s_lastDisplayUpdate = 0;
static uint32_t s_lastStatsUpdate = 0;
static uint32_t s_lastLedUpdate = 0;
static uint32_t s_lastActivityTime = 0;  // Screen timeout tracking
static uint32_t s_lastLedShareCount = 0;  // Track shares for LED flash

static advanced::Ledger s_ledger;
static advanced::SessionClock s_clock;
static advanced::SavePolicy s_savePolicy;
static advanced::Rate s_rate;
static bool s_lastSaveOk = true, s_saveAttempted = false;
static uint64_t s_sequence = 0;
static uint32_t s_bootId = 0;
static char s_displayPool[64] = {0};

static advanced::Counters sessionCounters(const mining_stats_t& raw, uint64_t seconds) {
    advanced::Counters c;
    c.hashes = raw.hashes; c.submitted = raw.shares;
    c.accepted = raw.accepted; c.rejected = raw.rejected; c.blocks = raw.blocks;
    c.uptime_s = seconds > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(seconds);
    c.best_difficulty = advanced::best(raw.bestDifficulty);
    return c;
}

// ============================================================
// Helper Functions
// ============================================================

static void updateDisplayData(display_data_t *data, const mining_stats_t& raw,
                              const advanced::Snapshot& snapshot) {
    const mining_stats_t* mstats = &raw;
    const auto& total = snapshot.lifetime;
    data->sessionStats = snapshot.session;
    data->lifetimeStats = snapshot.lifetime;
    data->totalHashes = total.hashes;
    data->sharesAccepted = total.accepted;
    data->sharesRejected = total.rejected;
    data->blocksFound = total.blocks;
    data->bestDifficulty = total.best_difficulty;
    data->sessionSuccessPercent = advanced::success_percent(snapshot.session);
    data->totalSuccessPercent = advanced::success_percent(total);
    data->templates = mstats->templates;
    data->blocks32 = mstats->matches32;
    data->uptimeSeconds = snapshot.session.uptime_s;
    data->avgLatency = mstats->avgLatency;
    data->hashRate = snapshot.hashrate_hs;

    // Pool info
    data->poolConnected = snapshot.pool_connected;
    const char* activePool = stratum_get_pool();
    if (!activePool || !activePool[0]) activePool = advanced::tcp_pool_host(nvs_config_get()->poolUrl);
    strncpy(s_displayPool, activePool, sizeof(s_displayPool)-1);
    s_displayPool[sizeof(s_displayPool)-1] = '\0';
    data->poolName = s_displayPool;

    // Get pool difficulty from miner
    data->poolDifficulty = miner_get_difficulty();

    // Network info
    data->wifiConnected = snapshot.wifi_connected;
    data->wifiRssi = data->wifiConnected ? WiFi.RSSI() : 0;
    data->ipAddress = wifi_manager_get_ip();

    // Live stats (thread-safe copy)
    live_stats_t lstats;
    live_stats_get_copy(&lstats);

    // Overwrite pool name if proxy provides a friendly one
    if (lstats.poolValid && strlen(lstats.poolName) > 0) {
        strncpy(s_displayPool, lstats.poolName, sizeof(s_displayPool)-1);
        s_displayPool[sizeof(s_displayPool)-1] = '\0';
        data->poolName = s_displayPool;
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
        
        data->difficultyProgress = lstats.difficultyProgress;
        data->difficultyChange = lstats.difficultyChange;
        data->difficultyRetargetBlocks = lstats.difficultyRetargetBlocks;
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
        strncpy(data->workerHashrate, lstats.workerHashrate, sizeof(data->workerHashrate) - 1);
        data->workerHashrate[sizeof(data->workerHashrate) - 1] = '\0';
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
    advanced::Counters boot;
    boot.hashes = pstats->lifetimeHashes; boot.submitted = pstats->lifetimeShares;
    boot.accepted = pstats->lifetimeAccepted; boot.rejected = pstats->lifetimeRejected;
    boot.blocks = pstats->lifetimeBlocks; boot.uptime_s = pstats->totalUptimeSeconds;
    boot.best_difficulty = pstats->bestDifficultyEver;
    s_ledger = advanced::Ledger(boot);
    s_clock.start(millis());
    s_bootId = esp_random();
    mining_stats_t initial;
    miner_get_stats_copy(&initial);
    s_rate.sample(initial.hashes, static_cast<uint64_t>(esp_timer_get_time()) / 1000);


    // Set wallet for pool stats
    miner_config_t *config = nvs_config_get();
    if (config->wallet[0]) {
        live_stats_set_wallet(config->wallet);
    }

    // Note: display_init() is now called earlier in main.cpp
    // (before WiFi setup, so we can show AP config screen)

    s_lastActivityTime = millis();
    s_initialized = true;

    Serial.printf("[MONITOR] Initialized (screen_timeout=%us)\n", config->screenTimeout);
}

void monitor_reset_activity() {
    s_lastActivityTime = millis();
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
        uint64_t sessionSeconds = s_clock.tick(now);

        #if (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
        if (display_touched()) {
            monitor_reset_activity();
            display_handle_touch();
        }
        #endif

        // Update live stats periodically
        if (now - s_lastStatsUpdate >= STATS_UPDATE_MS) {
            live_stats_update();
            s_lastStatsUpdate = now;
        }

        // Update display
        if (now - s_lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
            mining_stats_t raw;
            miner_get_stats_copy(&raw);
            // Fetches may have blocked; sample the clock after they finish.
            now = millis();
            sessionSeconds = s_clock.tick(now);
            advanced::Snapshot snapshot;
            snapshot.session = sessionCounters(raw, sessionSeconds);
            snapshot.lifetime = s_ledger.total(snapshot.session);
            snapshot.sampled_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
            snapshot.sequence = ++s_sequence;
            snapshot.boot_id = s_bootId;
            snapshot.hashrate_hs = s_rate.sample(raw.hashes, snapshot.sampled_ms);
            snapshot.pool_connected = stratum_is_connected();
            snapshot.wifi_connected = WiFi.status() == WL_CONNECTED;
            snapshot.save_attempted = s_saveAttempted;
            snapshot.last_save_ok = s_lastSaveOk;
            updateDisplayData(&displayData, raw, snapshot);
            advanced_stats_publish(snapshot);

            #if (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
                display_update(&displayData);



                // Screen timeout check
                {
                    miner_config_t *cfg = nvs_config_get();
                    if (cfg->screenTimeout > 0 && !display_is_backlight_off()) {
                        uint32_t elapsed = now - s_lastActivityTime;
                        uint32_t timeoutMs = (uint32_t)cfg->screenTimeout * 1000;
                        if (elapsed >= timeoutMs) {
                            Serial.printf("[MONITOR] Screen timeout after %lus (timeout=%us)\n",
                                          elapsed / 1000, cfg->screenTimeout);
                            display_set_backlight_off();
                        }
                    }
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
                uint64_t core0, core1;
                miner_get_stats_copy(nullptr, &core0, &core1);
                Serial.printf("[STATS] Core0: %llu hashes, Core1: %llu hashes\n", core0, core1);

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
            mining_stats_t ledCopy;
            miner_get_stats_copy(&ledCopy);
            const mining_stats_t *ledStats = &ledCopy;
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

        #ifdef ADVANCED_LOCAL_CONTROLS
        mining_stats_t settingsRaw;
        miner_get_stats_copy(&settingsRaw);
        local_settings_process(s_ledger.total(sessionCounters(settingsRaw, s_clock.tick(millis()))));
        #endif

        // Idempotent absolute checkpoints. Retry failures at most once per 30 s.
        mining_stats_t current;
        miner_get_stats_copy(&current);
        sessionSeconds = s_clock.tick(millis());
        if (s_savePolicy.due(sessionSeconds, current.accepted)
            #ifdef ADVANCED_LOCAL_CONTROLS
            && !local_settings_blocks_checkpoint()
            #endif
        ) {
            const auto session = sessionCounters(current, sessionSeconds);
            s_saveAttempted = true;
            s_lastSaveOk = s_ledger.checkpoint(session, [](const advanced::Counters& total) {
                return nvs_stats_store_totals(total);
            });
            s_savePolicy.result(sessionSeconds, current.accepted, s_lastSaveOk);
            Serial.printf("[MONITOR] Absolute statistics checkpoint: %s\n",
                          s_lastSaveOk ? "saved" : "failed; retry pending");
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

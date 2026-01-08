/*
 * SparkMiner - NVS Configuration
 * Persistent settings storage using ESP32 NVS
 *
 * Based on BitsyMiner by Justin Williams (GPL v3)
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <Arduino.h>
#include <board_config.h>

/**
 * Miner configuration structure
 * Stored in NVS for persistence across reboots
 */
typedef struct {
    // WiFi settings
    char ssid[MAX_SSID_LENGTH + 1];
    char wifiPassword[MAX_PASSWORD_LEN + 1];

    // Primary pool
    char poolUrl[MAX_POOL_URL_LEN + 1];
    uint16_t poolPort;
    char wallet[MAX_WALLET_LEN + 1];
    char poolPassword[MAX_PASSWORD_LEN + 1];

    // Backup pool
    char backupPoolUrl[MAX_POOL_URL_LEN + 1];
    uint16_t backupPoolPort;
    char backupWallet[MAX_WALLET_LEN + 1];
    char backupPoolPassword[MAX_PASSWORD_LEN + 1];

    // Display settings
    uint8_t brightness;
    uint8_t screenTimeout;
    uint8_t rotation;       // Screen rotation (0-3)
    bool displayEnabled;
    bool invertColors;      // Invert display colors

    // Miner settings
    char workerName[32];
    double targetDifficulty;

    // Stats API settings
    // Proxy URL format: http://[user:pass@]host:port
    // When proxy is set, HTTPS stats are auto-enabled via proxy
    // When proxy is empty and enableHttpsStats=true, direct HTTPS (unstable)
    char statsProxyUrl[128];    // HTTP proxy for stats APIs (supports auth)
    bool enableHttpsStats;      // Manual override for direct HTTPS (default: false)

    // Checksum for validation
    uint32_t checksum;
} miner_config_t;

/**
 * Persistent mining statistics structure
 * Saved periodically to NVS to survive reboots
 * Uses wear-leveling strategy: save every 1 hour or on clean shutdown
 */
#define STATS_MAGIC 0x53544154  // "STAT"

typedef struct __attribute__((packed)) {
    uint64_t lifetimeHashes;    // Total hashes computed across all sessions
    uint32_t lifetimeShares;    // Total shares submitted
    uint32_t lifetimeAccepted;  // Total accepted shares
    uint32_t lifetimeRejected;  // Total rejected shares
    uint32_t lifetimeBlocks;    // Blocks found (the lottery win!)
    uint32_t totalUptimeSeconds;// Total mining uptime
    double bestDifficultyEver;  // Best difficulty ever achieved
    uint32_t sessionCount;      // Number of boot cycles
    uint32_t magic;             // Magic value for validation
    uint32_t checksum;          // Checksum for data integrity
} mining_persistence_t;

/**
 * Initialize NVS configuration subsystem
 */
void nvs_config_init();

/**
 * Load configuration from NVS
 * @param config Pointer to config structure to fill
 * @return true if loaded successfully, false if defaults used
 */
bool nvs_config_load(miner_config_t *config);

/**
 * Save configuration to NVS
 * @param config Pointer to config structure to save
 * @return true if saved successfully
 */
bool nvs_config_save(const miner_config_t *config);

/**
 * Reset configuration to defaults
 * @param config Pointer to config structure to reset
 */
void nvs_config_reset(miner_config_t *config);

/**
 * Get global configuration instance
 */
miner_config_t* nvs_config_get();

/**
 * Check if configuration is valid (has wallet set)
 */
bool nvs_config_is_valid();

// ============================================================
// Persistent Stats API
// ============================================================

/**
 * Load persistent stats from NVS
 * @param stats Pointer to stats structure to fill
 * @return true if loaded successfully, false if no saved stats
 */
bool nvs_stats_load(mining_persistence_t *stats);

/**
 * Save persistent stats to NVS
 * Call sparingly (every 1 hour) to avoid flash wear
 * @param stats Pointer to stats structure to save
 * @return true if saved successfully
 */
bool nvs_stats_save(const mining_persistence_t *stats);

/**
 * Get global persistent stats instance
 */
mining_persistence_t* nvs_stats_get();

/**
 * Update persistent stats from current session
 * Call periodically (every 1 hour) and on shutdown
 * @param currentHashes Hashes from current session
 * @param currentShares Shares from current session
 * @param currentAccepted Accepted from current session
 * @param currentRejected Rejected from current session
 * @param currentBlocks Blocks from current session
 * @param sessionSeconds Uptime of current session
 * @param bestDiff Best difficulty this session
 */
void nvs_stats_update(uint64_t currentHashes, uint32_t currentShares,
                      uint32_t currentAccepted, uint32_t currentRejected,
                      uint32_t currentBlocks, uint32_t sessionSeconds,
                      double bestDiff);

#endif // NVS_CONFIG_H

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

    // Miner settings
    char workerName[32];
    double targetDifficulty;

    // Checksum for validation
    uint32_t checksum;
} miner_config_t;

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

#endif // NVS_CONFIG_H

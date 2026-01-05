/*
 * SparkMiner - NVS Configuration Implementation
 * Persistent settings storage using ESP32 NVS
 *
 * Based on BitsyMiner by Justin Williams (GPL v3)
 */

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <board_config.h>
#include "nvs_config.h"
#include "../stratum/stratum_types.h"

// SD card support - use SD_MMC for ESP32-S3 CYD, SPI SD for others
#ifdef USE_SD_MMC
    #include <SD_MMC.h>
    #define SD_FS SD_MMC
#else
    #include <SD.h>
    #include <SPI.h>
    #define SD_FS SD
    // SD card CS pin for SPI mode
    #ifndef SD_CS_PIN
        #define SD_CS_PIN 5
    #endif
#endif

// Config file path on SD card
#define CONFIG_FILE_PATH "/config.json"

// NVS namespace
#define NVS_NAMESPACE "sparkminer"
#define NVS_KEY_CONFIG "config"

// Magic value for checksum validation
#define CONFIG_MAGIC 0x5350524B  // "SPRK"

static Preferences s_prefs;
static miner_config_t s_config;
static bool s_initialized = false;

// ============================================================
// Utility Functions
// ============================================================

static uint32_t calculateChecksum(const miner_config_t *config) {
    const uint8_t *data = (const uint8_t *)config;
    uint32_t sum = CONFIG_MAGIC;

    // Calculate checksum over all fields except the checksum itself
    size_t len = sizeof(miner_config_t) - sizeof(uint32_t);
    for (size_t i = 0; i < len; i++) {
        sum = sum * 31 + data[i];
    }

    return sum;
}

static void safeStrCpy(char *dest, const char *src, size_t maxLen) {
    if (src) {
        strncpy(dest, src, maxLen - 1);
        dest[maxLen - 1] = '\0';
    } else {
        dest[0] = '\0';
    }
}

/**
 * Load configuration from /config.json file on SD card
 * Returns true if valid config was loaded
 *
 * Config file is NOT deleted - it persists on SD card.
 * It's only read when NVS has no valid config (first boot or reset).
 */
static bool loadConfigFromFile(miner_config_t *config) {
    Serial.println("[CONFIG] Attempting to load config from SD card...");
    
    // Initialize SD card
#ifdef USE_SD_MMC
    // ESP32-S3 FNK0104 uses SD_MMC 4-bit interface (Freenove driver approach)
    Serial.println("[CONFIG] Setting up SD_MMC (Freenove FNK0104)...");
    Serial.print("[CONFIG] SD Pins - CLK:"); Serial.print(SD_MMC_CLK);
        Serial.print(" CMD:"); Serial.print(SD_MMC_CMD);
        Serial.print(" D0:"); Serial.print(SD_MMC_D0);
        Serial.print(" D1:"); Serial.print(SD_MMC_D1);
        Serial.print(" D2:"); Serial.print(SD_MMC_D2);
        Serial.print(" D3:"); Serial.print(SD_MMC_D3);
        Serial.print(" Freq:"); Serial.println(BOARD_MAX_SDMMC_FREQ);
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0, SD_MMC_D1, SD_MMC_D2, SD_MMC_D3);
    
    // Give SD card time to power up
    delay(100);
    
    // Try 4-bit mode first
    Serial.println("[CONFIG] Trying SD_MMC 4-bit mode...");
    if (!SD_MMC.begin("/sdcard", false, false, BOARD_MAX_SDMMC_FREQ, 5)) {
        Serial.println("[CONFIG] 4-bit failed, trying 1-bit mode @ 4MHz...");
        SD_MMC.end();  // Clean up before retry
        delay(100);
        if (!SD_MMC.begin("/sdcard", true, false, 4000, 5)) {
            Serial.println("[CONFIG] 1-bit failed, trying 1-bit @ 1MHz...");
            SD_MMC.end();
            delay(100);
            if (!SD_MMC.begin("/sdcard", true, false, 1000, 5)) {
                Serial.println("[CONFIG] SD_MMC card not found or failed to mount");
                Serial.println("[CONFIG] Check: card inserted? FAT32? contacts clean?");
                return false;
            }
        }
    }
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == 0) {
        Serial.println("[CONFIG] No SD card detected");
        return false;
    }
    Serial.print("[CONFIG] Card type: "); Serial.println(cardType);
    Serial.printf("[CONFIG] SD_MMC Card Size: %lluMB\n", SD_MMC.cardSize() / (1024 * 1024));
    Serial.println("[CONFIG] SD_MMC initialized successfully");
#else
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[CONFIG] SD card not found or failed to mount");
        return false;
    }
#endif

    if (!SD_FS.exists(CONFIG_FILE_PATH)) {
        Serial.println("[CONFIG] No config.json on SD card");
        SD_FS.end();
        return false;
    }

    File file = SD_FS.open(CONFIG_FILE_PATH, "r");
    if (!file) {
        Serial.println("[CONFIG] Failed to open config.json");
        SD_FS.end();
        return false;
    }

    Serial.println("[CONFIG] Found config.json on SD card, loading...");

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    SD_FS.end();

    if (err) {
        Serial.printf("[CONFIG] JSON parse error: %s\n", err.c_str());
        return false;
    }

    // WiFi settings
    if (doc.containsKey("ssid")) {
        safeStrCpy(config->ssid, doc["ssid"], sizeof(config->ssid));
    }
    if (doc.containsKey("wifi_password")) {
        safeStrCpy(config->wifiPassword, doc["wifi_password"], sizeof(config->wifiPassword));
    }

    // Pool settings
    if (doc.containsKey("pool_url")) {
        safeStrCpy(config->poolUrl, doc["pool_url"], sizeof(config->poolUrl));
    }
    if (doc.containsKey("pool_port")) {
        config->poolPort = doc["pool_port"];
    }
    if (doc.containsKey("wallet")) {
        safeStrCpy(config->wallet, doc["wallet"], sizeof(config->wallet));
    }
    if (doc.containsKey("pool_password")) {
        safeStrCpy(config->poolPassword, doc["pool_password"], sizeof(config->poolPassword));
    }
    if (doc.containsKey("worker_name")) {
        safeStrCpy(config->workerName, doc["worker_name"], sizeof(config->workerName));
    }

    // Backup pool (optional)
    if (doc.containsKey("backup_pool_url")) {
        safeStrCpy(config->backupPoolUrl, doc["backup_pool_url"], sizeof(config->backupPoolUrl));
    }
    if (doc.containsKey("backup_pool_port")) {
        config->backupPoolPort = doc["backup_pool_port"];
    }
    if (doc.containsKey("backup_wallet")) {
        safeStrCpy(config->backupWallet, doc["backup_wallet"], sizeof(config->backupWallet));
    }

    // Display settings (optional)
    if (doc.containsKey("brightness")) {
        config->brightness = doc["brightness"];
    }
    if (doc.containsKey("invert_colors")) {
        config->invertColors = doc["invert_colors"];
    }
    if (doc.containsKey("rotation")) {
        config->rotation = doc["rotation"];
    }

    // Config file stays on SD card - NOT deleted
    // It will only be read again if NVS is reset/cleared

    Serial.println("[CONFIG] Configuration loaded from SD card");
    return config->wallet[0] != '\0';  // Valid if wallet is set
}

// ============================================================
// Public API
// ============================================================

void nvs_config_init() {
    if (s_initialized) return;

    // Initialize with defaults first
    nvs_config_reset(&s_config);

    // Try to load from NVS
    if (!nvs_config_load(&s_config)) {
        Serial.println("[NVS] No valid config in NVS, checking for config file...");

        // Try to load from config.json file
        if (loadConfigFromFile(&s_config)) {
            Serial.println("[NVS] Saving config from file to NVS...");
            nvs_config_save(&s_config);
        } else {
            Serial.println("[NVS] No config file found, using defaults");
        }
    } else {
        Serial.println("[NVS] Configuration loaded from NVS");
    }

    s_initialized = true;
}

bool nvs_config_load(miner_config_t *config) {
    if (!s_prefs.begin(NVS_NAMESPACE, true)) {  // Read-only
        Serial.println("[NVS] Failed to open namespace");
        return false;
    }

    size_t len = s_prefs.getBytesLength(NVS_KEY_CONFIG);
    if (len != sizeof(miner_config_t)) {
        Serial.printf("[NVS] Config size mismatch: %d vs %d\n", len, sizeof(miner_config_t));
        s_prefs.end();
        return false;
    }

    size_t read = s_prefs.getBytes(NVS_KEY_CONFIG, config, sizeof(miner_config_t));
    s_prefs.end();

    if (read != sizeof(miner_config_t)) {
        Serial.println("[NVS] Failed to read config");
        return false;
    }

    // Verify checksum
    uint32_t expected = calculateChecksum(config);
    if (config->checksum != expected) {
        Serial.printf("[NVS] Checksum mismatch: %08x vs %08x\n", config->checksum, expected);
        // CRITICAL: Reset config to prevent stale data from being used
        nvs_config_reset(config);
        return false;
    }

    return true;
}

bool nvs_config_save(const miner_config_t *config) {
    // Calculate checksum
    miner_config_t configCopy = *config;
    configCopy.checksum = calculateChecksum(&configCopy);

    if (!s_prefs.begin(NVS_NAMESPACE, false)) {  // Read-write
        Serial.println("[NVS] Failed to open namespace for writing");
        return false;
    }

    size_t written = s_prefs.putBytes(NVS_KEY_CONFIG, &configCopy, sizeof(miner_config_t));
    s_prefs.end();

    if (written != sizeof(miner_config_t)) {
        Serial.println("[NVS] Failed to write config");
        return false;
    }

    // Update global copy
    memcpy(&s_config, &configCopy, sizeof(miner_config_t));

    Serial.println("[NVS] Configuration saved");
    return true;
}

void nvs_config_reset(miner_config_t *config) {
    memset(config, 0, sizeof(miner_config_t));

    // WiFi defaults (empty - will use captive portal)
    config->ssid[0] = '\0';
    config->wifiPassword[0] = '\0';

    // Primary pool defaults
    safeStrCpy(config->poolUrl, DEFAULT_POOL_URL, sizeof(config->poolUrl));
    config->poolPort = DEFAULT_POOL_PORT;
    safeStrCpy(config->poolPassword, DEFAULT_POOL_PASS, sizeof(config->poolPassword));
    config->wallet[0] = '\0';  // Must be set by user

    // Backup pool defaults
    safeStrCpy(config->backupPoolUrl, BACKUP_POOL_URL, sizeof(config->backupPoolUrl));
    config->backupPoolPort = BACKUP_POOL_PORT;
    safeStrCpy(config->backupPoolPassword, DEFAULT_POOL_PASS, sizeof(config->backupPoolPassword));
    config->backupWallet[0] = '\0';

    // Display defaults
    config->brightness = 100;
    config->screenTimeout = 0;  // Never timeout
    config->rotation = 1;       // Landscape (default)
    config->displayEnabled = true;
    config->invertColors = false;  // Normal colors

    // Miner defaults
    safeStrCpy(config->workerName, "hybrid", sizeof(config->workerName));
    config->targetDifficulty = DESIRED_DIFFICULTY;

    config->checksum = 0;  // Will be calculated on save
}

miner_config_t* nvs_config_get() {
    if (!s_initialized) {
        nvs_config_init();
    }
    return &s_config;
}

bool nvs_config_is_valid() {
    miner_config_t *config = nvs_config_get();
    return config->wallet[0] != '\0';
}

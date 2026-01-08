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
// Headless boards don't have SD card support
#if defined(USE_SD_MMC)
    #include <SD_MMC.h>
    #define SD_FS SD_MMC
    #define HAS_SD_CARD 1
#elif defined(SD_CS_PIN)
    #include <SD.h>
    #include <SPI.h>
    #define SD_FS SD
    #define HAS_SD_CARD 1
#else
    // No SD card support (headless builds)
    #define HAS_SD_CARD 0
#endif

// File paths on SD card
#define CONFIG_FILE_PATH "/config.json"
#define STATS_FILE_PATH "/stats.json"

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
#if !HAS_SD_CARD
    // No SD card support on this build (headless)
    Serial.println("[CONFIG] SD card not supported on this board");
    return false;
#else
    Serial.println("[CONFIG] Attempting to load config from SD card...");

    // Initialize SD card
#ifdef USE_SD_MMC
    // ESP32-S3 FNK0104 uses SD_MMC.
    // NOTE: GPIO 48 (D2) is often the RGB LED on Freenove boards, causing 4-bit mode to fail.
    // We default to 1-bit mode to avoid this conflict and ensure stability.
    Serial.println("[CONFIG] Setting up SD_MMC (Freenove FNK0104)...");
    Serial.print("[CONFIG] SD Pins - CLK:"); Serial.print(SD_MMC_CLK);
    Serial.print(" CMD:"); Serial.print(SD_MMC_CMD);
    Serial.print(" D0:"); Serial.print(SD_MMC_D0);
    Serial.print(" Freq:"); Serial.println(BOARD_MAX_SDMMC_FREQ);

    // Only set the essential pins for 1-bit mode to avoid claiming GPIO 48 (LED)
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);

    // Give SD card time to power up (Freenove requires 3s, we use 2s as compromise)
    Serial.println("[CONFIG] Waiting for SD card power-up...");
    delay(2000);
    
    // Try 1-bit mode
    Serial.println("[CONFIG] Trying SD_MMC 1-bit mode...");
    if (!SD_MMC.begin("/sdcard", true, false, BOARD_MAX_SDMMC_FREQ, 5)) {
        Serial.println("[CONFIG] 1-bit failed, trying 1-bit @ 1MHz...");
        SD_MMC.end();  // Clean up before retry
        delay(100);
        if (!SD_MMC.begin("/sdcard", true, false, 1000, 5)) {
            Serial.println("[CONFIG] SD_MMC card not found or failed to mount");
            Serial.println("[CONFIG] Check: card inserted? FAT32? contacts clean?");
            Serial.println("[CONFIG] TIP: Freenove pins can vary. If failing, try swapping CLK/CMD.");
            return false;
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
#endif  // HAS_SD_CARD
}

/**
 * Initialize SD card for file operations
 * Returns true if SD card is ready
 */
static bool initSD() {
#if !HAS_SD_CARD
    return false;
#else
    #ifdef USE_SD_MMC
        // ESP32-S3 SD_MMC mode
        SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
        if (!SD_MMC.begin("/sdcard", true, false, BOARD_MAX_SDMMC_FREQ, 5)) {
            return false;
        }
        if (SD_MMC.cardType() == 0) {
            SD_MMC.end();
            return false;
        }
    #else
        // SPI SD mode
        if (!SD.begin(SD_CS_PIN)) {
            return false;
        }
    #endif
    return true;
#endif
}

/**
 * Save mining stats to SD card as JSON backup
 * Called alongside NVS save - survives firmware updates and factory resets
 */
static bool saveStatsToSD(const mining_persistence_t *stats) {
#if !HAS_SD_CARD
    return false;
#else
    if (!initSD()) {
        // SD card not available - silently skip (not an error)
        return false;
    }

    // Create JSON document
    StaticJsonDocument<512> doc;
    doc["lifetimeHashes"] = stats->lifetimeHashes;
    doc["lifetimeShares"] = stats->lifetimeShares;
    doc["lifetimeAccepted"] = stats->lifetimeAccepted;
    doc["lifetimeRejected"] = stats->lifetimeRejected;
    doc["lifetimeBlocks"] = stats->lifetimeBlocks;
    doc["totalUptimeSeconds"] = stats->totalUptimeSeconds;
    doc["bestDifficultyEver"] = stats->bestDifficultyEver;
    doc["sessionCount"] = stats->sessionCount;
    doc["magic"] = STATS_MAGIC;

    // Write to file
    File file = SD_FS.open(STATS_FILE_PATH, "w");
    if (!file) {
        Serial.println("[SD-STATS] Failed to open stats.json for writing");
        SD_FS.end();
        return false;
    }

    size_t written = serializeJson(doc, file);
    file.close();
    SD_FS.end();

    if (written == 0) {
        Serial.println("[SD-STATS] Failed to write stats.json");
        return false;
    }

    Serial.printf("[SD-STATS] Backup saved: %llu hashes, %lu shares\n",
                  stats->lifetimeHashes, stats->lifetimeShares);
    return true;
#endif
}

/**
 * Load mining stats from SD card backup
 * Used as fallback when NVS is empty (factory reset, firmware update)
 */
static bool loadStatsFromSD(mining_persistence_t *stats) {
#if !HAS_SD_CARD
    return false;
#else
    if (!initSD()) {
        return false;
    }

    if (!SD_FS.exists(STATS_FILE_PATH)) {
        SD_FS.end();
        return false;
    }

    File file = SD_FS.open(STATS_FILE_PATH, "r");
    if (!file) {
        Serial.println("[SD-STATS] Failed to open stats.json");
        SD_FS.end();
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    SD_FS.end();

    if (err) {
        Serial.printf("[SD-STATS] JSON parse error: %s\n", err.c_str());
        return false;
    }

    // Verify magic value
    if (!doc.containsKey("magic") || doc["magic"].as<uint32_t>() != STATS_MAGIC) {
        Serial.println("[SD-STATS] Invalid magic value in stats.json");
        return false;
    }

    // Load values
    stats->lifetimeHashes = doc["lifetimeHashes"] | 0ULL;
    stats->lifetimeShares = doc["lifetimeShares"] | 0UL;
    stats->lifetimeAccepted = doc["lifetimeAccepted"] | 0UL;
    stats->lifetimeRejected = doc["lifetimeRejected"] | 0UL;
    stats->lifetimeBlocks = doc["lifetimeBlocks"] | 0UL;
    stats->totalUptimeSeconds = doc["totalUptimeSeconds"] | 0UL;
    stats->bestDifficultyEver = doc["bestDifficultyEver"] | 0.0;
    stats->sessionCount = doc["sessionCount"] | 0UL;
    stats->magic = STATS_MAGIC;

    Serial.printf("[SD-STATS] Restored from backup: %llu hashes, %lu shares, %lu sessions\n",
                  stats->lifetimeHashes, stats->lifetimeShares, stats->sessionCount);
    return true;
#endif
}

// ============================================================
// Public API
// ============================================================

void nvs_config_init() {
    if (s_initialized) return;

    // Initialize with defaults first
    nvs_config_reset(&s_config);

    bool loadedFromSd = false;
    bool loadedFromNvs = false;

    // 1. Try to load from NVS first (persistent storage takes priority)
    // Config saved from SD card or WiFi portal persists here
    if (nvs_config_load(&s_config)) {
        Serial.println("[NVS] Configuration loaded from NVS");
        loadedFromNvs = true;
    }

    // 2. If no valid NVS config, try SD card (initial setup only)
    // SD card is only used when NVS is empty (first boot or factory reset)
    if (!loadedFromNvs) {
        Serial.println("[NVS] No valid config in NVS, checking for config file...");
        if (loadConfigFromFile(&s_config)) {
            Serial.println("[NVS] Config loaded from SD card (initial setup)");
            loadedFromSd = true;
            // Save to NVS for persistence
            Serial.println("[NVS] Saving config to NVS for persistence...");
            if (nvs_config_save(&s_config)) {
                Serial.println("[NVS] Config saved to NVS successfully - SD card can now be removed");
            } else {
                Serial.println("[NVS] ERROR: Failed to save config to NVS!");
                Serial.println("[NVS] Config will be lost on reboot without SD card!");
            }
        }
    }

    // 3. If neither, we are using defaults (will use WiFi portal)
    if (!loadedFromNvs && !loadedFromSd) {
        Serial.println("[NVS] No config file found, using defaults");
    }

    s_initialized = true;
}

bool nvs_config_load(miner_config_t *config) {
    Serial.printf("[NVS] Loading config (struct size: %d bytes)\n", sizeof(miner_config_t));

    if (!s_prefs.begin(NVS_NAMESPACE, true)) {  // Read-only
        Serial.println("[NVS] Failed to open namespace (may be first boot)");
        return false;
    }

    size_t len = s_prefs.getBytesLength(NVS_KEY_CONFIG);
    if (len == 0) {
        Serial.println("[NVS] No saved config found (first boot or erased)");
        s_prefs.end();
        return false;
    }

    if (len != sizeof(miner_config_t)) {
        Serial.printf("[NVS] Config size mismatch: stored=%d, expected=%d\n", len, sizeof(miner_config_t));
        Serial.println("[NVS] Struct size changed - clearing old config");
        // Clear the old incompatible config
        s_prefs.end();
        if (s_prefs.begin(NVS_NAMESPACE, false)) {
            s_prefs.remove(NVS_KEY_CONFIG);
            s_prefs.end();
        }
        return false;
    }

    size_t read = s_prefs.getBytes(NVS_KEY_CONFIG, config, sizeof(miner_config_t));
    s_prefs.end();

    if (read != sizeof(miner_config_t)) {
        Serial.printf("[NVS] Failed to read config: read=%d, expected=%d\n", read, sizeof(miner_config_t));
        return false;
    }

    // Verify checksum
    uint32_t expected = calculateChecksum(config);
    if (config->checksum != expected) {
        Serial.printf("[NVS] Checksum mismatch: stored=%08x, calculated=%08x\n", config->checksum, expected);
        // CRITICAL: Reset config to prevent stale data from being used
        nvs_config_reset(config);
        return false;
    }

    Serial.printf("[NVS] Config loaded: wallet=%s, pool=%s:%d\n",
                  config->wallet[0] ? config->wallet : "(empty)",
                  config->poolUrl,
                  config->poolPort);
    return true;
}

bool nvs_config_save(const miner_config_t *config) {
    Serial.printf("[NVS] Saving config (%d bytes)...\n", sizeof(miner_config_t));

    // Calculate checksum
    miner_config_t configCopy = *config;
    configCopy.checksum = calculateChecksum(&configCopy);

    if (!s_prefs.begin(NVS_NAMESPACE, false)) {  // Read-write
        Serial.println("[NVS] ERROR: Failed to open namespace for writing");
        return false;
    }

    size_t written = s_prefs.putBytes(NVS_KEY_CONFIG, &configCopy, sizeof(miner_config_t));
    s_prefs.end();

    if (written != sizeof(miner_config_t)) {
        Serial.printf("[NVS] ERROR: Write failed - wrote %d of %d bytes\n", written, sizeof(miner_config_t));
        return false;
    }

    // Update global copy
    memcpy(&s_config, &configCopy, sizeof(miner_config_t));

    Serial.printf("[NVS] Config saved: wallet=%s, pool=%s:%d, checksum=%08x\n",
                  configCopy.wallet[0] ? configCopy.wallet : "(empty)",
                  configCopy.poolUrl,
                  configCopy.poolPort,
                  configCopy.checksum);
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
    config->rotation = 0;       // Portrait USB Top (default)
    config->displayEnabled = true;
    config->invertColors = true;   // Dark theme (default)

    // Miner defaults
    safeStrCpy(config->workerName, "hybrid", sizeof(config->workerName));
    config->targetDifficulty = DESIRED_DIFFICULTY;

    // Stats API defaults - HTTPS disabled by default for stability
    config->statsProxyUrl[0] = '\0';  // No proxy by default
    config->enableHttpsStats = false; // Direct HTTPS disabled (causes WDT crashes)

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

// ============================================================
// Persistent Stats Implementation
// ============================================================

#define NVS_KEY_STATS "stats"

static mining_persistence_t s_persistentStats = {0};
static bool s_statsInitialized = false;

static uint32_t calculateStatsChecksum(const mining_persistence_t *stats) {
    const uint8_t *data = (const uint8_t *)stats;
    uint32_t sum = STATS_MAGIC;

    // Calculate checksum over all fields except the checksum itself
    size_t len = sizeof(mining_persistence_t) - sizeof(uint32_t);
    for (size_t i = 0; i < len; i++) {
        sum = sum * 31 + data[i];
    }

    return sum;
}

bool nvs_stats_load(mining_persistence_t *stats) {
    if (!s_prefs.begin(NVS_NAMESPACE, true)) {  // Read-only
        Serial.println("[NVS-STATS] Failed to open namespace");
        return false;
    }

    size_t len = s_prefs.getBytesLength(NVS_KEY_STATS);
    if (len != sizeof(mining_persistence_t)) {
        Serial.printf("[NVS-STATS] Stats size mismatch: %d vs %d\n", len, sizeof(mining_persistence_t));
        s_prefs.end();
        return false;
    }

    size_t read = s_prefs.getBytes(NVS_KEY_STATS, stats, sizeof(mining_persistence_t));
    s_prefs.end();

    if (read != sizeof(mining_persistence_t)) {
        Serial.println("[NVS-STATS] Failed to read stats");
        return false;
    }

    // Verify magic and checksum
    if (stats->magic != STATS_MAGIC) {
        Serial.printf("[NVS-STATS] Invalid magic: %08x (expected %08x)\n", stats->magic, STATS_MAGIC);
        memset(stats, 0, sizeof(mining_persistence_t));
        return false;
    }

    uint32_t expected = calculateStatsChecksum(stats);
    if (stats->checksum != expected) {
        Serial.printf("[NVS-STATS] Checksum mismatch: %08x vs %08x - clearing corrupted data\n", stats->checksum, expected);
        // Clear corrupted stats from NVS
        if (s_prefs.begin(NVS_NAMESPACE, false)) {
            s_prefs.remove(NVS_KEY_STATS);
            s_prefs.end();
        }
        memset(stats, 0, sizeof(mining_persistence_t));
        return false;
    }

    Serial.printf("[NVS-STATS] Loaded: %llu hashes, %lu shares, %lu sessions\n",
                  stats->lifetimeHashes, stats->lifetimeShares, stats->sessionCount);
    return true;
}

bool nvs_stats_save(const mining_persistence_t *stats) {
    // Create zero-initialized copy to avoid padding byte issues
    mining_persistence_t statsCopy;
    memset(&statsCopy, 0, sizeof(mining_persistence_t));

    // Copy fields explicitly (avoids copying garbage in padding)
    statsCopy.lifetimeHashes = stats->lifetimeHashes;
    statsCopy.lifetimeShares = stats->lifetimeShares;
    statsCopy.lifetimeAccepted = stats->lifetimeAccepted;
    statsCopy.lifetimeRejected = stats->lifetimeRejected;
    statsCopy.lifetimeBlocks = stats->lifetimeBlocks;
    statsCopy.totalUptimeSeconds = stats->totalUptimeSeconds;
    statsCopy.bestDifficultyEver = stats->bestDifficultyEver;
    statsCopy.sessionCount = stats->sessionCount;
    statsCopy.magic = STATS_MAGIC;
    statsCopy.checksum = calculateStatsChecksum(&statsCopy);

    if (!s_prefs.begin(NVS_NAMESPACE, false)) {  // Read-write
        Serial.println("[NVS-STATS] Failed to open namespace for writing");
        return false;
    }

    size_t written = s_prefs.putBytes(NVS_KEY_STATS, &statsCopy, sizeof(mining_persistence_t));
    s_prefs.end();

    if (written != sizeof(mining_persistence_t)) {
        Serial.println("[NVS-STATS] Failed to write stats");
        return false;
    }

    // Update global copy
    memcpy(&s_persistentStats, &statsCopy, sizeof(mining_persistence_t));

    Serial.printf("[NVS-STATS] Saved: %llu lifetime hashes, %lu shares\n",
                  statsCopy.lifetimeHashes, statsCopy.lifetimeShares);

    // Also backup to SD card (survives factory reset and firmware updates)
    saveStatsToSD(&statsCopy);

    return true;
}

mining_persistence_t* nvs_stats_get() {
    if (!s_statsInitialized) {
        bool loaded = false;

        // 1. Try to load from NVS first (primary storage)
        if (nvs_stats_load(&s_persistentStats)) {
            loaded = true;
        }

        // 2. If NVS failed, try SD card backup (survives factory reset)
        if (!loaded) {
            Serial.println("[NVS-STATS] No NVS stats, checking SD card backup...");
            if (loadStatsFromSD(&s_persistentStats)) {
                loaded = true;
                // Save recovered stats to NVS for faster access next boot
                Serial.println("[NVS-STATS] Restoring stats from SD card to NVS...");
                nvs_stats_save(&s_persistentStats);
            }
        }

        // 3. If both failed, start fresh
        if (!loaded) {
            memset(&s_persistentStats, 0, sizeof(mining_persistence_t));
            Serial.println("[NVS-STATS] No saved stats, starting fresh");
        }

        // Increment session count on each boot
        s_persistentStats.sessionCount++;
        s_statsInitialized = true;
    }
    return &s_persistentStats;
}

void nvs_stats_update(uint64_t currentHashes, uint32_t currentShares,
                      uint32_t currentAccepted, uint32_t currentRejected,
                      uint32_t currentBlocks, uint32_t sessionSeconds,
                      double bestDiff) {
    // Get current persistent stats
    mining_persistence_t *stats = nvs_stats_get();

    // Add current session values to lifetime totals
    stats->lifetimeHashes += currentHashes;
    stats->lifetimeShares += currentShares;
    stats->lifetimeAccepted += currentAccepted;
    stats->lifetimeRejected += currentRejected;
    stats->lifetimeBlocks += currentBlocks;
    stats->totalUptimeSeconds += sessionSeconds;

    // Track best difficulty ever
    if (bestDiff > stats->bestDifficultyEver) {
        stats->bestDifficultyEver = bestDiff;
    }

    // Save to NVS
    nvs_stats_save(stats);
}

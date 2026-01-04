/*
 * SparkMiner - Main Entry Point
 * ESP32 Bitcoin Solo Miner
 *
 * A tiny spark of mining power - combining performance with usability
 *
 * GPL v3 License
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <esp_pm.h>
#include <OneButton.h>

#include <board_config.h>
#include "mining/miner.h"
#include "stratum/stratum_types.h"
#include "stratum/stratum.h"
#include "config/nvs_config.h"
#include "config/wifi_manager.h"
#include "stats/monitor.h"
#include "display/display.h"

// Task handles
TaskHandle_t miner0Task = NULL;
TaskHandle_t miner1Task = NULL;
TaskHandle_t stratumTask = NULL;
TaskHandle_t monitorTask = NULL;

// Global state
volatile bool systemReady = false;

// Button handling (OneButton)
#if defined(BUTTON_PIN) && USE_DISPLAY
OneButton button(BUTTON_PIN, true, true);  // active low, enable pullup

// Single click: cycle screens
void onButtonClick() {
    display_next_screen();
}

// Double click: flip screen 180°
void onButtonDoubleClick() {
    uint8_t newRotation = display_flip_rotation();
    // Save to NVS
    miner_config_t *config = nvs_config_get();
    config->rotation = newRotation;
    nvs_config_save(config);
}
#endif

// Forward declarations
void setupPowerManagement();
void setupTasks();
void printBanner();

/**
 * Arduino setup - runs once at boot
 */
void setup() {
    Serial.begin(115200);
    delay(1000);

    printBanner();

    // Configure watchdog with longer timeout for mining
    // Mining loops will yield periodically via vTaskDelay(1)
    Serial.println("[INIT] Configuring watchdog timer (30s timeout)...");
    esp_task_wdt_init(30, true);  // 30 second timeout, panic on trigger

    // Disable power management (no CPU throttling/sleep)
    setupPowerManagement();

    // Initialize NVS configuration
    nvs_config_init();

    // Initialize mining subsystem
    miner_init();

    // Initialize stratum subsystem
    stratum_init();

    // Load pool configuration from NVS
    miner_config_t *config = nvs_config_get();
    stratum_set_pool(config->poolUrl, config->poolPort, config->wallet, config->poolPassword, config->workerName);
    stratum_set_backup_pool(config->backupPoolUrl, config->backupPoolPort,
                           config->backupWallet, config->backupPoolPassword, config->workerName);

    // Initialize display early (needed for WiFi setup screen)
    #if USE_DISPLAY
        display_init(config->rotation, config->brightness);
    #endif

    // Setup button handlers (OneButton)
    #if defined(BUTTON_PIN) && USE_DISPLAY
        button.setClickTicks(400);       // Time window for single click (ms)
        button.setPressTicks(800);       // Time for long press to start (ms)
        button.setDebounceTicks(50);     // Debounce time (ms)
        button.attachClick(onButtonClick);
        button.attachDoubleClick(onButtonDoubleClick);
    #endif

    // Initialize WiFiManager and connect
    wifi_manager_init();
    Serial.println("[INIT] Starting WiFi...");
    wifi_manager_start();

    // Initialize monitor (live stats - display already initialized)
    monitor_init();

    Serial.println("[INIT] Setup complete");

    // Check if configuration is valid
    if (!nvs_config_is_valid()) {
        Serial.println("[WARN] No wallet configured! Please set up via captive portal.");
    }

    // Start FreeRTOS tasks
    setupTasks();

    // Print configuration summary
    Serial.println();
    Serial.println("=== SparkMiner v" AUTO_VERSION " ===");
    Serial.println("SHA-256 Implementation: "
        #if defined(USE_HARDWARE_SHA)
            "Hardware (ESP32-S3/C3)"
        #else
            "Software (Optimized)"
        #endif
    );
    Serial.println("Board: " BOARD_NAME);
    #if USE_DISPLAY
        Serial.println("Display: Enabled");
    #else
        Serial.println("Display: Disabled");
    #endif
    Serial.println();

    systemReady = true;
}

/**
 * Arduino loop - runs continuously
 * Minimal work here - most work done in FreeRTOS tasks
 */
void loop() {
    // Handle button with OneButton (single click = next screen, double click = flip)
    #if defined(BUTTON_PIN) && USE_DISPLAY
        button.tick();
    #endif

    // Yield to FreeRTOS tasks
    delay(5);  // Fast tick for responsive button handling
}

/**
 * Disable ESP32 power management for consistent performance
 * From BitsyMiner - critical for maintaining hashrate
 */
void setupPowerManagement() {
    #if CONFIG_PM_ENABLE
        esp_pm_lock_handle_t pmLock;
        esp_err_t err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "miner", &pmLock);
        if (err == ESP_OK) {
            esp_pm_lock_acquire(pmLock);
            Serial.println("[INIT] Power management disabled (no sleep)");
        } else {
            Serial.println("[WARN] Could not disable power management");
        }
    #else
        Serial.println("[INIT] Power management not enabled in config");
    #endif
}

/**
 * Create FreeRTOS tasks for mining and pool communication
 * Miner tasks are only created if wallet is configured
 */
void setupTasks() {
    Serial.println("[INIT] Creating FreeRTOS tasks...");

    bool hasValidConfig = nvs_config_is_valid();

    // Stratum task (pool communication) - only if configured
    if (hasValidConfig) {
        xTaskCreatePinnedToCore(
            stratum_task,
            "Stratum",
            STRATUM_STACK,
            NULL,
            STRATUM_PRIORITY,
            &stratumTask,
            STRATUM_CORE
        );
    }

    // Monitor task (display + stats) - always runs for UI
    xTaskCreatePinnedToCore(
        monitor_task,
        "Monitor",
        MONITOR_STACK,
        NULL,
        MONITOR_PRIORITY,
        &monitorTask,
        MONITOR_CORE
    );

    // Only create miner tasks if wallet is configured
    if (hasValidConfig) {
        // Miner on Core 1 (high priority, dedicated core)
        xTaskCreatePinnedToCore(
            miner_task_core1,
            "Miner1",
            MINER_1_STACK,
            NULL,
            MINER_1_PRIORITY,
            &miner1Task,
            MINER_1_CORE
        );

        // Miner on Core 0 (lower priority, yields to WiFi/Stratum/Display)
        // Uses taskYIELD() every 256 hashes to cooperate with other tasks
        xTaskCreatePinnedToCore(
            miner_task_core0,
            "Miner0",
            MINER_0_STACK,
            NULL,
            MINER_0_PRIORITY,
            &miner0Task,
            MINER_0_CORE
        );

        Serial.println("[INIT] All tasks created (dual-core mining)");
    } else {
        Serial.println("[INIT] Monitor task created (mining disabled - no wallet)");
        Serial.println("[INIT] Configure via captive portal or SD card config.json");
    }
}

/**
 * Print startup banner
 */
void printBanner() {
    Serial.println();
    Serial.println("╔═══════════════════════════════════════════╗");
    Serial.println("║          SparkMiner for ESP32             ║");
    Serial.println("║     A tiny spark of mining power          ║");
    Serial.println("╚═══════════════════════════════════════════╝");
    Serial.println();
}

// Mining functions are implemented in mining/miner.cpp

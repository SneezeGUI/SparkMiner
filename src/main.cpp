/*
 * SparkMiner - Main Entry Point
 * ESP32 Bitcoin Solo Miner
 *
 * A tiny spark of mining power - combining performance with usability
 *
 * Author: Sneeze (github.com/SneezeGUI)
 * GPL v3 License
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <esp_pm.h>
#include <Preferences.h>
#include <OneButton.h>
#include <soc/soc_caps.h>  // For SOC_CPU_CORES_NUM

// For CPU overclocking
#if defined(CONFIG_IDF_TARGET_ESP32)
#include <soc/rtc.h>
#include <soc/rtc_cntl_reg.h>
#include <esp32/rom/rtc.h>
extern "C" {
    #include <esp_private/esp_clk.h>
}
#endif

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
TaskHandle_t buttonTask = NULL;

// Global state
volatile bool systemReady = false;

#if defined(BOOT_TEST_MINIMAL)
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("[BOOT-TEST] ESP32-S3 minimal firmware is running");
}

void loop() {
    Serial.println("[BOOT-TEST] alive");
    delay(1000);
}

#else

// Button handling (OneButton)
#if defined(BUTTON_PIN) && (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
OneButton button(BUTTON_PIN, true, true);  // active low, enable pullup

// Single click: wake screen if off, otherwise cycle screens
void onButtonClick() {
    monitor_reset_activity();
    if (display_is_backlight_off()) {
        display_set_backlight_on();
        return;  // First click just wakes the screen
    }
    display_next_screen();
}

// Double click: cycle screen rotation (0->1->2->3->0)
void onButtonDoubleClick() {
    monitor_reset_activity();
    if (display_is_backlight_off()) {
        display_set_backlight_on();
        return;
    }
    Serial.println("[BUTTON] Double-click detected - cycling rotation");
    uint8_t newRotation = display_flip_rotation();
    // Save to NVS
    miner_config_t *config = nvs_config_get();
    config->rotation = newRotation;
    nvs_config_save(config);
    Serial.printf("[BUTTON] New rotation saved: %d\n", newRotation);
}

// Triple click: toggle color inversion
void onButtonMultiClick() {
    monitor_reset_activity();
    if (display_is_backlight_off()) {
        display_set_backlight_on();
        return;
    }
    int clicks = button.getNumberClicks();
    if (clicks == 3) {
        Serial.println("[BUTTON] Triple-click detected - toggling color theme");
        miner_config_t *config = nvs_config_get();
        config->invertColors = !config->invertColors;
        display_set_inverted(config->invertColors);
        nvs_config_save(config);
        Serial.printf("[BUTTON] Theme switched to %s mode\n", config->invertColors ? "Dark" : "Light");
    }
}

// Long press: factory reset with 3-second visual countdown
void onButtonLongPressStart() {
    monitor_reset_activity();
    if (display_is_backlight_off()) {
        display_set_backlight_on();
        return;
    }
    Serial.println("[RESET] Long press detected - starting countdown...");

    // Visual countdown on display
    for (int i = 3; i > 0; i--) {
        display_show_reset_countdown(i);

        delay(1000);

        // Check if button released early
        if (digitalRead(BUTTON_PIN) == HIGH) {
            Serial.println("[RESET] Cancelled - button released");
            display_redraw();  // Trigger redraw of normal screen
            return;
        }
    }

    Serial.println("[RESET] *** FACTORY RESET TRIGGERED ***");

    // Show reset message
    display_show_reset_complete();

    // Clear NVS
    Preferences prefs;
    if (prefs.begin("sparkminer", false)) {
        prefs.clear();
        prefs.end();
        Serial.println("[RESET] NVS cleared");
    }

    // Clear WiFi settings
    WiFi.disconnect(true, true);
    Serial.println("[RESET] WiFi settings cleared");

    delay(500);
    Serial.println("[RESET] Restarting...");
    ESP.restart();
}
#endif

#if defined(BUTTON_PIN) && (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
/**
 * Dedicated button handling task (with display)
 * Runs at higher priority than mining to ensure responsive UI
 */
void button_task(void *param) {
    Serial.println("[BUTTON] Task started on core 0");
    for (;;) {
        button.tick();
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms polling = responsive buttons
    }
}

#elif defined(BUTTON_PIN)
/**
 * Headless button handling task
 * Simple long-press detection for factory reset (no OneButton/display dependencies)
 * Hold button for 5 seconds to trigger factory reset
 */
void button_task(void *param) {
    Serial.println("[BUTTON] Headless button task started");
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    unsigned long pressStart = 0;
    bool wasPressed = false;
    const unsigned long RESET_HOLD_MS = 5000;  // 5 seconds to trigger reset

    for (;;) {
        bool pressed = (digitalRead(BUTTON_PIN) == LOW);

        if (pressed && !wasPressed) {
            // Button just pressed
            pressStart = millis();
            wasPressed = true;
            Serial.println("[BUTTON] Press detected - hold 5s for factory reset");
        } else if (pressed && wasPressed) {
            // Button held - check duration
            unsigned long held = millis() - pressStart;
            if (held >= RESET_HOLD_MS) {
                Serial.println("[RESET] *** FACTORY RESET TRIGGERED ***");

                // Clear NVS
                Preferences prefs;
                if (prefs.begin("sparkminer", false)) {
                    prefs.clear();
                    prefs.end();
                    Serial.println("[RESET] NVS cleared");
                }

                // Clear WiFi settings
                WiFi.disconnect(true, true);
                Serial.println("[RESET] WiFi settings cleared");

                delay(500);
                Serial.println("[RESET] Restarting...");
                ESP.restart();
            }
        } else if (!pressed && wasPressed) {
            // Button released before 5 seconds
            Serial.println("[BUTTON] Released - normal operation continues");
            wasPressed = false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms polling for headless
    }
}
#endif

// Forward declarations
void setupPowerManagement();
void setupTasks();
void printBanner();
void checkFactoryReset();
uint32_t tryOverclock();
void printHeadlessDiagnostics(bool force = false);

static const char* wifiStatusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "IDLE";
        case WL_NO_SSID_AVAIL: return "NO_SSID";
        case WL_SCAN_COMPLETED: return "SCAN_DONE";
        case WL_CONNECTED: return "CONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        default: return "UNKNOWN";
    }
}

static void printMaskedWallet(const char *wallet) {
    if (!wallet || wallet[0] == '\0') {
        Serial.print("(empty)");
        return;
    }

    size_t len = strlen(wallet);
    if (len <= 12) {
        Serial.print(wallet);
        return;
    }

    for (size_t i = 0; i < 8 && i < len; i++) {
        Serial.print(wallet[i]);
    }
    Serial.print("...");
    for (size_t i = len - 4; i < len; i++) {
        Serial.print(wallet[i]);
    }
}

/**
 * Attempt CPU overclock using 320MHz PLL
 * ESP32 valid PLL configs:
 *   - 480MHz PLL / 2 = 240MHz (standard)
 *   - 320MHz PLL / 1 = 320MHz (overclock, may not be stable)
 *   - 320MHz PLL / 2 = 160MHz
 *
 * Returns the achieved frequency
 */
uint32_t tryOverclock() {
#if defined(CONFIG_IDF_TARGET_ESP32)
    Serial.println("[OVERCLOCK] Attempting 320MHz overclock...");
    Serial.flush();  // Ensure message is sent before clock change

    uint32_t baseFreq = getCpuFrequencyMhz();
    Serial.printf("[OVERCLOCK] Base frequency: %u MHz\n", baseFreq);
    Serial.flush();

    // Configure for 320MHz PLL with divider 1
    rtc_cpu_freq_config_t conf;
    conf.source = RTC_CPU_FREQ_SRC_PLL;
    conf.source_freq_mhz = 320;  // 320MHz PLL
    conf.div = 1;                // div=1 = 320MHz output
    conf.freq_mhz = 320;

    // Apply the configuration
    rtc_clk_cpu_freq_set_config(&conf);

    // Adjust serial baud rate for new clock (320/240 ratio)
    // Serial internally uses APB clock which may change
    delay(10);

    // Quick stability test
    volatile uint32_t dummy = 0;
    for (int j = 0; j < 100000; j++) {
        dummy += j * 17;
        dummy ^= (dummy >> 3);
    }

    // Verify the frequency
    uint32_t actualFreq = getCpuFrequencyMhz();

    if (actualFreq >= 300) {
        Serial.printf("[OVERCLOCK] SUCCESS at %u MHz!\n", actualFreq);
        return actualFreq;
    }

    // Failed, revert to 240MHz
    setCpuFrequencyMhz(240);
    Serial.printf("[OVERCLOCK] Failed (got %u MHz), using 240 MHz\n", actualFreq);
    return 240;

#else
    Serial.println("[OVERCLOCK] Not supported on this chip variant");
    return getCpuFrequencyMhz();
#endif
}

/**
 * Check if boot button is held for factory reset
 * Hold BOOT button for 5+ seconds to wipe NVS and restart
 */
void checkFactoryReset() {
    #ifdef BUTTON_PIN
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Check if button is pressed at boot
    if (digitalRead(BUTTON_PIN) == LOW) {
        Serial.println();
        Serial.println("[RESET] Boot button held - hold for 5 seconds to factory reset...");

        unsigned long startTime = millis();
        int lastSecond = -1;

        while (digitalRead(BUTTON_PIN) == LOW) {
            unsigned long elapsed = millis() - startTime;
            int seconds = elapsed / 1000;

            // Print countdown
            if (seconds != lastSecond && seconds <= 5) {
                Serial.print("[RESET] "); Serial.print(5 - seconds); Serial.println(" seconds...");
                lastSecond = seconds;
            }

            // Check if 5 seconds have passed
            if (elapsed >= 5000) {
                Serial.println();
                Serial.println("[RESET] *** FACTORY RESET TRIGGERED ***");
                Serial.println("[RESET] Clearing all configuration...");

                // Clear NVS
                Preferences prefs;
                if (prefs.begin("sparkminer", false)) {
                    prefs.clear();
                    prefs.end();
                }

                // Also reset WiFiManager settings
                WiFi.disconnect(true, true);

                Serial.println("[RESET] Configuration cleared. Restarting...");
                delay(1000);
                ESP.restart();
            }

            delay(100);
        }

        // Button released before 5 seconds
        Serial.println("[RESET] Button released - normal boot continuing...");
        Serial.println();
    }
    #endif
}

/**
 * Arduino setup - runs once at boot
 */
void setup() {
    Serial.begin(115200);

    // Wait for USB CDC to be ready (with timeout for headless operation)
    // On ESP32-S3, Serial only becomes true when USB host enumerates CDC
    // Without timeout, device would block forever if no USB connected
    unsigned long serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart < 5000)) {
        delay(10);
    }
    Serial.flush();
    
    // Debug output  
    Serial.println();
    Serial.println("[BOOT] Starting...");

    // Check for factory reset (hold BOOT button for 5 seconds)
    checkFactoryReset();

    printBanner();

    // Configure watchdog with longer timeout for mining
    // Mining loops will yield periodically via vTaskDelay(1)
    Serial.println("[INIT] Configuring watchdog timer (30s timeout)...");
    esp_task_wdt_init(30, true);  // 30 second timeout, panic on trigger

    // Disable power management (no CPU throttling/sleep)
    setupPowerManagement();

    // NOTE: ESP32 overclocking via PLL manipulation causes boot loops
    // This ESP32-D0WD-V3 chip cannot exceed 240MHz
    // NMMiner's 1000 KH/s must come from SHA optimization, not overclocking
    Serial.printf("[INIT] Running at %u MHz\n", getCpuFrequencyMhz());

    // Initialize NVS configuration
    nvs_config_init();

    // Initialize mining subsystem
    miner_init();

    // Initialize stratum subsystem
    stratum_init();

    // Load pool configuration from NVS
    miner_config_t *config = nvs_config_get();
    Serial.println("[CONFIG] Active configuration:");
    Serial.printf("[CONFIG] WiFi SSID: %s\n", config->ssid[0] ? config->ssid : "(empty)");
    Serial.printf("[CONFIG] Primary pool: %s:%d\n", config->poolUrl, config->poolPort);
    Serial.print("[CONFIG] Wallet: ");
    printMaskedWallet(config->wallet);
    Serial.println();
    Serial.printf("[CONFIG] Worker: %s\n", config->workerName[0] ? config->workerName : "(empty)");
    stratum_set_pool(config->poolUrl, config->poolPort, config->wallet, config->poolPassword, config->workerName);
    stratum_set_backup_pool(config->backupPoolUrl, config->backupPoolPort,
                           config->backupWallet, config->backupPoolPassword, config->workerName);

    // Initialize display early (needed for WiFi setup screen)
    #if (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
        display_init(config->rotation, config->brightness);
        display_set_inverted(config->invertColors);
    #endif

    // Setup button handlers (OneButton)
    #if defined(BUTTON_PIN) && (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
        button.setClickMs(400);          // Time window for single click (ms)
        button.setPressMs(1500);         // Time for long press to start (1.5s)
        button.setDebounceMs(50);        // Debounce time (ms)
        button.attachClick(onButtonClick);
        button.attachDoubleClick(onButtonDoubleClick);
        button.attachMultiClick(onButtonMultiClick);          // Triple-click for inversion
        button.attachLongPressStart(onButtonLongPressStart);  // Factory reset handler
        Serial.println("[INIT] Button handlers registered (click/double/triple/long-press)");
    #endif

    // Register WiFi event handlers for diagnostics
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.printf("[WIFI] Disconnected, reason: %d\n", info.wifi_sta_disconnected.reason);
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.printf("[WIFI] Connected, channel: %d\n", WiFi.channel());
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.printf("[WIFI] Got IP: %s, RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

    // Initialize WiFiManager and connect
    wifi_manager_init();
    Serial.println("[INIT] Starting WiFi...");
    wifi_manager_start();
    printHeadlessDiagnostics(true);

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
            "Hardware/ASM (ESP32-S3)"
        #else
            "Software (Optimized)"
        #endif
    );
    Serial.println("Board: " BOARD_NAME);
    #if (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
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
    // Button handling moved to dedicated FreeRTOS task for responsiveness during mining
    printHeadlessDiagnostics(false);

    // Yield to FreeRTOS tasks
    vTaskDelay(pdMS_TO_TICKS(100));  // Main loop can sleep longer now
}

void printHeadlessDiagnostics(bool force) {
#if !(USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
    static uint32_t lastPrint = 0;
    uint32_t now = millis();

    if (!force && (now - lastPrint < 30000)) {
        return;
    }
    lastPrint = now;

    miner_config_t *config = nvs_config_get();
    wl_status_t wifiStatus = WiFi.status();
    const char *currentPool = stratum_get_pool();

    Serial.print("[HEADLESS] WiFi=");
    Serial.print(wifiStatusName(wifiStatus));
    if (wifiStatus == WL_CONNECTED) {
        Serial.print(" SSID=");
        Serial.print(WiFi.SSID());
        Serial.print(" IP=");
        Serial.print(WiFi.localIP());
        Serial.print(" RSSI=");
        Serial.print(WiFi.RSSI());
        Serial.print("dBm");
    }

    Serial.print(" | Pool=");
    Serial.print(config->poolUrl);
    Serial.print(":");
    Serial.print(config->poolPort);

    Serial.print(" | Wallet=");
    printMaskedWallet(config->wallet);

    Serial.print(" | Config=");
    Serial.print(nvs_config_is_valid() ? "OK" : "MISSING_WALLET");

    Serial.print(" | Stratum=");
    Serial.print(stratum_is_connected() ? "CONNECTED" : "DISCONNECTED");
    if (stratum_is_connected()) {
        Serial.print(" current=");
        Serial.print(currentPool && currentPool[0] ? currentPool : "(unknown)");
        if (stratum_is_backup()) {
            Serial.print(" backup");
        }
    }
    Serial.println();
#endif
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

    // Button task (responsive UI during mining)
    // Needs 4KB+ stack for NVS writes (rotation save) and display updates
    #if defined(BUTTON_PIN) && (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
        xTaskCreatePinnedToCore(
            button_task,
            "Button",
            4096,           // 4KB stack for NVS/flash operations in handlers
            NULL,
            5,              // Priority 5: above miner0 (1), below miner1 (19)
            &buttonTask,
            0               // Core 0 with other UI tasks
        );
    #elif defined(BUTTON_PIN)
        // Headless button task for factory reset (Issue #15 fix)
        xTaskCreatePinnedToCore(
            button_task,
            "Button",
            3072,           // Smaller stack for headless (no display calls)
            NULL,
            2,              // Low priority, just needs to run occasionally
            &buttonTask,
            0
        );
    #endif

    // Only create miner tasks if wallet is configured
    if (hasValidConfig) {
        #if (SOC_CPU_CORES_NUM >= 2)
            // Dual-core: Run miners on both cores
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

            #if defined(S3_CORE1_ONLY)
            Serial.println("[INIT] Core0 miner disabled for S3 SHA benchmark");
            #else
            // Miner on Core 0 (lower priority, yields to WiFi/Stratum/Display)
            xTaskCreatePinnedToCore(
                miner_task_core0,
                "Miner0",
                MINER_0_STACK,
                NULL,
                MINER_0_PRIORITY,
                &miner0Task,
                MINER_0_CORE
            );
            #endif

            Serial.println("[INIT] All tasks created (dual-core mining)");
        #else
            // Single-core (C3, S2): Run only one miner task, not pinned
            // Must yield frequently to let WiFi/Stratum work
            xTaskCreate(
                miner_task_core0,
                "Miner",
                MINER_0_STACK,
                NULL,
                MINER_0_PRIORITY,
                &miner0Task
            );

            Serial.println("[INIT] All tasks created (single-core mining)");
        #endif
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

#endif // BOOT_TEST_MINIMAL

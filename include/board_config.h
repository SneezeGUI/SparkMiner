/*
 * SparkMiner - Board Configuration
 * Compile-time board selection and pin definitions
 *
 * GPL v3 License
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// ============================================================
// Project Info
// ============================================================
#define MINER_NAME "SparkMiner"
#define MINER_VERSION "1.0.0"

#ifndef AUTO_VERSION
#define AUTO_VERSION "dev"
#endif

// ============================================================
// Byte swap macro (used throughout)
// ============================================================
#define BYTESWAP32(z) ((uint32_t)((z&0xFF)<<24|((z>>8)&0xFF)<<16|((z>>16)&0xFF)<<8|((z>>24)&0xFF)))

// ============================================================
// Debug Configuration
// ============================================================
#ifdef DEBUG_MINING
    #define dbg(...) Serial.printf(__VA_ARGS__)
#else
    #define dbg(...) (void)0
#endif

// ============================================================
// ESP32-2432S028R - Cheap Yellow Display 2.8"
// ============================================================
#if defined(ESP32_2432S028)
    #define BOARD_NAME "ESP32-2432S028"

    // Display
    #ifndef USE_DISPLAY
        #define USE_DISPLAY 1
    #endif
    #define DISPLAY_TYPE_TFT 1
    #define TFT_WIDTH 320
    #define TFT_HEIGHT 240

    // LED (RGB, active low)
    #ifndef LED_R_PIN
        #define LED_R_PIN 4
    #endif
    #ifndef LED_G_PIN
        #define LED_G_PIN 16
    #endif
    #ifndef LED_B_PIN
        #define LED_B_PIN 17
    #endif
    #define LED_PWM_FREQ 5000
    #define LED_PWM_RES 12

    // Backlight
    #ifndef TFT_BL_PIN
        #define TFT_BL_PIN 21
    #endif

    // Button
    #ifndef BUTTON_PIN
        #define BUTTON_PIN 0
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // SHA Implementation: Defined in platformio.ini (USE_HARDWARE_SHA=1)

// ============================================================
// ESP32-S3-CYD - Cheap Yellow Display (S3 Single USB)
// ============================================================
#elif defined(ESP32_S3_CYD)
    #define BOARD_NAME "ESP32-S3-CYD"

    // Display
    #ifndef USE_DISPLAY
        #define USE_DISPLAY 1
    #endif
    #define DISPLAY_TYPE_TFT 1
    #define TFT_WIDTH 320
    #define TFT_HEIGHT 240

    // LED
    #ifndef LED_PIN
        #define LED_PIN 4
    #endif

    // Backlight (Waveshare ESP32-S3-Touch-LCD-2.8)
    #ifndef TFT_BL_PIN
        #define TFT_BL_PIN 45
    #endif

    // Button
    #ifndef BUTTON_PIN
        #define BUTTON_PIN 0
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // SHA Implementation: Defined in platformio.ini (USE_HARDWARE_SHA=1)

// ============================================================
// ESP32-S3 DevKit - Hardware SHA
// ============================================================
#elif defined(ESP32_S3_DEVKIT) || defined(ESP32_S3_MINI)
    #define BOARD_NAME "ESP32-S3"

    // No display by default
    #ifndef USE_DISPLAY
        #define USE_DISPLAY 0
    #endif

    // LED
    #ifndef LED_PIN
        #define LED_PIN 48
    #endif

    // Button
    #ifndef BUTTON_PIN
        #define BUTTON_PIN 0
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // SHA Implementation: Defined in platformio.ini (USE_HARDWARE_SHA=1)

// ============================================================
// ESP32 Headless
// ============================================================
#elif defined(ESP32_HEADLESS)
    #define BOARD_NAME "ESP32-Headless"

    #define USE_DISPLAY 0

    #ifndef BUTTON_PIN
        #define BUTTON_PIN 0
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // SHA Implementation: Defined in platformio.ini (USE_HARDWARE_SHA=1)

// ============================================================
// Default - Generic ESP32
// ============================================================
#else
    #define BOARD_NAME "ESP32-Generic"
    #define USE_DISPLAY 0
    // SHA Implementation: Defaults to software if not defined in platformio.ini

    #ifndef BUTTON_PIN
        #define BUTTON_PIN 0
    #endif
    #define BUTTON_ACTIVE_LOW 1
#endif

// ============================================================
// FreeRTOS Task Configuration (from BitsyMiner)
// ============================================================

// Core 0 - Shared tasks (WiFi, Stratum, Display, etc.)
#define CORE_0 0

// Core 1 - Dedicated mining (highest priority)
#define CORE_1 1

// Miner on Core 0 (lower priority, yields to other tasks)
#define MINER_0_CORE        CORE_0
#define MINER_0_PRIORITY    1
#define MINER_0_STACK       8000    // Increased for SHA stack usage
#define MINER_0_YIELD_COUNT 256     // Yield every N hashes

// Miner on Core 1 (highest priority, dedicated)
#define MINER_1_CORE        CORE_1
#define MINER_1_PRIORITY    19      // Near-max priority (FreeRTOS max is 24)
#define MINER_1_STACK       8000    // Increased for SHA stack usage

// Stratum task
#define STRATUM_CORE        CORE_0
#define STRATUM_PRIORITY    2
#define STRATUM_STACK       12288

// Monitor/Display task
// NOTE: Needs large stack for HTTPClient + JSON parsing + TFT rendering
#define MONITOR_CORE        CORE_0
#define MONITOR_PRIORITY    1
#define MONITOR_STACK       10000

// Stats API task
#define STATS_CORE          CORE_0
#define STATS_PRIORITY      1
#define STATS_STACK         6000

// ============================================================
// Network Configuration
// ============================================================
#define AP_SSID_PREFIX      "SparkMiner_"
#define AP_PASSWORD         "minebitcoin"

#define WIFI_RECONNECT_MS   10000
#define NTP_UPDATE_MS       600000  // 10 minutes

// ============================================================
// Pool Configuration
// ============================================================
#define DEFAULT_POOL_URL    "public-pool.io"
#define DEFAULT_POOL_PORT   21496
#define DEFAULT_POOL_PASS   "x"

#define BACKUP_POOL_URL     "pool.nerdminers.org"
#define BACKUP_POOL_PORT    3333

#define POOL_TIMEOUT_MS     60000   // 60s inactivity
#define POOL_KEEPALIVE_MS   30000   // 30s keepalive
#define POOL_FAILOVER_MS    30000   // 30s before failover

// ============================================================
// String Limits
// ============================================================
#define MAX_SSID_LENGTH     63  // Note: ESP-IDF uses MAX_SSID_LEN=32
#define MAX_PASSWORD_LEN    64
#define MAX_POOL_URL_LEN    80
#define MAX_WALLET_LEN      120
#define MAX_JOB_ID_LEN      64

#endif // BOARD_CONFIG_H

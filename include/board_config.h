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
    // Undefine TFT dimensions if set by library, then set our values
    #ifdef TFT_WIDTH
        #undef TFT_WIDTH
    #endif
    #define TFT_WIDTH 320
    #ifdef TFT_HEIGHT
        #undef TFT_HEIGHT
    #endif
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
    // Undefine TFT dimensions if set by library, then set our values
    #ifdef TFT_WIDTH
        #undef TFT_WIDTH
    #endif
    #define TFT_WIDTH 320
    #ifdef TFT_HEIGHT
        #undef TFT_HEIGHT
    #endif
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
// LILYGO T-Display S3 - 170x320 ST7789 (8-bit parallel)
// ============================================================
#elif defined(LILYGO_T_DISPLAY_S3)
    #define BOARD_NAME "T-Display-S3"

    #ifndef USE_DISPLAY
        #define USE_DISPLAY 1
    #endif
    #define DISPLAY_TYPE_TFT 1

    #ifdef TFT_WIDTH
        #undef TFT_WIDTH
    #endif
    #define TFT_WIDTH 170

    #ifdef TFT_HEIGHT
        #undef TFT_HEIGHT
    #endif
    #define TFT_HEIGHT 320

    // Buttons
    #ifndef BUTTON_PIN
        #define BUTTON_PIN 0
    #endif
    #ifndef BUTTON2_PIN
        #define BUTTON2_PIN 14
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // 5V enable pin (T-Display S3 specific)
    #define PIN_ENABLE5V 15

    // SHA Implementation: Defined in platformio.ini (USE_HARDWARE_SHA=1)

// ============================================================
// LILYGO T-Display V1 - 135x240 ST7789 (SPI)
// ============================================================
#elif defined(LILYGO_T_DISPLAY_V1)
    #define BOARD_NAME "T-Display-V1"

    #ifndef USE_DISPLAY
        #define USE_DISPLAY 1
    #endif
    #define DISPLAY_TYPE_TFT 1

    #ifdef TFT_WIDTH
        #undef TFT_WIDTH
    #endif
    #define TFT_WIDTH 135

    #ifdef TFT_HEIGHT
        #undef TFT_HEIGHT
    #endif
    #define TFT_HEIGHT 240

    // Buttons
    #ifndef BUTTON_PIN
        #define BUTTON_PIN 0
    #endif
    #ifndef BUTTON2_PIN
        #define BUTTON2_PIN 35
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // SHA Implementation: Defined in platformio.ini (USE_HARDWARE_SHA=1)

// ============================================================
// ESP32-S3 DevKit - Hardware SHA (headless)
// ============================================================
#elif defined(ESP32_S3_DEVKIT)
    #define BOARD_NAME "ESP32-S3-DevKit"

    #ifndef USE_DISPLAY
        #define USE_DISPLAY 0
    #endif

    // Built-in LED (optional)
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
// ESP32-S3 Mini (Wemos/Lolin) - Headless with RGB LED
// ============================================================
#elif defined(ESP32_S3_MINI)
    #define BOARD_NAME "ESP32-S3-Mini"

    #ifndef USE_DISPLAY
        #define USE_DISPLAY 0
    #endif

    // RGB LED Status (WS2812B on GPIO47)
    #define USE_LED_STATUS 1
    #ifndef RGB_LED_PIN
        #define RGB_LED_PIN 47
    #endif
    #define RGB_LED_TYPE_WS2812B 1
    #define RGB_LED_COUNT 1
    #define RGB_LED_BRIGHTNESS 32

    // Button
    #ifndef BUTTON_PIN
        #define BUTTON_PIN 0
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // SHA Implementation: Defined in platformio.ini (USE_HARDWARE_SHA=1)

// ============================================================
// ESP32 Headless - No display, serial only
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
// ESP32 Headless with RGB LED
// Generic ESP32 with external NeoPixel for status
// ============================================================
#elif defined(ESP32_HEADLESS_LED)
    #define BOARD_NAME "ESP32-Headless-LED"

    #define USE_DISPLAY 0

    // RGB LED Status
    #define USE_LED_STATUS 1
    #ifndef RGB_LED_PIN
        #define RGB_LED_PIN 2  // Common GPIO for external LED
    #endif
    #define RGB_LED_TYPE_WS2812B 1
    #define RGB_LED_COUNT 1
    #define RGB_LED_BRIGHTNESS 32

    #ifndef BUTTON_PIN
        #define BUTTON_PIN 0
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // SHA Implementation: Defined in platformio.ini (USE_HARDWARE_SHA=1)

// ============================================================
// ESP32-C3 SuperMini - Single-core RISC-V (headless)
// Ultra-compact and cheap ESP32-C3 board
// ============================================================
#elif defined(ESP32_C3_SUPERMINI)
    #define BOARD_NAME "ESP32-C3-SuperMini"

    #define USE_DISPLAY 0

    // No LED status by default (can be added)
    #ifndef USE_LED_STATUS
        #define USE_LED_STATUS 0
    #endif

    #ifndef BUTTON_PIN
        #define BUTTON_PIN 9  // XIAO ESP32-C3 boot button
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // Single-core RISC-V - software SHA only
    // SHA Implementation: Uses miner_sha256.cpp (BitsyMiner software SHA)

// ============================================================
// ESP32-C3 with OLED Display (0.96" 128x64 SSD1306)
// Compact miner with small monochrome display
// ============================================================
#elif defined(ESP32_C3_OLED)
    #define BOARD_NAME "ESP32-C3-OLED"

    // Use OLED display (not TFT)
    #define USE_DISPLAY 0
    #define USE_OLED_DISPLAY 1

    // OLED configuration (128x64 SSD1306 I2C)
    #define OLED_WIDTH 128
    #define OLED_HEIGHT 64
    #define OLED_SDA_PIN 5
    #define OLED_SCL_PIN 6
    #define OLED_I2C_ADDR 0x3C

    #ifndef BUTTON_PIN
        #define BUTTON_PIN 9
    #endif
    #define BUTTON_ACTIVE_LOW 1

    // Single-core RISC-V - software SHA only
    // SHA Implementation: Uses miner_sha256.cpp (BitsyMiner software SHA)

// ============================================================
// ESP32-S3 with OLED Display (0.96" 128x64 SSD1306)
// Higher performance with small display
// ============================================================
#elif defined(ESP32_S3_OLED)
    #define BOARD_NAME "ESP32-S3-OLED"

    // Use OLED display (not TFT)
    #define USE_DISPLAY 0
    #define USE_OLED_DISPLAY 1

    // OLED configuration (128x64 SSD1306 I2C)
    #define OLED_WIDTH 128
    #define OLED_HEIGHT 64
    #define OLED_SDA_PIN 5
    #define OLED_SCL_PIN 6
    #define OLED_I2C_ADDR 0x3C

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

// Core 0 mining yield configuration
// Higher = more hashes per yield, but UI/WiFi may lag
// Lower = more responsive UI, but fewer hashes from Core 0
// Values: 128 (responsive), 256 (default), 512 (throughput), 1024 (max)
#ifndef CORE_0_YIELD_COUNT
    #define CORE_0_YIELD_COUNT 256
#endif

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
// NOTE: Needs large stack for WiFiClientSecure SSL context (~10-15KB)
#define STATS_CORE          CORE_0
#define STATS_PRIORITY      1
#define STATS_STACK         12000

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

/*
 * SparkMiner - LED Status Driver Implementation
 * Visual feedback via RGB LED for headless builds
 *
 * GPL v3 License
 */

#include <board_config.h>

#ifdef USE_LED_STATUS

#include <Arduino.h>
#include <FastLED.h>
#include "led_status.h"

// ============================================================
// Configuration
// ============================================================

#ifndef RGB_LED_COUNT
#define RGB_LED_COUNT 1
#endif

#ifndef RGB_LED_BRIGHTNESS
#define RGB_LED_BRIGHTNESS 32  // 0-255, keep low to reduce power/heat
#endif

// Animation timing
#define SLOW_PULSE_MS   1500   // Connecting pulse period
#define FAST_PULSE_MS   500    // Mining pulse period
#define FLASH_DURATION  200    // Share found flash
#define RAINBOW_DURATION 3000  // Block found celebration

// ============================================================
// State
// ============================================================

static CRGB s_leds[RGB_LED_COUNT];
static led_status_t s_currentStatus = LED_STATUS_OFF;
static led_status_t s_previousStatus = LED_STATUS_OFF;  // For returning after flash
static bool s_enabled = true;
static uint32_t s_lastUpdate = 0;
static uint32_t s_flashStart = 0;
static uint8_t s_brightness = 0;
static int8_t s_fadeDirection = 1;

// ============================================================
// Color Definitions (GRB format for most NeoPixels)
// ============================================================

static const CRGB COLOR_YELLOW = CRGB(255, 200, 0);
static const CRGB COLOR_BLUE   = CRGB(0, 100, 255);
static const CRGB COLOR_GREEN  = CRGB(0, 255, 50);
static const CRGB COLOR_WHITE  = CRGB(255, 255, 255);
static const CRGB COLOR_RED    = CRGB(255, 0, 0);
static const CRGB COLOR_OFF    = CRGB(0, 0, 0);

// ============================================================
// Helper Functions
// ============================================================

static void setColor(CRGB color, uint8_t brightness) {
    s_leds[0] = color;
    s_leds[0].nscale8(brightness);
    FastLED.show();
}

static void updatePulse(CRGB color, uint16_t periodMs) {
    uint32_t now = millis();
    uint32_t elapsed = now - s_lastUpdate;

    // Calculate fade step based on period
    // Full cycle = 0->255->0 = 510 steps
    // At 10ms update rate: steps per update = (510 * 10) / periodMs
    uint8_t fadeStep = max(1, (510 * 10) / periodMs);

    s_brightness += s_fadeDirection * fadeStep;

    // Bounce at limits
    if (s_brightness >= RGB_LED_BRIGHTNESS) {
        s_brightness = RGB_LED_BRIGHTNESS;
        s_fadeDirection = -1;
    } else if (s_brightness <= 10) {
        s_brightness = 10;
        s_fadeDirection = 1;
    }

    setColor(color, s_brightness);
    s_lastUpdate = now;
}

static void updateRainbow() {
    static uint8_t hue = 0;
    s_leds[0] = CHSV(hue++, 255, RGB_LED_BRIGHTNESS);
    FastLED.show();
}

// ============================================================
// Public API
// ============================================================

void led_status_init() {
    #if defined(RGB_LED_PIN)
        // Configure FastLED based on LED type
        #if defined(RGB_LED_TYPE_WS2812)
            FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(s_leds, RGB_LED_COUNT);
        #elif defined(RGB_LED_TYPE_WS2812B)
            FastLED.addLeds<WS2812B, RGB_LED_PIN, GRB>(s_leds, RGB_LED_COUNT);
        #elif defined(RGB_LED_TYPE_SK6812)
            FastLED.addLeds<SK6812, RGB_LED_PIN, GRB>(s_leds, RGB_LED_COUNT);
        #elif defined(RGB_LED_TYPE_NEOPIXEL)
            FastLED.addLeds<NEOPIXEL, RGB_LED_PIN>(s_leds, RGB_LED_COUNT);
        #else
            // Default to WS2812B (most common)
            FastLED.addLeds<WS2812B, RGB_LED_PIN, GRB>(s_leds, RGB_LED_COUNT);
        #endif

        FastLED.setBrightness(RGB_LED_BRIGHTNESS);
        FastLED.clear(true);

        s_currentStatus = LED_STATUS_BOOT;
        s_enabled = true;

        Serial.printf("[LED] Status driver initialized (pin %d)\n", RGB_LED_PIN);
    #else
        Serial.println("[LED] No RGB_LED_PIN defined, LED status disabled");
        s_enabled = false;
    #endif
}

void led_status_set(led_status_t status) {
    if (status != s_currentStatus) {
        s_previousStatus = s_currentStatus;
        s_currentStatus = status;
        s_brightness = RGB_LED_BRIGHTNESS / 2;  // Reset brightness for new state
        s_fadeDirection = 1;

        #ifdef DEBUG_LED
        const char* statusNames[] = {
            "OFF", "BOOT", "AP_MODE", "CONNECTING",
            "MINING", "SHARE", "BLOCK", "ERROR"
        };
        Serial.printf("[LED] Status: %s\n", statusNames[status]);
        #endif
    }
}

led_status_t led_status_get() {
    return s_currentStatus;
}

void led_status_share_found() {
    s_previousStatus = s_currentStatus;
    s_currentStatus = LED_STATUS_SHARE_FOUND;
    s_flashStart = millis();
    setColor(COLOR_WHITE, RGB_LED_BRIGHTNESS);
}

void led_status_block_found() {
    s_previousStatus = s_currentStatus;
    s_currentStatus = LED_STATUS_BLOCK_FOUND;
    s_flashStart = millis();
    Serial.println("[LED] BLOCK FOUND! Rainbow celebration!");
}

void led_status_update() {
    if (!s_enabled) return;

    uint32_t now = millis();

    // Handle temporary states (flash/celebration)
    if (s_currentStatus == LED_STATUS_SHARE_FOUND) {
        if (now - s_flashStart >= FLASH_DURATION) {
            s_currentStatus = s_previousStatus;
        } else {
            return;  // Keep showing white flash
        }
    }

    if (s_currentStatus == LED_STATUS_BLOCK_FOUND) {
        if (now - s_flashStart >= RAINBOW_DURATION) {
            s_currentStatus = s_previousStatus;
        } else {
            updateRainbow();
            return;
        }
    }

    // Update based on current status
    switch (s_currentStatus) {
        case LED_STATUS_OFF:
            setColor(COLOR_OFF, 0);
            break;

        case LED_STATUS_BOOT:
            setColor(COLOR_YELLOW, RGB_LED_BRIGHTNESS);
            break;

        case LED_STATUS_AP_MODE:
            updatePulse(COLOR_YELLOW, SLOW_PULSE_MS);
            break;

        case LED_STATUS_CONNECTING:
            updatePulse(COLOR_BLUE, SLOW_PULSE_MS);
            break;

        case LED_STATUS_MINING:
            updatePulse(COLOR_GREEN, FAST_PULSE_MS);
            break;

        case LED_STATUS_ERROR:
            setColor(COLOR_RED, RGB_LED_BRIGHTNESS);
            break;

        default:
            break;
    }
}

void led_status_toggle() {
    s_enabled = !s_enabled;
    if (!s_enabled) {
        FastLED.clear(true);
    }
    Serial.printf("[LED] Status feedback %s\n", s_enabled ? "enabled" : "disabled");
}

bool led_status_is_enabled() {
    return s_enabled;
}

#else // !USE_LED_STATUS

// Stub implementations when LED status is disabled
void led_status_init() {}
void led_status_set(led_status_t status) { (void)status; }
led_status_t led_status_get() { return LED_STATUS_OFF; }
void led_status_share_found() {}
void led_status_block_found() {}
void led_status_update() {}
void led_status_toggle() {}
bool led_status_is_enabled() { return false; }

#endif // USE_LED_STATUS

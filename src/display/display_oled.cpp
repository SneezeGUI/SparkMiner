/*
 * SparkMiner - OLED Display Driver Implementation
 * U8g2-based driver for small monochrome OLED displays
 *
 * GPL v3 License
 */

#include <Arduino.h>
#include <board_config.h>
#include "display/display_oled.h"

#if USE_OLED_DISPLAY

#include <U8g2lib.h>
#include <Wire.h>

// ============================================================
// Configuration
// ============================================================

// Default I2C pins (can be overridden in board_config.h)
#ifndef OLED_SDA_PIN
    #define OLED_SDA_PIN 21
#endif

#ifndef OLED_SCL_PIN
    #define OLED_SCL_PIN 22
#endif

#ifndef OLED_I2C_ADDR
    #define OLED_I2C_ADDR 0x3C
#endif

// Display dimensions (set in board_config.h)
#ifndef OLED_WIDTH
    #define OLED_WIDTH 128
#endif

#ifndef OLED_HEIGHT
    #define OLED_HEIGHT 64
#endif

// Screen cycling (OLED has limited space, fewer screens)
#define OLED_SCREEN_MAIN    0
#define OLED_SCREEN_STATS   1
#define OLED_SCREEN_COUNT   2

// ============================================================
// U8g2 Display Object
// ============================================================

// Select display constructor based on size
#if (OLED_HEIGHT == 64)
    // 128x64 SSD1306
    static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_u8g2(U8G2_R0, U8X8_PIN_NONE);
#elif (OLED_HEIGHT == 32)
    // 128x32 SSD1306
    static U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C s_u8g2(U8G2_R0, U8X8_PIN_NONE);
#else
    // Default to 128x64
    static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_u8g2(U8G2_R0, U8X8_PIN_NONE);
#endif

// ============================================================
// State
// ============================================================

static uint8_t s_currentScreen = OLED_SCREEN_MAIN;
static uint8_t s_brightness = 255;
static uint8_t s_rotation = 0;
static bool s_needsRedraw = true;
static bool s_inverted = false;

// ============================================================
// Helper Functions
// ============================================================

static String formatHashrateCompact(double hashrate) {
    if (hashrate >= 1e9) {
        return String(hashrate / 1e9, 1) + "G";
    } else if (hashrate >= 1e6) {
        return String(hashrate / 1e6, 1) + "M";
    } else if (hashrate >= 1e3) {
        return String(hashrate / 1e3, 1) + "K";
    } else {
        return String((int)hashrate);
    }
}

static String formatUptimeCompact(uint32_t seconds) {
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;

    if (days > 0) {
        return String(days) + "d" + String(hours) + "h";
    } else if (hours > 0) {
        return String(hours) + "h" + String(mins) + "m";
    } else {
        return String(mins) + "m";
    }
}

static String formatDiffCompact(double diff) {
    if (diff >= 1e12) {
        return String(diff / 1e12, 1) + "T";
    } else if (diff >= 1e9) {
        return String(diff / 1e9, 1) + "G";
    } else if (diff >= 1e6) {
        return String(diff / 1e6, 1) + "M";
    } else if (diff >= 1e3) {
        return String(diff / 1e3, 1) + "K";
    } else {
        return String((int)diff);
    }
}

// ============================================================
// Screen Drawing Functions
// ============================================================

static void drawMainScreen(const display_data_t *data) {
    s_u8g2.clearBuffer();

    // Header line with status icons
    s_u8g2.setFont(u8g2_font_6x10_tf);

    // WiFi status
    if (data->wifiConnected) {
        s_u8g2.drawStr(0, 8, "W");
    } else {
        s_u8g2.drawStr(0, 8, "-");
    }

    // Pool status
    if (data->poolConnected) {
        s_u8g2.drawStr(10, 8, "P");
    } else {
        s_u8g2.drawStr(10, 8, "-");
    }

    // Uptime (right aligned)
    String uptime = formatUptimeCompact(data->uptimeSeconds);
    int uptimeWidth = s_u8g2.getStrWidth(uptime.c_str());
    s_u8g2.drawStr(OLED_WIDTH - uptimeWidth, 8, uptime.c_str());

    // Separator line
    s_u8g2.drawHLine(0, 10, OLED_WIDTH);

    // Large hashrate display
    s_u8g2.setFont(u8g2_font_logisoso16_tn);  // Large numeric font
    String hashrate = formatHashrateCompact(data->hashRate);
    int hrWidth = s_u8g2.getStrWidth(hashrate.c_str());
    s_u8g2.drawStr((OLED_WIDTH - hrWidth) / 2, 32, hashrate.c_str());

    // "H/s" label below
    s_u8g2.setFont(u8g2_font_6x10_tf);
    s_u8g2.drawStr((OLED_WIDTH - 18) / 2, 42, "H/s");

    // Bottom stats row
    #if (OLED_HEIGHT == 64)
        s_u8g2.drawHLine(0, 48, OLED_WIDTH);

        // Shares
        String shares = "S:" + String(data->sharesAccepted);
        s_u8g2.drawStr(0, 60, shares.c_str());

        // Best diff (right)
        String best = "B:" + formatDiffCompact(data->bestDifficulty);
        int bestWidth = s_u8g2.getStrWidth(best.c_str());
        s_u8g2.drawStr(OLED_WIDTH - bestWidth, 60, best.c_str());
    #endif

    s_u8g2.sendBuffer();
}

static void drawStatsScreen(const display_data_t *data) {
    s_u8g2.clearBuffer();
    s_u8g2.setFont(u8g2_font_6x10_tf);

    // Title
    s_u8g2.drawStr(0, 8, "STATS");
    s_u8g2.drawHLine(0, 10, OLED_WIDTH);

    // Pool info
    String poolLine = "Pool: ";
    poolLine += data->poolConnected ? "OK" : "---";
    s_u8g2.drawStr(0, 22, poolLine.c_str());

    // Difficulty
    String diffLine = "Diff: " + formatDiffCompact(data->poolDifficulty);
    s_u8g2.drawStr(0, 34, diffLine.c_str());

    // Templates
    String templLine = "Tmpl: " + String(data->templates);
    s_u8g2.drawStr(0, 46, templLine.c_str());

    #if (OLED_HEIGHT == 64)
        // WiFi signal
        String rssiLine = "RSSI: ";
        if (data->wifiConnected) {
            rssiLine += String(data->wifiRssi) + "dBm";
        } else {
            rssiLine += "---";
        }
        s_u8g2.drawStr(0, 58, rssiLine.c_str());
    #endif

    s_u8g2.sendBuffer();
}

// ============================================================
// Public API Implementation
// ============================================================

void oled_display_init(uint8_t rotation, uint8_t brightness) {
    Serial.printf("[OLED] Initializing %dx%d display\n", OLED_WIDTH, OLED_HEIGHT);

    // Initialize I2C with custom pins
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    // Initialize U8g2
    s_u8g2.begin();

    // Set rotation
    s_rotation = rotation;
    if (rotation == 2) {
        s_u8g2.setDisplayRotation(U8G2_R2);  // 180 degrees
    } else {
        s_u8g2.setDisplayRotation(U8G2_R0);  // Normal
    }

    // Set brightness/contrast
    s_brightness = (brightness * 255) / 100;
    s_u8g2.setContrast(s_brightness);

    // Show boot screen
    oled_display_show_boot();

    Serial.println("[OLED] Display initialized");
}

void oled_display_update(const display_data_t *data) {
    switch (s_currentScreen) {
        case OLED_SCREEN_MAIN:
            drawMainScreen(data);
            break;
        case OLED_SCREEN_STATS:
            drawStatsScreen(data);
            break;
        default:
            drawMainScreen(data);
            break;
    }
    s_needsRedraw = false;
}

void oled_display_set_brightness(uint8_t brightness) {
    s_brightness = (brightness * 255) / 100;
    s_u8g2.setContrast(s_brightness);
}

void oled_display_next_screen() {
    s_currentScreen = (s_currentScreen + 1) % OLED_SCREEN_COUNT;
    s_needsRedraw = true;
    Serial.printf("[OLED] Screen: %d\n", s_currentScreen);
}

void oled_display_show_ap_config(const char *ssid, const char *password, const char *ip) {
    s_u8g2.clearBuffer();
    s_u8g2.setFont(u8g2_font_6x10_tf);

    s_u8g2.drawStr(0, 10, "WiFi Setup");
    s_u8g2.drawHLine(0, 12, OLED_WIDTH);

    s_u8g2.drawStr(0, 26, "SSID:");
    s_u8g2.drawStr(0, 38, ssid);

    s_u8g2.drawStr(0, 52, "Pass:");
    s_u8g2.drawStr(30, 52, password);

    #if (OLED_HEIGHT == 64)
        s_u8g2.drawStr(0, 64, ip);
    #endif

    s_u8g2.sendBuffer();
}

void oled_display_show_boot() {
    s_u8g2.clearBuffer();

    // Center "SparkMiner" title
    s_u8g2.setFont(u8g2_font_7x14B_tf);
    const char *title = "SparkMiner";
    int titleWidth = s_u8g2.getStrWidth(title);
    s_u8g2.drawStr((OLED_WIDTH - titleWidth) / 2, 28, title);

    // Version below
    s_u8g2.setFont(u8g2_font_6x10_tf);
    const char *version = AUTO_VERSION;
    int versionWidth = s_u8g2.getStrWidth(version);
    s_u8g2.drawStr((OLED_WIDTH - versionWidth) / 2, 42, version);

    #if (OLED_HEIGHT == 64)
        s_u8g2.drawStr((OLED_WIDTH - 60) / 2, 58, "Initializing...");
    #endif

    s_u8g2.sendBuffer();
}

void oled_display_show_reset_countdown(int seconds) {
    s_u8g2.clearBuffer();
    s_u8g2.setFont(u8g2_font_7x14B_tf);

    s_u8g2.drawStr(20, 20, "FACTORY");
    s_u8g2.drawStr(28, 36, "RESET");

    s_u8g2.setFont(u8g2_font_logisoso16_tn);
    String countdown = String(seconds);
    int w = s_u8g2.getStrWidth(countdown.c_str());
    s_u8g2.drawStr((OLED_WIDTH - w) / 2, 58, countdown.c_str());

    s_u8g2.sendBuffer();
}

void oled_display_show_reset_complete() {
    s_u8g2.clearBuffer();
    s_u8g2.setFont(u8g2_font_7x14B_tf);

    s_u8g2.drawStr(28, 28, "RESET");
    s_u8g2.drawStr(16, 46, "COMPLETE");

    s_u8g2.sendBuffer();
}

void oled_display_redraw() {
    s_needsRedraw = true;
}

uint8_t oled_display_flip_rotation() {
    s_rotation = (s_rotation == 0) ? 2 : 0;

    if (s_rotation == 2) {
        s_u8g2.setDisplayRotation(U8G2_R2);
    } else {
        s_u8g2.setDisplayRotation(U8G2_R0);
    }

    s_needsRedraw = true;
    return s_rotation;
}

void oled_display_set_inverted(bool inverted) {
    s_inverted = inverted;
    // U8g2 doesn't have direct invert, we'd need to redraw with XOR
    // For now just track state
    s_needsRedraw = true;
}

uint16_t oled_display_get_width() {
    return OLED_WIDTH;
}

uint16_t oled_display_get_height() {
    return OLED_HEIGHT;
}

bool oled_display_is_portrait() {
    return false;  // OLEDs are typically landscape
}

uint8_t oled_display_get_screen() {
    return s_currentScreen;
}

void oled_display_set_screen(uint8_t screen) {
    if (screen < OLED_SCREEN_COUNT) {
        s_currentScreen = screen;
        s_needsRedraw = true;
    }
}

// ============================================================
// Display Driver Interface
// ============================================================

static DisplayDriver s_oledDriver = {
    .init = oled_display_init,
    .update = oled_display_update,
    .set_brightness = oled_display_set_brightness,
    .next_screen = oled_display_next_screen,
    .show_ap_config = oled_display_show_ap_config,
    .show_boot = oled_display_show_boot,
    .show_reset_countdown = oled_display_show_reset_countdown,
    .show_reset_complete = oled_display_show_reset_complete,
    .redraw = oled_display_redraw,
    .flip_rotation = oled_display_flip_rotation,
    .set_inverted = oled_display_set_inverted,
    .get_width = oled_display_get_width,
    .get_height = oled_display_get_height,
    .is_portrait = oled_display_is_portrait,
    .get_screen = oled_display_get_screen,
    .set_screen = oled_display_set_screen,
    .name = "U8g2 OLED"
};

DisplayDriver* oled_get_driver() {
    return &s_oledDriver;
}

// ============================================================
// Standard display API wrappers (for main.cpp compatibility)
// ============================================================

void display_init(uint8_t rotation, uint8_t brightness) {
    oled_display_init(rotation, brightness);
}

void display_update(const display_data_t *data) {
    oled_display_update(data);
}

void display_set_brightness(uint8_t brightness) {
    oled_display_set_brightness(brightness);
}

void display_set_screen(uint8_t screen) {
    oled_display_set_screen(screen);
}

uint8_t display_get_screen() {
    return oled_display_get_screen();
}

void display_next_screen() {
    oled_display_next_screen();
}

void display_redraw() {
    oled_display_redraw();
}

uint8_t display_flip_rotation() {
    return oled_display_flip_rotation();
}

uint16_t display_get_width() {
    return oled_display_get_width();
}

uint16_t display_get_height() {
    return oled_display_get_height();
}

bool display_is_portrait() {
    return oled_display_is_portrait();
}

bool display_touched() {
    return false;  // OLED has no touch
}

void display_handle_touch() {
    // No-op for OLED
}

void display_show_ap_config(const char *ssid, const char *password, const char *ip) {
    oled_display_show_ap_config(ssid, password, ip);
}

void display_set_inverted(bool inverted) {
    oled_display_set_inverted(inverted);
}

void display_show_reset_countdown(int seconds) {
    oled_display_show_reset_countdown(seconds);
}

void display_show_reset_complete() {
    oled_display_show_reset_complete();
}

#endif // USE_OLED_DISPLAY

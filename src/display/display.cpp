/*
 * SparkMiner - Display Driver Implementation
 * TFT display for CYD (Cheap Yellow Display) boards
 *
 * Author: Sneeze (github.com/SneezeGUI)
 * Based on BitsyMiner by Justin Williams (GPL v3)
 */

#include <Arduino.h>
#include <math.h>
#include <board_config.h>
#include "display.h"

#if USE_DISPLAY

#include <SPI.h>
#include <TFT_eSPI.h>
// #include <XPT2046_Touchscreen.h>  // Touch disabled - see memory bank for issues

// ============================================================
// Configuration
// ============================================================

// CYD 2.8" specific pins
#if defined(ESP32_2432S028)
    #define LCD_BL_PIN      21
    #define TOUCH_CS_PIN    33
    #define TOUCH_IRQ_PIN   36
    #define TOUCH_MOSI_PIN  32
    #define TOUCH_MISO_PIN  39
    #define TOUCH_CLK_PIN   25
#endif

// S3 CYD / Waveshare ESP32-S3-Touch-LCD-2.8 specific pins
#if defined(ESP32_S3_CYD)
    #define LCD_BL_PIN      45
#endif

// LILYGO T-Display S3 (170x320, 8-bit parallel)
#if defined(LILYGO_T_DISPLAY_S3)
    #define LCD_BL_PIN      38
#endif

// LILYGO T-Display V1 (135x240, SPI)
#if defined(LILYGO_T_DISPLAY_V1)
    #define LCD_BL_PIN      4
#endif

// M5StickC Plus2 (135x240, SPI)
#if defined(M5STICKC_PLUS2)
    #define LCD_BL_PIN      27
#endif

// PWM settings for backlight
#define LEDC_CHANNEL    0
#define LEDC_FREQ       5000
#define LEDC_RESOLUTION 12

// Colors (RGB565) - Dark Spark Theme
#define COLOR_BG        0x0000  // Pure black
#define COLOR_FG        0xFFFF  // White
#define COLOR_ACCENT    0xFD00  // Bright orange (spark core)
#define COLOR_SPARK1    0xFBE0  // Yellow-orange (spark glow)
#define COLOR_SPARK2    0xFC60  // Amber (spark edge)
#define COLOR_SUCCESS   0x07E0  // Green
#define COLOR_WARNING   0xFE00  // Yellow-orange (warning/okay)
#define COLOR_ERROR     0xF800  // Red
#define COLOR_DIM       0x528A  // Darker gray
#define COLOR_PANEL     0x10A2  // Very dark gray panel

// Layout - responsive to display size
// Small displays (135x240 T-Display V1) need tighter spacing
#if defined(LILYGO_T_DISPLAY_V1) || defined(M5STICKC_PLUS2)
    #define MARGIN          4
    #define LINE_HEIGHT     16
    #define HEADER_HEIGHT   24
    #define SMALL_DISPLAY   1
#elif defined(LILYGO_T_DISPLAY_S3)
    #define MARGIN          6
    #define LINE_HEIGHT     18
    #define HEADER_HEIGHT   30
    #define SMALL_DISPLAY   1
#else
    #define MARGIN          10
    #define LINE_HEIGHT     22
    #define HEADER_HEIGHT   40
    #define SMALL_DISPLAY   0
#endif

// ============================================================
// State
// ============================================================

static TFT_eSPI s_tft = TFT_eSPI();
static TFT_eSprite s_sprite = TFT_eSprite(&s_tft);
static TFT_eSprite s_sprite_small = TFT_eSprite(&s_tft);
static TFT_eSprite s_sprite_time = TFT_eSprite(&s_tft);
static TFT_eSprite s_headerSprite = TFT_eSprite(&s_tft);
static TFT_eSprite s_bottomSprite = TFT_eSprite(&s_tft);

// Touch controller disabled - see memory bank for implementation issues
// #if defined(ESP32_2432S028)
//     static SPIClass s_touchSpi = SPIClass(VSPI);
//     static XPT2046_Touchscreen s_touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);
// #endif

static uint8_t s_currentScreen = SCREEN_MINING;
static uint8_t s_brightness = 100;
static uint8_t s_rotation = 1;  // Current rotation (0-3)
static bool s_needsRedraw = true;
static bool s_backlightOff = false;
static display_data_t s_lastData;

// ============================================================
// Helper Functions
// ============================================================

static void recreate_sprites();
static uint8_t s_rgbRow[320 * 3];

uint16_t display_get_width() {
    return s_tft.width();
}

uint16_t display_get_height() {
    return s_tft.height();
}

bool display_is_portrait() {
    // Use actual display dimensions after rotation to determine orientation.
    return (s_tft.width() < s_tft.height());
}

static void setBacklight(uint8_t percent) {
    uint32_t duty = (4095 * percent) / 100;
    ledcWrite(LEDC_CHANNEL, duty);
}

static const char* formatHashrate(double hashrate) {
    static char buf[4][32];
    static uint8_t idx = 0;
    char *out = buf[idx++ % 4];
    if (hashrate >= 1e9) {
        snprintf(out, 32, "%.2f GH/s", hashrate / 1e9);
    } else if (hashrate >= 1e6) {
        snprintf(out, 32, "%.2f MH/s", hashrate / 1e6);
    } else if (hashrate >= 1e3) {
        snprintf(out, 32, "%.2f KH/s", hashrate / 1e3);
    } else {
        snprintf(out, 32, "%.1f H/s", hashrate);
    }
    return out;
}

static const char* formatNumber(uint64_t num) {
    if (num >= 1e12) {
        static char buf[4][32]; static uint8_t idx = 0; char *out = buf[idx++ % 4];
        snprintf(out, 32, "%.2fT", (double)num / 1e12);
        return out;
    } else if (num >= 1e9) {
        static char buf[4][32]; static uint8_t idx = 0; char *out = buf[idx++ % 4];
        snprintf(out, 32, "%.2fG", (double)num / 1e9);
        return out;
    } else if (num >= 1e6) {
        static char buf[4][32]; static uint8_t idx = 0; char *out = buf[idx++ % 4];
        snprintf(out, 32, "%.2fM", (double)num / 1e6);
        return out;
    } else if (num >= 1e3) {
        static char buf[4][32]; static uint8_t idx = 0; char *out = buf[idx++ % 4];
        snprintf(out, 32, "%.2fK", (double)num / 1e3);
        return out;
    } else {
        static char buf[4][32]; static uint8_t idx = 0; char *out = buf[idx++ % 4];
        snprintf(out, 32, "%lu", (unsigned long)num);
        return out;
    }
}

static const char* formatUptime(uint32_t seconds) {
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;
    static char buf[4][32]; static uint8_t idx = 0; char *out = buf[idx++ % 4];
    if (days > 0) {
        snprintf(out, 32, "%ud %uh", days, hours);
    } else if (hours > 0) {
        snprintf(out, 32, "%uh %um", hours, mins);
    } else {
        snprintf(out, 32, "%um %us", mins, secs);
    }
    return out;
}

static const char* formatDifficulty(double diff) {
    static char buf[4][32]; static uint8_t idx = 0; char *out = buf[idx++ % 4];
    if (diff >= 1e15) {
        snprintf(out, 32, "%.2fP", diff / 1e15);
    } else if (diff >= 1e12) {
        snprintf(out, 32, "%.2fT", diff / 1e12);
    } else if (diff >= 1e9) {
        snprintf(out, 32, "%.2fG", diff / 1e9);
    } else if (diff >= 1e6) {
        snprintf(out, 32, "%.2fM", diff / 1e6);
    } else if (diff >= 1e3) {
        snprintf(out, 32, "%.2fK", diff / 1e3);
    } else {
        snprintf(out, 32, "%.4f", diff);
    }
    return out;
}

// Color coding helpers for status indicators
// Returns: COLOR_SUCCESS (good), COLOR_WARNING (okay), COLOR_ERROR (bad)
static uint16_t getPingColor(uint32_t latencyMs) {
    if (latencyMs == 0) return COLOR_DIM;        // No data
    if (latencyMs < 200) return COLOR_SUCCESS;   // Good: <200ms
    if (latencyMs < 500) return COLOR_WARNING;   // Okay: 200-500ms
    return COLOR_ERROR;                           // Bad: >500ms
}

static uint16_t getTempColor(float tempC) {
    if (tempC < 50) return COLOR_SUCCESS;        // Good: <50C
    if (tempC < 70) return COLOR_WARNING;        // Okay: 50-70C
    return COLOR_ERROR;                           // Bad: >70C
}

static uint16_t getWifiColor(int rssi) {
    if (rssi == 0) return COLOR_ERROR;           // Not connected
    if (rssi > -60) return COLOR_SUCCESS;        // Excellent: >-60dBm
    if (rssi > -75) return COLOR_WARNING;        // Okay: -60 to -75dBm
    return COLOR_ERROR;                           // Bad: <-75dBm
}

// Clear a rectangular text area with given color
static void clearTextArea(int x, int y, int w, int h, uint16_t color) {
    s_tft.fillRect(x, y, w, h, color);
}

static bool safeStrEqual(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static bool display_data_changed(const display_data_t *newData, const display_data_t *oldData) {
    if (newData->totalHashes != oldData->totalHashes) return true;
    if (fabs(newData->hashRate - oldData->hashRate) > 1.0) return true;
    if (newData->bestDifficulty != oldData->bestDifficulty) return true;
    if (newData->sharesAccepted != oldData->sharesAccepted) return true;
    if (newData->sharesRejected != oldData->sharesRejected) return true;
    if (newData->templates != oldData->templates) return true;
    if (newData->blocksFound != oldData->blocksFound) return true;
    if (newData->uptimeSeconds != oldData->uptimeSeconds) return true;
    if (newData->avgLatency != oldData->avgLatency) return true;
    if (newData->poolConnected != oldData->poolConnected) return true;
    if (newData->poolFailovers != oldData->poolFailovers) return true;
    if (newData->poolDifficulty != oldData->poolDifficulty) return true;
    if (newData->poolWorkersTotal != oldData->poolWorkersTotal) return true;
    if (!safeStrEqual(newData->poolName, oldData->poolName)) return true;
    if (newData->wifiConnected != oldData->wifiConnected) return true;
    if (newData->wifiRssi != oldData->wifiRssi) return true;
    if (!safeStrEqual(newData->ipAddress, oldData->ipAddress)) return true;
    if (newData->btcPrice != oldData->btcPrice) return true;
    if (newData->blockHeight != oldData->blockHeight) return true;
    if (newData->halfHourFee != oldData->halfHourFee) return true;
    if (memcmp(newData->networkHashrate, oldData->networkHashrate, sizeof(newData->networkHashrate)) != 0) return true;
    if (memcmp(newData->networkDifficulty, oldData->networkDifficulty, sizeof(newData->networkDifficulty)) != 0) return true;
    if (memcmp(newData->poolHashrate, oldData->poolHashrate, sizeof(newData->poolHashrate)) != 0) return true;
    if (memcmp(newData->workerHashrate, oldData->workerHashrate, sizeof(newData->workerHashrate)) != 0) return true;
    if (memcmp(newData->addressBestDiff, oldData->addressBestDiff, sizeof(newData->addressBestDiff)) != 0) return true;
    return false;
}

static void recreate_sprites() {
    int w = s_tft.width();

    // Header sprite
    s_headerSprite.deleteSprite();
    if (w > 0) {
        if (s_headerSprite.createSprite(w, HEADER_HEIGHT)) {
            s_headerSprite.fillSprite(COLOR_PANEL);
        }
    }

    // Bottom sprite (portrait only)
    s_bottomSprite.deleteSprite();
    if (display_is_portrait()) {
        int barHeight = 32;
        if (s_bottomSprite.createSprite(w, barHeight)) {
            s_bottomSprite.fillSprite(COLOR_PANEL);
        }
    }

    // Small sprite for hashrate
    s_sprite_small.deleteSprite();
    int hrW = w - 2 * MARGIN;
    if (hrW < 64) hrW = 64;
    if (s_sprite_small.createSprite(hrW, 24)) {
        s_sprite_small.fillSprite(COLOR_PANEL);
    }

    // Time sprite
    s_sprite_time.deleteSprite();
    int timeW = w - 2 * MARGIN;
    if (SMALL_DISPLAY) {
        if (timeW > 120) timeW = 120;
        if (timeW < 64) timeW = 64;
    } else {
        if (timeW < 220) timeW = max(timeW, 120);
    }
    if (s_sprite_time.createSprite(timeW, 36)) {
        s_sprite_time.fillSprite(COLOR_PANEL);
    }
}

// ============================================================
// Spark Logo Drawing
// ============================================================

// 16x16 lightning bolt bitmap (1 = pixel on)
// Clean simple design with 2 jogs
static const uint16_t BOLT_W = 16;
static const uint16_t BOLT_H = 16;
static const uint8_t boltBitmap[] = {
    0b00000000, 0b00110000,  // row 0:           ##
    0b00000000, 0b01100000,  // row 1:          ##
    0b00000000, 0b11000000,  // row 2:         ##
    0b00000001, 0b10000000,  // row 3:        ##
    0b00000011, 0b11110000,  // row 4:       ######  <- first jog
    0b00000000, 0b11110000,  // row 5:         ####
    0b00000001, 0b10000000,  // row 6:        ##
    0b00000011, 0b00000000,  // row 7:       ##
    0b00000111, 0b11000000,  // row 8:      #####   <- second jog
    0b00000001, 0b11000000,  // row 9:        ###
    0b00000011, 0b00000000,  // row 10:       ##
    0b00000110, 0b00000000,  // row 11:      ##
    0b00001000, 0b00000000,  // row 12:     #       <- point
    0b00000000, 0b00000000,  // row 13:
    0b00000000, 0b00000000,  // row 14:
    0b00000000, 0b00000000,  // row 15:
};

static void drawSparkLogo(int x, int y, int size, TFT_eSprite *target = nullptr) {
    // Draw the lightning bolt bitmap scaled to fit
    float scale = (float)size / BOLT_H;

    for (int row = 0; row < BOLT_H; row++) {
        // 16-bit wide bitmap: 2 bytes per row
        uint8_t b1 = boltBitmap[row * 2];
        uint8_t b2 = boltBitmap[row * 2 + 1];
        uint16_t rowBits = (b1 << 8) | b2;

        for (int col = 0; col < BOLT_W; col++) {
            if (rowBits & (0x8000 >> col)) {
                int px = x + (int)(col * scale);
                int py = y + (int)(row * scale);
                int pw = (int)scale + 1;
                int ph = (int)scale + 1;
                if (target) {
                    target->fillRect(px, py, pw, ph, COLOR_SPARK1);
                } else {
                    s_tft.fillRect(px, py, pw, ph, COLOR_SPARK1);
                }
            }
        }
    }
}

// ============================================================
// Version Helpers
// ============================================================

// Extract major version number from AUTO_VERSION (e.g., "1.2.0" -> 1)
static int getMajorVersion() {
    const char* ver = AUTO_VERSION;
    // Skip 'v' prefix if present
    if (ver[0] == 'v' || ver[0] == 'V') ver++;
    return atoi(ver);
}

// ============================================================
// Screen Drawing Functions
// ============================================================

static void drawHeader(const display_data_t *data) {
    int w = display_get_width();
    bool isPortrait = display_is_portrait();
    bool useSprite = (s_headerSprite.width() == w);

    // All vertical offsets below were originally hardcoded for a 40px-tall
    // header (HEADER_HEIGHT on CYD-class boards). On SMALL_DISPLAY boards
    // HEADER_HEIGHT is only 24px, so everything is scaled/shifted to fit
    // within the actual header height instead of bleeding past it.
    bool compact = (HEADER_HEIGHT < 32);
    int logoSize   = compact ? 16 : 30;
    int logoY      = compact ? 3  : 5;
    int titleY     = compact ? 3  : 12;
    int verBadgeY  = compact ? 15 : 16;
    int iconLabelY = compact ? 1  : 6;
    int circleY    = compact ? 15 : 26;
    int circleR    = compact ? 4  : 5;
    int valueY     = compact ? 12 : 18;   // top of the cleared value rect
    int valueTextY = compact ? 14 : 22;
    int tempY      = compact ? 2  : 12;   // top of the cleared temp rect
    int tempTextY  = compact ? 4  : 16;

    // Use persistent sprite for the header if available
    if (useSprite) {
        s_headerSprite.fillSprite(COLOR_PANEL);
    } else {
        s_tft.fillRect(0, 0, w, HEADER_HEIGHT, COLOR_PANEL);
    }

    // Spark logo
    drawSparkLogo(8, logoY, logoSize, useSprite ? &s_headerSprite : nullptr);

    // Title with spark gradient effect. On narrow displays (SMALL_DISPLAY,
    // ~240px wide in landscape vs ~320px on CYD-class boards) the full
    // "SparkMiner" title plus version badge overruns into the status
    // indicators on the right - shorten to "Spark" and drop the version
    // badge from the header on these boards to leave room.
    if (useSprite) {
        s_headerSprite.setTextColor(COLOR_ACCENT);
        s_headerSprite.setTextSize(2);
        s_headerSprite.setCursor(42, titleY);
        s_headerSprite.print("Spark");
        if (!compact) {
            s_headerSprite.setTextColor(COLOR_SPARK1);
            s_headerSprite.print("Miner");
        }

        // Major version badge - only where there's room for it
        if (!compact) {
            s_headerSprite.setTextSize(1);
            s_headerSprite.setTextColor(COLOR_DIM);
            s_headerSprite.setCursor(162, verBadgeY);
            s_headerSprite.print("V");
            s_headerSprite.print(getMajorVersion());
        }
    } else {
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.setTextSize(2);
        s_tft.setCursor(42, titleY);
        s_tft.print("Spark");
        if (!compact) {
            s_tft.setTextColor(COLOR_SPARK1);
            s_tft.print("Miner");
            s_tft.setTextSize(1);
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(162, verBadgeY);
            s_tft.print("V");
            s_tft.print(getMajorVersion());
        }
    }

    if (!isPortrait) {
        // Status indicators (right side) - compact layout with color coding
        // Layout: Temp | WAN | POOL (right to left from right edge).
        // Block widths/spacing scale down on narrow (SMALL_DISPLAY) screens
        // so all three indicators fit without overlapping the title - on a
        // 240px-wide M5StickC Plus2 there isn't room for the same fixed
        // 40/45/28px spacing used on 320px-wide CYD boards.
        if (useSprite) {
            s_headerSprite.setTextSize(1);
        } else {
            s_tft.setTextSize(1);
        }
        int rightEdge = w - MARGIN;
        int valueBoxW = compact ? 24 : 40;
        // poolX chosen so the value box (ping/rssi number) never runs past
        // rightEdge: poolX + 15 (offset to value box) + valueBoxW <= rightEdge
        int poolX = compact ? (rightEdge - 15 - valueBoxW) : (rightEdge - 40);
        int blockGap = compact ? (15 + valueBoxW + 6) : 45;  // +6px breathing room between blocks
        int tempBoxW = compact ? 26 : 40;
        int tempGap  = compact ? (tempBoxW + 6) : 28;
        if (useSprite) {
            s_headerSprite.setTextColor(COLOR_DIM);
            s_headerSprite.setCursor(poolX, iconLabelY);
            s_headerSprite.print("POOL");
        } else {
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(poolX, iconLabelY);
            s_tft.print("POOL");
        }
        uint16_t pingColor = data->poolConnected ? getPingColor(data->avgLatency) : COLOR_ERROR;
        if (data->poolConnected && data->poolFailovers > 0) pingColor = COLOR_WARNING;
        if (useSprite) {
            s_headerSprite.fillCircle(poolX + 6, circleY, circleR, pingColor);
            if (data->poolConnected && data->avgLatency > 0) {
                s_headerSprite.fillRect(poolX + 15, valueY, valueBoxW, 12, COLOR_PANEL);
                s_headerSprite.setTextColor(pingColor);
                s_headerSprite.setCursor(poolX + 15, valueTextY);
                s_headerSprite.print(data->avgLatency);
            }
        } else {
            s_tft.fillCircle(poolX + 6, circleY, circleR, pingColor);
            if (data->poolConnected && data->avgLatency > 0) {
                clearTextArea(poolX + 15, valueY, valueBoxW, 12, COLOR_PANEL);
                s_tft.setTextColor(pingColor);
                s_tft.setCursor(poolX + 15, valueTextY);
                s_tft.print(data->avgLatency);
            }
        }

        // WAN status - middle (color coded by signal strength)
        int wanX = poolX - blockGap;
        uint16_t wifiColor = data->wifiConnected ? getWifiColor(data->wifiRssi) : COLOR_ERROR;
        if (useSprite) {
            s_headerSprite.setTextColor(COLOR_DIM);
            s_headerSprite.setCursor(wanX, iconLabelY);
            s_headerSprite.print("WAN");
            s_headerSprite.fillCircle(wanX + 6, circleY, circleR, wifiColor);
            if (data->wifiConnected) {
                s_headerSprite.fillRect(wanX + 15, valueY, valueBoxW, 12, COLOR_PANEL);
                s_headerSprite.setTextColor(wifiColor);
                s_headerSprite.setCursor(wanX + 15, valueTextY);
                s_headerSprite.print(data->wifiRssi);
            }
        } else {
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(wanX, iconLabelY);
            s_tft.print("WAN");
            s_tft.fillCircle(wanX + 6, circleY, circleR, wifiColor);
            if (data->wifiConnected) {
                clearTextArea(wanX + 15, valueY, valueBoxW, 12, COLOR_PANEL);
                s_tft.setTextColor(wifiColor);
                s_tft.setCursor(wanX + 15, valueTextY);
                s_tft.print(data->wifiRssi);
            }
        }

        // Temperature - compact with color coding
        float temp = temperatureRead();
        uint16_t tempColor = getTempColor(temp);
        int tempX = wanX - tempGap;
        // Clear temp area then print to avoid leftover digits
        if (useSprite) {
            s_headerSprite.fillRect(tempX, tempY, tempBoxW, 16, COLOR_PANEL);
            s_headerSprite.setTextColor(tempColor);   // was missing: sprite kept the previous WAN color
            s_headerSprite.setCursor(tempX, tempTextY);
            s_headerSprite.print((int)temp);
            s_headerSprite.print("C");
        } else {
            clearTextArea(tempX, tempY, tempBoxW, 16, COLOR_PANEL);
            s_tft.setTextColor(tempColor);
            s_tft.setCursor(tempX, tempTextY);
            s_tft.print((int)temp);
            s_tft.print("C");
        }
    }
    
    // Push header sprite if used
    if (useSprite) {
        s_headerSprite.pushSprite(0, 0);
        // keep sprite allocated for reuse
    }
}

static void drawBottomStatusBar(const display_data_t *data) {
    if (!display_is_portrait()) return;

    int w = display_get_width();
    int h = display_get_height();
    int barHeight = SMALL_DISPLAY ? 28 : 32;
    int y = h - barHeight;

    // Use persistent bottom sprite if available
    if (s_bottomSprite.width() == w) {
        s_bottomSprite.fillSprite(COLOR_PANEL);
        s_bottomSprite.setTextSize(1);
    } else {
        s_tft.fillRect(0, y, w, barHeight, COLOR_PANEL);
        s_tft.drawFastHLine(0, y, w, COLOR_SPARK2);
        s_tft.setTextSize(1);
    }
    int centerY = y + (barHeight / 2) - 5;

    // Layout: evenly space 3 sections across width
    // Section 1 (left): Temperature (color coded)
    // Section 2 (center): WAN + indicator (color coded by signal)
    // Section 3 (right): POOL + indicator (color coded by ping)
    int sectionW = w / 3;

    // Temperature on left - color coded with label
    float temp = temperatureRead();
    uint16_t tempColor = getTempColor(temp);
    if (s_bottomSprite.width() == w) {
        s_bottomSprite.setTextColor(COLOR_DIM);
        s_bottomSprite.setCursor(MARGIN, centerY - 6);
        s_bottomSprite.print("TEMP");
        s_bottomSprite.setTextColor(tempColor);
        s_bottomSprite.fillRect(MARGIN, centerY - 2, 40, 16, COLOR_PANEL);
        s_bottomSprite.setCursor(MARGIN, centerY + 6);
        s_bottomSprite.print((int)temp);
        s_bottomSprite.print("C");
    } else {
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN, centerY - 6);
        s_tft.print("TEMP");
        s_tft.setTextColor(tempColor);
        clearTextArea(MARGIN, centerY - 2, 40, 16, COLOR_PANEL);
        s_tft.setCursor(MARGIN, centerY + 6);
        s_tft.print((int)temp);
        s_tft.print("C");
    }

    // WAN Status - center section (color coded by signal strength)
    int wanX = sectionW;
    uint16_t wifiColor = data->wifiConnected ? getWifiColor(data->wifiRssi) : COLOR_ERROR;
    if (s_bottomSprite.width() == w) {
        s_bottomSprite.setTextColor(COLOR_DIM);
        s_bottomSprite.setCursor(wanX, centerY - 6);
        s_bottomSprite.print("WAN");
        s_bottomSprite.fillCircle(wanX + 4, centerY + 8, 4, wifiColor);
        if (data->wifiConnected) {
            s_bottomSprite.setTextColor(wifiColor);
            s_bottomSprite.setCursor(wanX + 12, centerY + 4);
            s_bottomSprite.print(data->wifiRssi);
        }
    } else {
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(wanX, centerY - 6);
        s_tft.print("WAN");
        s_tft.fillCircle(wanX + 4, centerY + 8, 4, wifiColor);
        if (data->wifiConnected) {
            s_tft.setTextColor(wifiColor);
            s_tft.setCursor(wanX + 12, centerY + 4);
            s_tft.print(data->wifiRssi);
        }
    }

    // POOL Status - right section (color coded by ping)
    int poolX = sectionW * 2;
    uint16_t pingColor = data->poolConnected ? getPingColor(data->avgLatency) : COLOR_ERROR;
    if (data->poolConnected && data->poolFailovers > 0) pingColor = COLOR_WARNING;
    if (s_bottomSprite.width() == w) {
        s_bottomSprite.setTextColor(COLOR_DIM);
        s_bottomSprite.setCursor(poolX, centerY - 6);
        s_bottomSprite.print("POOL");
        s_bottomSprite.fillCircle(poolX + 4, centerY + 8, 4, pingColor);
        if (data->poolConnected && data->avgLatency > 0) {
            s_bottomSprite.fillRect(poolX + 14, centerY, 40, 12, COLOR_PANEL);
            s_bottomSprite.setTextColor(pingColor);
            s_bottomSprite.setCursor(poolX + 14, centerY + 4);
            s_bottomSprite.print(data->avgLatency);
        }

        // Push bottom sprite (keep allocated for reuse)
        s_bottomSprite.pushSprite(0, y);
    } else {
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(poolX, centerY - 6);
        s_tft.print("POOL");
        s_tft.fillCircle(poolX + 4, centerY + 8, 4, pingColor);
        if (data->poolConnected && data->avgLatency > 0) {
            clearTextArea(poolX + 14, centerY, 40, 12, COLOR_PANEL);
            s_tft.setTextColor(pingColor);
            s_tft.setCursor(poolX + 14, centerY + 4);
            s_tft.print(data->avgLatency);
        }
    }
}

static void drawMiningScreen(const display_data_t *data) {
    int w = display_get_width();
    int h = display_get_height();
    int y = HEADER_HEIGHT + 8;
    bool isPortrait = display_is_portrait();

    if (SMALL_DISPLAY) {
        // This layout was originally sized for the T-Display V1 run in
        // portrait (h=240). The M5StickC Plus2 shares this same code path
        // but is forced into landscape (h=135) - about 45% less vertical
        // room. When the screen is this short, tighten every gap and
        // collapse the bottom info panel from 3 lines to 2 (dropping the
        // LAN/IP line, which is still shown on the WiFi setup screen) so
        // nothing is drawn past the visible bottom edge.
        bool tinyHeight = (h < 160);

        // Compact layout for small small displays
        int topPanelH = 32;   // must stay >= 32: the hashrate sprite below is a fixed 24px tall, pushed 4px from the top
        s_tft.fillRoundRect(MARGIN - 4, y - 4, w - 2 * MARGIN + 8, topPanelH, 4, COLOR_PANEL);
        s_tft.drawRoundRect(MARGIN - 4, y - 4, w - 2 * MARGIN + 8, topPanelH, 4, COLOR_ACCENT);

        s_sprite_small.fillSprite(COLOR_PANEL);
        s_sprite_small.setTextColor(COLOR_ACCENT, COLOR_PANEL);
        s_sprite_small.setTextSize(2);
        s_sprite_small.setCursor(0, 4);
        s_sprite_small.print(formatHashrate(data->hashRate));
        s_sprite_small.pushSprite(MARGIN + 4, y + 4);

        int sharesX = w - MARGIN - 68;
        if (sharesX < MARGIN) sharesX = MARGIN;
        int sharesW = w - MARGIN - sharesX;
        s_tft.setTextSize(1);
        s_tft.setTextColor(COLOR_DIM);
        int labelWidth = s_tft.textWidth("Shares");
        s_tft.setCursor(sharesX + max(0, (sharesW - labelWidth) / 2), y + 2);
        s_tft.print("Shares");
        char sharesBuf[24];
        snprintf(sharesBuf, sizeof(sharesBuf), "%u/%u", data->sharesAccepted, data->sharesAccepted + data->sharesRejected);
        clearTextArea(sharesX, y + 16, sharesW, 12, COLOR_PANEL);
        s_tft.setTextColor(COLOR_FG);
        int valueWidth = s_tft.textWidth(sharesBuf);
        s_tft.setCursor(sharesX + max(0, (sharesW - valueWidth) / 2), y + 16);
        s_tft.print(sharesBuf);

        y += topPanelH + (tinyHeight ? 4 : 6);
        s_tft.setTextSize(1);

        char bufJobs[16];
        char bufBlocks[16];
        snprintf(bufJobs, sizeof(bufJobs), "%u", data->templates);
        snprintf(bufBlocks, sizeof(bufBlocks), "%u", data->blocksFound);

        struct { const char *label; const char *value; uint16_t color; } stats[] = {
            {"Best", formatDifficulty(data->bestDifficulty), COLOR_SPARK1},
            {"Uptime", formatUptime(data->uptimeSeconds), COLOR_FG},
            {"Jobs", bufJobs, COLOR_FG},
            {"Blocks", bufBlocks, COLOR_SUCCESS},
        };

        int cols = 2;
        int boxW = (w - (cols + 1) * MARGIN) / cols;
        int boxH = tinyHeight ? 18 : (LINE_HEIGHT + 6);
        int rowGap = tinyHeight ? 2 : 5;

        for (int i = 0; i < 4; i++) {
            int col = i % cols;
            int row = i / cols;
            int x = MARGIN + col * (boxW + MARGIN);
            int ly = y + row * (boxH + rowGap);

            s_tft.fillRoundRect(x - 2, ly - 2, boxW, boxH, 3, COLOR_PANEL);
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(x + 2, ly);
            s_tft.print(stats[i].label);
            s_tft.setTextColor(stats[i].color);
            s_tft.setCursor(x + 2, ly + 10);
            s_tft.print(stats[i].value);
        }

        y += 2 * (boxH + rowGap) + (tinyHeight ? 3 : 6);

        uint16_t statusColor = COLOR_ERROR;
        if (data->poolConnected) {
            statusColor = (data->poolFailovers > 0) ? COLOR_WARNING : COLOR_SUCCESS;
        }

        char poolName[20];
        if (data->poolName && data->poolName[0]) {
            strncpy(poolName, data->poolName, sizeof(poolName) - 1);
            poolName[sizeof(poolName) - 1] = '\0';
        } else {
            strncpy(poolName, "Disconnected", sizeof(poolName) - 1);
            poolName[sizeof(poolName) - 1] = '\0';
        }
        // The 12-char truncation here was left over from a tighter layout;
        // at this panel's actual width (~198px available in tinyHeight
        // mode) there's room for ~30+ chars at text size 1, so 12 was
        // cutting pool names short unnecessarily (e.g. "public-pool" ->
        // "public-poo.."). Widened to 18 chars, still with a safety margin
        // below the real available width.
        if (strlen(poolName) > 18) {
            poolName[16] = '.';
            poolName[17] = '.';
            poolName[18] = '\0';
        }

        if (tinyHeight) {
            // Not enough vertical room for 3 stacked lines - combine Pool
            // status and pool difficulty into a single 2-line panel and
            // drop the LAN/IP line (still visible on the WiFi setup screen).
            // panelH trimmed from 22 to 19: the calculated bottom margin
            // was only ~4px, which rounded-corner drawing can eat into,
            // causing this panel's border to visibly spill past the
            // screen's bottom edge.
            int panelH = 19;
            s_tft.fillRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, panelH, 4, COLOR_PANEL);
            s_tft.drawRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, panelH, 4, COLOR_SPARK2);

            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("Pool:");
            s_tft.setTextColor(statusColor);
            clearTextArea(MARGIN + 34, y, w - MARGIN - 34, 10, COLOR_PANEL);
            s_tft.setCursor(MARGIN + 34, y);
            s_tft.print(poolName);

            y += 9;
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("Diff:");
            s_tft.setTextColor(COLOR_FG);
            clearTextArea(MARGIN + 32, y, w - MARGIN - 32, 10, COLOR_PANEL);
            s_tft.setCursor(MARGIN + 32, y);
            s_tft.print(formatDifficulty(data->poolDifficulty));
        } else {
            int panelH = 36;
            s_tft.fillRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, panelH, 4, COLOR_PANEL);
            s_tft.drawRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, panelH, 4, COLOR_SPARK2);

            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("Pool:");
            s_tft.setTextColor(statusColor);
            clearTextArea(MARGIN + 34, y, w - MARGIN - 34, 12, COLOR_PANEL);
            s_tft.setCursor(MARGIN + 34, y);
            s_tft.print(poolName);

            y += 14;
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("Diff:");
            s_tft.setTextColor(COLOR_FG);
            clearTextArea(MARGIN + 32, y, w - MARGIN - 32, 12, COLOR_PANEL);
            s_tft.setCursor(MARGIN + 32, y);
            s_tft.print(formatDifficulty(data->poolDifficulty));

            y += 14;
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("LAN:");
            s_tft.setTextColor(COLOR_FG);
            const char *ip = data->ipAddress ? data->ipAddress : "---";
            clearTextArea(MARGIN + 28, y, w - MARGIN - 28, 12, COLOR_PANEL);
            s_tft.setCursor(MARGIN + 28, y);
            s_tft.print(ip);
        }

        drawBottomStatusBar(data);
        return;
    }

    // Hashrate panel with glow effect
    s_tft.fillRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 38, 4, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 38, 4, COLOR_ACCENT);

    // Sprite buffered hashrate (use persistent small sprite)
    s_sprite_small.fillSprite(COLOR_PANEL);
    s_sprite_small.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    s_sprite_small.setTextSize(2);
    s_sprite_small.setCursor(0, 4);
    s_sprite_small.print(formatHashrate(data->hashRate));
    s_sprite_small.pushSprite(MARGIN + 4, y + 6);

    // Shares on right side of hashrate panel
    // Portrait: shift toward center to fit 5+ digit share counts (e.g., "12345/12345")
    int sharesX = isPortrait ? (w - 75) : (w - 100);
    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(sharesX, y + 4);
    s_tft.print("Shares");
    s_tft.setTextColor(COLOR_FG);
    // Clear area for shares value then print
    int shareAreaW = w - sharesX - MARGIN;
    if (shareAreaW > 90) shareAreaW = 90;
    clearTextArea(sharesX, y + 8, shareAreaW, 18, COLOR_PANEL);
    s_tft.setCursor(sharesX, y + 16);
    char sharesBuf[32];
    snprintf(sharesBuf, sizeof(sharesBuf), "%u/%u", data->sharesAccepted, data->sharesAccepted + data->sharesRejected);
    s_tft.print(sharesBuf);

    y += 44;

    s_tft.setTextSize(1);

    // Stats grid with panels
    // Landscape: 3 cols x 2 rows
    // Portrait:  2 cols x 3 rows
    int cols = isPortrait ? 2 : 3;
    int boxW = (w - (cols + 1) * MARGIN) / cols;

    // Prepare small formatted values into stack buffers to avoid String allocations
    char bufJobs[16];
    char bufBlocks[16];
    snprintf(bufJobs, sizeof(bufJobs), "%u", data->templates);
    snprintf(bufBlocks, sizeof(bufBlocks), "%u", data->blocksFound);
    const char *swarmHR = (strlen(data->workerHashrate) > 0) ? data->workerHashrate : "---";

    struct { const char *label; const char *value; uint16_t color; } stats[] = {
        {"Best",     formatDifficulty(data->bestDifficulty), COLOR_SPARK1},
        {"Hashes",   formatNumber(data->totalHashes), COLOR_FG},
        {"Uptime",   formatUptime(data->uptimeSeconds), COLOR_FG},
        {"Jobs",     bufJobs, COLOR_FG},
        {"Blocks",   bufBlocks, COLOR_SUCCESS},
        {"Swarm HR", swarmHR, COLOR_SPARK1},
    };

    for (int i = 0; i < 6; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = MARGIN + col * (boxW + MARGIN);
        int ly = y + row * (LINE_HEIGHT + 12);

        // Mini panel
        s_tft.fillRoundRect(x - 2, ly - 2, boxW, LINE_HEIGHT + 8, 3, COLOR_PANEL);

        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(x + 2, ly);
        s_tft.print(stats[i].label);

        s_tft.setTextColor(stats[i].color);
        s_tft.setCursor(x + 2, ly + 11);
        s_tft.print(stats[i].value);
    }

    int gridRows = isPortrait ? 3 : 2;
    y += gridRows * (LINE_HEIGHT + 12) + 8;

    // Pool info panel
    s_tft.fillRoundRect(MARGIN - 4, y, w - 2*MARGIN + 8, 50, 4, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, y, w - 2*MARGIN + 8, 50, 4, COLOR_SPARK2);

    y += 6;

    // Pool name and status
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Pool: ");
    
    // Status color: Green if connected, Warning if failover active, Red if disconnected
    uint16_t statusColor = COLOR_ERROR;
    if (data->poolConnected) {
        statusColor = (data->poolFailovers > 0) ? COLOR_WARNING : COLOR_SUCCESS;
    }
    s_tft.setTextColor(statusColor);
    
    // Truncate pool name if needed and clear previous text area
    char poolName[32];
    if (data->poolName && data->poolName[0]) {
        strncpy(poolName, data->poolName, sizeof(poolName) - 1);
        poolName[sizeof(poolName) - 1] = '\0';
    } else {
        strncpy(poolName, "Disconnected", sizeof(poolName) - 1);
        poolName[sizeof(poolName) - 1] = '\0';
    }
    if (isPortrait && strlen(poolName) > 12) {
        poolName[10] = '.';
        poolName[11] = '.';
        poolName[12] = '\0';
    }
    int poolNameAreaW = w - (MARGIN + 40) - 100;
    if (poolNameAreaW < 0) poolNameAreaW = 0;
    clearTextArea(MARGIN + 40, y, poolNameAreaW, 14, COLOR_PANEL);
    s_tft.print(poolName);

    // Pool workers on right
    if (data->poolWorkersTotal > 0) {
        char poolWorkersBuf[32];
        snprintf(poolWorkersBuf, sizeof(poolWorkersBuf), "%u miners", data->poolWorkersTotal);
        clearTextArea(w - 90, y, 90, 14, COLOR_PANEL);
        s_tft.setTextColor(COLOR_SPARK1);
        s_tft.setCursor(w - 90, y);
        s_tft.print(poolWorkersBuf);
    }

    y += 14;

    // Pool difficulty
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Diff: ");
    s_tft.setTextColor(COLOR_FG);
    // Clear and print difficulty to avoid leftover digits
    clearTextArea(MARGIN + 40, y - 2, 120, 14, COLOR_PANEL);
    s_tft.print(formatDifficulty(data->poolDifficulty));

    // Fee rate on right side of Diff line
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(w - 90, y);
    s_tft.print("Fee: ");
    s_tft.setTextColor(COLOR_SPARK2);
    char feeBuf[16];
    if (data->halfHourFee > 0) {
        snprintf(feeBuf, sizeof(feeBuf), "%u sat", data->halfHourFee);
    } else {
        strncpy(feeBuf, "---", sizeof(feeBuf));
    }
    clearTextArea(w - 50, y, 50, 14, COLOR_PANEL);
    s_tft.print(feeBuf);

    y += 14;

    // Pool hashrate (left) and block height (right)
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Pool: ");
    s_tft.setTextColor(COLOR_SPARK1);
    s_tft.print((strlen(data->poolHashrate) > 0) ? data->poolHashrate : "---");

    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(w - 90, y);
    s_tft.print("Block: ");
    s_tft.setTextColor(COLOR_FG);
    char blockBuf[16];
    if (data->blockHeight > 0) {
        snprintf(blockBuf, sizeof(blockBuf), "%u", data->blockHeight);
    } else {
        strncpy(blockBuf, "---", sizeof(blockBuf));
    }
    clearTextArea(w - 50, y, 50, 14, COLOR_PANEL);
    s_tft.print(blockBuf);

    y += 22;

    // LAN Info panel
    s_tft.fillRoundRect(MARGIN - 4, y, w - 2*MARGIN + 8, 24, 4, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, y, w - 2*MARGIN + 8, 24, 4, COLOR_SPARK2);

    y += 5;
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("LAN: ");
    s_tft.setTextColor(COLOR_FG);
    const char *ip = data->ipAddress ? data->ipAddress : "---";
    s_tft.print(ip);

    drawBottomStatusBar(data);
}

static void drawStatsScreen(const display_data_t *data) {
    int w = display_get_width();
    int y = HEADER_HEIGHT + 8;

    if (SMALL_DISPLAY) {
        int h = display_get_height();
        bool tinyHeight = (h < 160);

        int topH = tinyHeight ? 30 : 36;
        s_tft.fillRoundRect(MARGIN - 4, y - 4, w - 2 * MARGIN + 8, topH, 4, COLOR_PANEL);
        s_tft.drawRoundRect(MARGIN - 4, y - 4, w - 2 * MARGIN + 8, topH, 4, COLOR_SPARK1);

        s_tft.setTextSize(2);
        s_tft.setTextColor(COLOR_SPARK1);
        int priceY = y + (tinyHeight ? 4 : 6);
        if (data->btcPrice > 0) {
            char priceBuf[16];
            snprintf(priceBuf, sizeof(priceBuf), "$%.0f", data->btcPrice);
            s_tft.setCursor(MARGIN + 4, priceY);
            s_tft.print(priceBuf);
        } else {
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 4, priceY);
            s_tft.print("Loading...");
        }

        s_tft.setTextSize(1);
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(w - 55, y + (tinyHeight ? 2 : 4));
        s_tft.print("Blk");
        s_tft.setTextColor(COLOR_FG);
        char blockBuf[16];
        if (data->blockHeight > 0) {
            snprintf(blockBuf, sizeof(blockBuf), "%u", data->blockHeight);
        } else {
            strncpy(blockBuf, "---", sizeof(blockBuf));
            blockBuf[sizeof(blockBuf) - 1] = '\0';
        }
        s_tft.setCursor(w - 55, y + (tinyHeight ? 14 : 18));
        s_tft.print(blockBuf);

        y += topH + (tinyHeight ? 4 : 8);

        char feeBuf[16];
        if (data->halfHourFee > 0) {
            snprintf(feeBuf, sizeof(feeBuf), "%u sat", data->halfHourFee);
        } else {
            strncpy(feeBuf, "---", sizeof(feeBuf));
            feeBuf[sizeof(feeBuf) - 1] = '\0';
        }

        if (tinyHeight) {
            // Only ~135px of total height is available on boards like the
            // M5StickC Plus2 (landscape). Put each label+value on a single
            // line instead of stacking them, so the network panel takes
            // 2 rows instead of 4.
            int midH = 26;
            s_tft.fillRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, midH, 4, COLOR_PANEL);
            s_tft.drawRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, midH, 4, COLOR_SPARK1);

            s_tft.setTextSize(1);
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("Net:");
            s_tft.setTextColor(COLOR_FG);
            s_tft.setCursor(MARGIN + 28, y);
            s_tft.print(strlen(data->networkHashrate) > 0 ? data->networkHashrate : "---");
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(w - 60, y);
            s_tft.print("Fee:");
            s_tft.setTextColor(COLOR_SPARK2);
            clearTextArea(w - 42, y, 42, 12, COLOR_PANEL);
            s_tft.setCursor(w - 42, y);
            s_tft.print(feeBuf);

            y += 12;
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("Diff:");
            s_tft.setTextColor(COLOR_FG);
            s_tft.setCursor(MARGIN + 28, y);
            s_tft.print(strlen(data->networkDifficulty) > 0 ? data->networkDifficulty : "---");

            y += midH - 12 + 4;
            int bottomH = 36;
            s_tft.fillRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, bottomH, 4, COLOR_PANEL);
            s_tft.drawRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, bottomH, 4, COLOR_ACCENT);

            s_tft.setTextColor(COLOR_ACCENT);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("Your Mining");
            if (data->poolWorkersTotal > 0) {
                char poolWorkersBuf[24];
                snprintf(poolWorkersBuf, sizeof(poolWorkersBuf), "%u on pool", data->poolWorkersTotal);
                s_tft.setTextColor(COLOR_SPARK1);
                clearTextArea(w - 78, y, 78, 12, COLOR_PANEL);
                s_tft.setCursor(w - 78, y);
                s_tft.print(poolWorkersBuf);
            }

            y += 12;
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("Rate:");
            s_tft.setTextColor(COLOR_FG);
            s_tft.setCursor(MARGIN + 32, y);
            s_tft.print(formatHashrate(data->hashRate));
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(w - 70, y);
            s_tft.print("Best:");
            s_tft.setTextColor(COLOR_SPARK1);
            clearTextArea(w - 40, y, 40, 12, COLOR_PANEL);
            s_tft.setCursor(w - 40, y);
            s_tft.print(formatDifficulty(data->bestDifficulty));

            y += 12;
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(MARGIN + 2, y);
            s_tft.print("Shares:");
            char sharesAcceptedBuf[16];
            snprintf(sharesAcceptedBuf, sizeof(sharesAcceptedBuf), "%u", data->sharesAccepted);
            s_tft.setTextColor(COLOR_FG);
            clearTextArea(MARGIN + 46, y, w - MARGIN - 46, 12, COLOR_PANEL);
            s_tft.setCursor(MARGIN + 46, y);
            s_tft.print(sharesAcceptedBuf);

            drawBottomStatusBar(data);
            return;
        }

        int midH = 48;
        s_tft.fillRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, midH, 4, COLOR_PANEL);
        s_tft.drawRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, midH, 4, COLOR_SPARK1);

        s_tft.setTextSize(1);
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN + 2, y);
        s_tft.print("Network:");
        s_tft.setTextColor(COLOR_FG);
        s_tft.setCursor(MARGIN + 2, y + 12);
        s_tft.print(strlen(data->networkHashrate) > 0 ? data->networkHashrate : "---");

        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN + 2, y + 24);
        s_tft.print("Diff:");
        s_tft.setTextColor(COLOR_FG);
        s_tft.setCursor(MARGIN + 2, y + 36);
        s_tft.print(strlen(data->networkDifficulty) > 0 ? data->networkDifficulty : "---");

        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(w - 60, y + 4);
        s_tft.print("Fee:");
        s_tft.setTextColor(COLOR_SPARK2);
        clearTextArea(w - 42, y + 4, 42, 12, COLOR_PANEL);
        s_tft.setCursor(w - 42, y + 4);
        s_tft.print(feeBuf);

        y += midH + 8;
        int bottomH = 56;
        s_tft.fillRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, bottomH, 4, COLOR_PANEL);
        s_tft.drawRoundRect(MARGIN - 4, y - 2, w - 2 * MARGIN + 8, bottomH, 4, COLOR_ACCENT);

        s_tft.setTextSize(1);
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.setCursor(MARGIN + 2, y);
        s_tft.print("Your Mining");

        if (data->poolWorkersTotal > 0) {
            char poolWorkersBuf[24];
            snprintf(poolWorkersBuf, sizeof(poolWorkersBuf), "%u on pool", data->poolWorkersTotal);
            s_tft.setTextColor(COLOR_SPARK1);
            s_tft.setCursor(w - 78, y);
            clearTextArea(w - 78, y, 78, 12, COLOR_PANEL);
            s_tft.print(poolWorkersBuf);
        }

        y += 14;
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN + 2, y);
        s_tft.print("Rate:");
        s_tft.setTextColor(COLOR_FG);
        s_tft.setCursor(MARGIN + 36, y);
        s_tft.print(formatHashrate(data->hashRate));

        y += 14;
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN + 2, y);
        s_tft.print("Best:");
        s_tft.setTextColor(COLOR_SPARK1);
        s_tft.setCursor(MARGIN + 36, y);
        s_tft.print(formatDifficulty(data->bestDifficulty));

        y += 14;
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN + 2, y);
        s_tft.print("Shares:");
        char sharesAcceptedBuf[16];
        snprintf(sharesAcceptedBuf, sizeof(sharesAcceptedBuf), "%u", data->sharesAccepted);
        s_tft.setTextColor(COLOR_FG);
        clearTextArea(w - 42, y, 42, 12, COLOR_PANEL);
        s_tft.setCursor(w - 42, y);
        s_tft.print(sharesAcceptedBuf);

        drawBottomStatusBar(data);
        return;
    }

    // BTC Price panel
    s_tft.fillRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 38, 4, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 38, 4, COLOR_SPARK1);

    s_tft.setTextSize(2);
    s_tft.setCursor(MARGIN + 4, y + 6);
    s_tft.setTextColor(COLOR_SPARK1);
    if (data->btcPrice > 0) {
        char priceBuf[16];
        snprintf(priceBuf, sizeof(priceBuf), "$%.0f", data->btcPrice);
        s_tft.print(priceBuf);
    } else {
        s_tft.setTextColor(COLOR_DIM);
        s_tft.print("Loading...");
    }

    // Block height on right
    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(w - 100, y + 4);
    s_tft.print("Block");
    s_tft.setTextColor(COLOR_FG);
    s_tft.setCursor(w - 100, y + 16);
    char blockBuf[16];
    if (data->blockHeight > 0) {
        snprintf(blockBuf, sizeof(blockBuf), "%u", data->blockHeight);
    } else {
        strncpy(blockBuf, "---", sizeof(blockBuf));
    }
    s_tft.print(blockBuf);

    y += 44;

    // Network stats panel
    s_tft.fillRoundRect(MARGIN - 4, y, w - 2*MARGIN + 8, 60, 4, COLOR_PANEL);

    y += 6;
    s_tft.setTextSize(1);

    // Network hashrate
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Network: ");
    s_tft.setTextColor(COLOR_FG);
    s_tft.print(strlen(data->networkHashrate) > 0 ? data->networkHashrate : "---");

    // Fee on right
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(w - 90, y);
    s_tft.print("Fee: ");
    s_tft.setTextColor(COLOR_SPARK2);
    char feeBuf[16];
    if (data->halfHourFee > 0) {
        snprintf(feeBuf, sizeof(feeBuf), "%u sat", data->halfHourFee);
    } else {
        strncpy(feeBuf, "---", sizeof(feeBuf));
        feeBuf[sizeof(feeBuf) - 1] = '\0';
    }
    clearTextArea(w - 50, y, 50, 14, COLOR_PANEL);
    s_tft.print(feeBuf);

    y += 16;

    // Difficulty
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Difficulty: ");
    s_tft.setTextColor(COLOR_FG);
    s_tft.print(strlen(data->networkDifficulty) > 0 ? data->networkDifficulty : "---");

    y += 32;

    // Your mining panel
    s_tft.fillRoundRect(MARGIN - 4, y, w - 2*MARGIN + 8, 55, 4, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, y, w - 2*MARGIN + 8, 55, 4, COLOR_ACCENT);

    y += 6;

    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Your Mining");

    // Pool workers
    if (data->poolWorkersTotal > 0) {
        s_tft.setTextColor(COLOR_SPARK1);
        s_tft.setCursor(w - 90, y);
        char poolWorkersBuf[32];
        snprintf(poolWorkersBuf, sizeof(poolWorkersBuf), "%u on pool", data->poolWorkersTotal);
        clearTextArea(w - 90, y, 90, 14, COLOR_PANEL);
        s_tft.print(poolWorkersBuf);
    }

    y += 14;

    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Rate: ");
    s_tft.setTextColor(COLOR_FG);
    s_tft.print(formatHashrate(data->hashRate));

    y += 14;

    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Best: ");
    s_tft.setTextColor(COLOR_SPARK1);
    s_tft.print(formatDifficulty(data->bestDifficulty));

    // Shares on right
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(w - 90, y);
    s_tft.print("Shares: ");
    s_tft.setTextColor(COLOR_FG);
    char sharesAcceptedBuf[16];
    snprintf(sharesAcceptedBuf, sizeof(sharesAcceptedBuf), "%u", data->sharesAccepted);
    clearTextArea(w - 45, y, 45, 14, COLOR_PANEL);
    s_tft.print(sharesAcceptedBuf);

    drawBottomStatusBar(data);
}

static void drawClockScreen(const display_data_t *data) {
    int w = display_get_width();
    int h = display_get_height();

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setTextSize(2);
        s_tft.setCursor(w / 2 - 60, h / 2 - 10);
        s_tft.print("No Time");
        return;
    }

    bool tinyHeight = (h < 160);

    int y = tinyHeight ? (HEADER_HEIGHT + 6) : (HEADER_HEIGHT + 12);
    int panelH = tinyHeight ? 46 : (SMALL_DISPLAY ? 48 : 60);
    s_tft.fillRoundRect(MARGIN - 4, y - 4, w - 2 * MARGIN + 8, panelH, 6, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, y - 4, w - 2 * MARGIN + 8, panelH, 6, COLOR_ACCENT);

    char timeStr[16];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

    s_sprite_time.fillSprite(COLOR_PANEL);
    s_sprite_time.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    s_sprite_time.setTextSize(SMALL_DISPLAY ? 3 : 4);
    int spriteW = s_sprite_time.width();
    int textW = s_sprite_time.textWidth(timeStr);
    s_sprite_time.setCursor(max(0, (spriteW - textW) / 2), 4);
    s_sprite_time.print(timeStr);
    s_sprite_time.pushSprite(w / 2 - spriteW / 2, y + 6);

    y += panelH + (tinyHeight ? 4 : 8);

    int summaryY;
    if (tinyHeight) {
        // Not enough room for a separate date line plus the summary panel
        // on a short landscape screen (e.g. M5StickC Plus2) - the date used
        // to be drawn here, but the summary panel below was positioned with
        // a fixed h-55 offset that landed on top of it and painted over it
        // every frame. Drop the date (time + mining stats matter more on
        // a screen this small) and stack the summary panel right after
        // the clock instead of measuring up from the bottom edge.
        summaryY = y;
    } else {
        char dateStr[32];
        strftime(dateStr, sizeof(dateStr), "%a, %b %d %Y", &timeinfo);
        s_tft.fillRect(MARGIN - 4, y, w - 2 * MARGIN + 8, 20, COLOR_BG);
        s_tft.setTextColor(COLOR_FG);
        s_tft.setTextSize(SMALL_DISPLAY ? 1 : 2);
        s_tft.setCursor(MARGIN + 2, y + 2);
        s_tft.print(dateStr);

        if (display_is_portrait()) {
            const int bottomBarHeight = 32;
            const int summaryPanelH = 46;
            const int summarySpacing = 8;
            summaryY = h - bottomBarHeight - summaryPanelH - summarySpacing;
        } else {
            summaryY = h - 55;
        }
    }
    s_tft.fillRoundRect(MARGIN - 4, summaryY, w - 2 * MARGIN + 8, 46, 4, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, summaryY, w - 2 * MARGIN + 8, 46, 4, COLOR_SPARK2);

    int lineY = summaryY + 8;
    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, lineY);
    s_tft.print("Hash:");
    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setCursor(MARGIN + 32, lineY);
    s_tft.print(formatHashrate(data->hashRate));

    if (data->btcPrice > 0) {
        char btcPriceBuf[16];
        snprintf(btcPriceBuf, sizeof(btcPriceBuf), "$%.0f", data->btcPrice);
        s_tft.setTextColor(COLOR_SPARK1);
        int priceX = w - MARGIN - s_tft.textWidth(btcPriceBuf);
        s_tft.setCursor(priceX, lineY);
        clearTextArea(priceX, lineY, w - MARGIN - priceX, 12, COLOR_PANEL);
        s_tft.print(btcPriceBuf);
    }

    lineY += 16;
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, lineY);
    s_tft.print("Shares:");
    char sharesAcceptedBuf2[16];
    snprintf(sharesAcceptedBuf2, sizeof(sharesAcceptedBuf2), "%u", data->sharesAccepted);
    s_tft.setTextColor(COLOR_FG);
    clearTextArea(MARGIN + 46, lineY, w - MARGIN - 46, 12, COLOR_PANEL);
    s_tft.setCursor(MARGIN + 46, lineY);
    s_tft.print(sharesAcceptedBuf2);

    if (data->blockHeight > 0) {
        char blockHeightBuf[16];
        snprintf(blockHeightBuf, sizeof(blockHeightBuf), "%u", data->blockHeight);
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(w - 60, lineY);
        s_tft.print("Blk");
        s_tft.setTextColor(COLOR_FG);
        clearTextArea(w - 40, lineY, 40, 12, COLOR_PANEL);
        s_tft.setCursor(w - 40, lineY);
        s_tft.print(blockHeightBuf);
    }

    drawBottomStatusBar(data);
}

// ============================================================
// Public API
// ============================================================

void display_init(uint8_t rotation, uint8_t brightness) {
    Serial.printf("[DISPLAY] Init with rotation=%d, brightness=%d\n", rotation, brightness);

    // Enable 5V power for T-Display S3 (required before display init)
    #ifdef PIN_ENABLE5V
        pinMode(PIN_ENABLE5V, OUTPUT);
        digitalWrite(PIN_ENABLE5V, HIGH);
        delay(10);  // Allow power to stabilize
    #endif

    // Initialize TFT
    s_tft.init();
    #if defined(M5STICKC_PLUS2)
        // Force portrait-friendly rotation for M5StickC Plus2 when the saved config is still the default 0
        if (rotation == 0) {
            rotation = 1;
        }
    #endif
    s_rotation = rotation;
    s_tft.setRotation(rotation);
    s_tft.fillScreen(COLOR_BG);

    // Touch controller disabled - see memory bank for implementation issues
    // #if defined(ESP32_2432S028)
    //     s_touchSpi.begin(TOUCH_CLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN);
    //     s_touch.begin(s_touchSpi);
    //     s_touch.setRotation(rotation);
    // #endif

    // Initialize backlight PWM
    #ifdef LCD_BL_PIN
        ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RESOLUTION);
        ledcAttachPin(LCD_BL_PIN, LEDC_CHANNEL);
    #endif

    s_brightness = brightness;
    setBacklight(brightness);

    // Show boot screen with spark logo
    s_tft.fillScreen(COLOR_BG);
    int w = s_tft.width();
    int h = s_tft.height();

    // Responsive boot screen layout
    #if SMALL_DISPLAY
        // Height-aware layout: boards like the M5StickC Plus2 run this same
        // SMALL_DISPLAY code path but in landscape (h=135), which is much
        // shorter than the T-Display V1 portrait case (h=240) this layout
        // was originally sized for. Scale down and shift up when short so
        // nothing gets drawn below the visible area.
        bool tinyHeight = (h < 160);
        int logoSize = tinyHeight ? 26 : 50;
        int logoY = tinyHeight ? 2 : 20;
        drawSparkLogo(w/2 - logoSize/2, logoY, logoSize);

        int titleY = logoY + logoSize + (tinyHeight ? 4 : 10);
        s_tft.setTextSize(2);
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.setCursor(w/2 - 60, titleY);
        s_tft.print("Spark");
        s_tft.setTextColor(COLOR_SPARK1);
        s_tft.print("Miner");
    #else
        // Full-size logo for CYD boards
        drawSparkLogo(w/2 - 40, 40, 80);

        s_tft.setTextSize(3);
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.setCursor(w/2 - 90, 130);
        s_tft.print("Spark");
        s_tft.setTextColor(COLOR_SPARK1);
        s_tft.print("Miner");
    #endif

    // Version and tagline - responsive layout
    #if SMALL_DISPLAY
        // Compact version display for small screens, stacked directly
        // below the title and clipped to whatever height remains so
        // nothing is drawn past the bottom edge of a short landscape panel.
        int infoY = titleY + 20;
        s_tft.setTextSize(1);
        s_tft.setTextColor(COLOR_SPARK2);
        s_tft.setCursor(w/2 - 35, infoY);
        s_tft.print("V");
        s_tft.print(getMajorVersion());
        s_tft.setTextColor(COLOR_DIM);
        s_tft.print(" (" AUTO_VERSION ")");

        // Shorter tagline (only if it fits above the bottom edge)
        if (infoY + 18 <= h - 10) {
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(w/2 - 45, infoY + 18);
            s_tft.print("Solo BTC Mining");
        }

        // Credits (only if it fits above the bottom edge)
        if (infoY + 36 <= h - 4) {
            s_tft.setTextColor(COLOR_SPARK2);
            s_tft.setCursor(w/2 - 25, infoY + 36);
            s_tft.print("by Sneeze");
        }
    #else
        // Full version badge (large)
        s_tft.setTextSize(2);
        s_tft.setTextColor(COLOR_SPARK2);
        s_tft.setCursor(w/2 - 30, 158);
        s_tft.print("V");
        s_tft.print(getMajorVersion());

        // Full version
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setTextSize(1);
        s_tft.setCursor(w/2 + 10, 162);
        s_tft.print("(" AUTO_VERSION ")");

        // Tagline (only if space permits)
        if (h >= 240) {
            s_tft.setTextColor(COLOR_DIM);
            s_tft.setCursor(w/2 - 75, 185);
            s_tft.print("A tiny spark of mining power");

            // Credits
            s_tft.setTextColor(COLOR_SPARK2);
            s_tft.setCursor(w/2 - 30, 210);
            s_tft.print("by Sneeze");
        }
    #endif

    delay(2000);

    s_needsRedraw = true;
    Serial.println("[DISPLAY] Initialized");

    // Create persistent sprites sized to the current rotation/size
    recreate_sprites();
}

void display_set_backlight_off() {
    if (!s_backlightOff) {
        setBacklight(0);
        s_backlightOff = true;
    }
}

void display_set_backlight_on() {
    if (s_backlightOff) {
        setBacklight(s_brightness);
        s_backlightOff = false;
        s_needsRedraw = true;
    }
}

bool display_is_backlight_off() {
    return s_backlightOff;
}

void display_dump_bitmap(void) {
    if (s_backlightOff) {
        Serial.println("[DISPLAY] Dump bitmap skipped: backlight off");
        return;
    }

    uint16_t w = s_tft.width();
    uint16_t h = s_tft.height();
    Serial.printf("--- DISPLAY BITMAP DUMP START (%u x %u) ---\n", w, h);

    for (uint16_t y = 0; y < h; ++y) {
        size_t rowBytes = (size_t)w * 3;
        if (rowBytes > sizeof(s_rgbRow)) {
            Serial.println("[DISPLAY] Dump bitmap failed: row buffer too small");
            break;
        }
        s_tft.readRectRGB(0, y, w, 1, s_rgbRow);
        Serial.print("ROW "); Serial.print(y); Serial.print(": ");
        for (uint16_t x = 0; x < w; ++x) {
            uint8_t *pixel = &s_rgbRow[x * 3];
            char hex[8];
            snprintf(hex, sizeof(hex), "%02X%02X%02X", pixel[0], pixel[1], pixel[2]);
            Serial.print(hex);
            if (x < w - 1) Serial.print(' ');
        }
        Serial.println();
    }
    Serial.println("--- DISPLAY BITMAP DUMP END ---");
}

void display_update(const display_data_t *data) {
    if (!data) return;

    // Skip rendering when backlight is off (save CPU)
    if (s_backlightOff) return;

    // Check if anything changed
    bool changed = s_needsRedraw || display_data_changed(data, &s_lastData);
    if (!changed) return;

    // On small displays (e.g. M5Stick) clear the whole screen each update
    // to avoid leftover pixels when variable-length text shrinks.
    #if SMALL_DISPLAY
        s_tft.fillScreen(COLOR_BG);
    #else
        // Full screen clear only on screen change for larger displays
        if (s_needsRedraw) {
            s_tft.fillScreen(COLOR_BG);
        }
    #endif

    // Header: redraw on screen change or status change. On SMALL_DISPLAY
    // boards the whole screen is cleared every update (see above), so the
    // header must be redrawn every update too - otherwise the clear wipes
    // it and it's never repainted, leaving a permanent black band where
    // the header used to be after the very first frame.
    #if SMALL_DISPLAY
        drawHeader(data);
    #else
        if (s_needsRedraw) {
            drawHeader(data);
        }
    #endif

    // Content: redraw on any change
    // Each screen's panels use fillRoundRect to clear their areas
    switch (s_currentScreen) {
        case SCREEN_MINING:
            drawMiningScreen(data);
            break;
        case SCREEN_STATS:
            drawStatsScreen(data);
            break;
        case SCREEN_CLOCK:
            drawClockScreen(data);
            break;
        default:
            drawMiningScreen(data);
            break;
    }

    // Save last data
    memcpy(&s_lastData, data, sizeof(display_data_t));
    s_needsRedraw = false;
}

void display_set_brightness(uint8_t brightness) {
    s_brightness = brightness > 100 ? 100 : brightness;
    setBacklight(s_brightness);
}

void display_set_screen(uint8_t screen) {
    if (screen != s_currentScreen) {
        s_currentScreen = screen;
        s_needsRedraw = true;
    }
}

uint8_t display_get_screen() {
    return s_currentScreen;
}

void display_next_screen() {
    s_currentScreen = (s_currentScreen + 1) % 3;
    s_needsRedraw = true;
}

void display_redraw() {
    s_needsRedraw = true;
}

uint8_t display_flip_rotation() {
    // Cycle all 4 rotations: 0->1->2->3->0
    s_rotation = (s_rotation + 1) % 4;
    s_tft.setRotation(s_rotation);
    s_tft.fillScreen(COLOR_BG);
    s_needsRedraw = true;
    Serial.printf("[DISPLAY] Screen rotated, rotation=%d\n", s_rotation);
    // Recreate sprites for new rotation/size
    recreate_sprites();
    return s_rotation;
}

void display_set_rotation(uint8_t rotation) {
    if (rotation > 3) rotation = 0;
    s_rotation = rotation;
    s_tft.setRotation(s_rotation);
    s_tft.fillScreen(COLOR_BG);
    s_needsRedraw = true;
    Serial.printf("[DISPLAY] Rotation set to %d\n", s_rotation);
    // Recreate sprites for new rotation/size
    recreate_sprites();
}

void display_set_inverted(bool inverted) {
    // TFT panel default is inverted, so we flip the logic
    // inverted=true means user wants light mode (white bg)
    // inverted=false means user wants dark mode (black bg)
    s_tft.invertDisplay(!inverted);
    Serial.printf("[DISPLAY] Color inversion %s\n", inverted ? "enabled" : "disabled");
}

void display_show_reset_countdown(int seconds) {
    s_tft.fillScreen(COLOR_BG);
    int w = s_tft.width();
    int h = s_tft.height();
    bool tinyHeight = (h < 160);

    char secBuf[4];
    snprintf(secBuf, sizeof(secBuf), "%d", seconds);

    int numSize = tinyHeight ? 4 : 6;
    s_tft.setTextColor(COLOR_ERROR);  // Red
    s_tft.setTextSize(numSize);
    int numY = tinyHeight ? 4 : (h/2 - 40);
    s_tft.setCursor(max(0, (w - s_tft.textWidth(secBuf)) / 2), numY);
    s_tft.print(secBuf);

    s_tft.setTextSize(2);
    s_tft.setTextColor(COLOR_FG);  // White
    int titleY = tinyHeight ? (numY + numSize * 8 + 6) : (h/2 + 30);
    s_tft.setCursor(max(0, (w - s_tft.textWidth("Factory Reset")) / 2), titleY);
    s_tft.print("Factory Reset");

    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_DIM);  // Dim
    int subY = tinyHeight ? (titleY + 20) : (h/2 + 60);
    s_tft.setCursor(max(0, (w - s_tft.textWidth("Release button to cancel")) / 2), subY);
    s_tft.print("Release button to cancel");
}

void display_show_reset_complete() {
    s_tft.fillScreen(COLOR_BG);
    int w = s_tft.width();
    int h = s_tft.height();
    
    s_tft.setTextColor(COLOR_SUCCESS);  // Green
    s_tft.setTextSize(2);
    s_tft.setCursor(max(0, (w - s_tft.textWidth("Resetting...")) / 2), h/2 - 10);
    s_tft.print("Resetting...");
}

bool display_touched() {
    // Touch disabled - XPT2046 implementation had issues on CYD
    // See memory bank for details on failed implementation attempts
    return false;
}

void display_handle_touch() {
    // Touch disabled - use button to cycle screens instead
    display_next_screen();
}

void display_show_ap_config(const char *ssid, const char *password, const char *ip) {
    s_tft.fillScreen(COLOR_BG);
    int w = s_tft.width();
    int h = s_tft.height();

    if (SMALL_DISPLAY && h < 160) {
        // Very limited vertical room (e.g. M5StickC Plus2 in landscape).
        // The original layout below runs to ~170px and was cutting off
        // the IP/URL - the one piece of information needed to actually
        // reach the config portal - so this is a dedicated compact form.
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.setTextSize(1);
        s_tft.setCursor(MARGIN, 2);
        s_tft.print("WiFi Setup");

        int y = 14;
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN, y);
        s_tft.print("SSID:");
        y += 10;
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.setTextSize(2);
        s_tft.setCursor(MARGIN, y);
        s_tft.print(ssid);
        y += 20;

        s_tft.setTextSize(1);
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN, y);
        s_tft.print("Pass:");
        y += 10;
        s_tft.setTextColor(COLOR_FG);
        s_tft.setTextSize(2);
        s_tft.setCursor(MARGIN, y);
        s_tft.print(password);
        y += 20;

        s_tft.setTextSize(1);
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN, y);
        s_tft.print("Browser:");
        y += 10;
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.setTextSize(2);
        s_tft.setCursor(MARGIN, y);
        s_tft.print("http://");
        s_tft.print(ip);
        return;
    }

    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setTextSize(2);
    s_tft.setCursor(w/2 - 60, 20);
    s_tft.print("WiFi Setup");

    s_tft.setTextColor(COLOR_FG);
    s_tft.setTextSize(1);

    int y = 60;
    s_tft.setCursor(MARGIN, y);
    s_tft.print("Connect to WiFi:");
    y += LINE_HEIGHT;

    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setTextSize(2);
    s_tft.setCursor(MARGIN, y);
    s_tft.print(ssid);
    y += 30;

    s_tft.setTextColor(COLOR_FG);
    s_tft.setTextSize(1);
    s_tft.setCursor(MARGIN, y);
    s_tft.print("Password: ");
    s_tft.print(password);
    y += LINE_HEIGHT * 2;

    s_tft.setCursor(MARGIN, y);
    s_tft.print("Then open browser to:");
    y += LINE_HEIGHT;

    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setCursor(MARGIN, y);
    s_tft.print("http://");
    s_tft.print(ip);

    // TODO: Add QR code for WiFi connection
}

#endif // USE_DISPLAY
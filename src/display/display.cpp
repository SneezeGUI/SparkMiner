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
#if defined(LILYGO_T_DISPLAY_V1)
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

static uint8_t s_currentScreen = SCREEN_MINING;
static uint8_t s_brightness = 100;
static uint8_t s_rotation = 1;  // Current rotation (0-3)
static bool s_needsRedraw = true;
static display_data_t s_lastData;

// ============================================================
// Helper Functions
// ============================================================

uint16_t display_get_width() {
    return s_tft.width();
}

uint16_t display_get_height() {
    return s_tft.height();
}

bool display_is_portrait() {
    return (s_tft.width() < s_tft.height());
}

static void setBacklight(uint8_t percent) {
    uint32_t duty = (4095 * percent) / 100;
    ledcWrite(LEDC_CHANNEL, duty);
}

static String formatHashrate(double hashrate) {
    if (hashrate >= 1e9) {
        return String(hashrate / 1e9, 2) + " GH/s";
    } else if (hashrate >= 1e6) {
        return String(hashrate / 1e6, 2) + " MH/s";
    } else if (hashrate >= 1e3) {
        return String(hashrate / 1e3, 2) + " KH/s";
    } else {
        return String(hashrate, 1) + " H/s";
    }
}

static String formatNumber(uint64_t num) {
    if (num >= 1e12) {
        return String((double)num / 1e12, 2) + "T";
    } else if (num >= 1e9) {
        return String((double)num / 1e9, 2) + "G";
    } else if (num >= 1e6) {
        return String((double)num / 1e6, 2) + "M";
    } else if (num >= 1e3) {
        return String((double)num / 1e3, 2) + "K";
    } else {
        return String((uint32_t)num);
    }
}

static String formatUptime(uint32_t seconds) {
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;

    if (days > 0) {
        return String(days) + "d " + String(hours) + "h";
    } else if (hours > 0) {
        return String(hours) + "h " + String(mins) + "m";
    } else {
        return String(mins) + "m " + String(secs) + "s";
    }
}

static String formatDifficulty(double diff) {
    if (diff >= 1e15) {
        return String(diff / 1e15, 2) + "P";
    } else if (diff >= 1e12) {
        return String(diff / 1e12, 2) + "T";
    } else if (diff >= 1e9) {
        return String(diff / 1e9, 2) + "G";
    } else if (diff >= 1e6) {
        return String(diff / 1e6, 2) + "M";
    } else if (diff >= 1e3) {
        return String(diff / 1e3, 2) + "K";
    } else {
        return String(diff, 4);
    }
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

static void drawSparkLogo(int x, int y, int size, bool toSprite = false) {
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
                s_tft.fillRect(px, py, pw, ph, COLOR_SPARK1);
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
    
    // Dark header with accent line
    s_tft.fillRect(0, 0, w, HEADER_HEIGHT, COLOR_PANEL);
    s_tft.drawFastHLine(0, HEADER_HEIGHT - 1, w, COLOR_ACCENT);

    // Spark logo
    drawSparkLogo(8, 5, 30);

    // Title with spark gradient effect
    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setTextSize(2);
    s_tft.setCursor(42, 12);
    s_tft.print("Spark");
    s_tft.setTextColor(COLOR_SPARK1);
    s_tft.print("Miner");

    // Major version badge
    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(162, 16);
    s_tft.print("V");
    s_tft.print(getMajorVersion());

    if (!isPortrait) {
        // Status indicators (right side) - compact layout with color coding
        // Layout: Temp | WAN | POOL (right to left from right edge)
        s_tft.setTextSize(1);
        int rightEdge = w - MARGIN;

        // POOL status - rightmost (color coded by ping)
        int poolX = rightEdge - 40;
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(poolX, 6);
        s_tft.print("POOL");
        uint16_t pingColor = data->poolConnected ? getPingColor(data->avgLatency) : COLOR_ERROR;
        s_tft.fillCircle(poolX + 6, 26, 5, pingColor);
        if (data->poolConnected && data->avgLatency > 0) {
            s_tft.setTextColor(pingColor);
            s_tft.setCursor(poolX + 15, 22);
            s_tft.print(data->avgLatency);
        }

        // WAN status - middle (color coded by signal strength)
        int wanX = poolX - 45;
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(wanX, 6);
        s_tft.print("WAN");
        uint16_t wifiColor = data->wifiConnected ? getWifiColor(data->wifiRssi) : COLOR_ERROR;
        s_tft.fillCircle(wanX + 6, 26, 5, wifiColor);
        if (data->wifiConnected) {
            s_tft.setTextColor(wifiColor);
            s_tft.setCursor(wanX + 15, 22);
            s_tft.print(data->wifiRssi);
        }

        // Temperature - compact with color coding
        float temp = temperatureRead();
        uint16_t tempColor = getTempColor(temp);
        s_tft.setTextColor(tempColor);
        s_tft.setCursor(wanX - 28, 16);
        s_tft.print((int)temp);
        s_tft.print("C");
    }
}

static void drawBottomStatusBar(const display_data_t *data) {
    if (!display_is_portrait()) return;

    int w = display_get_width();
    int h = display_get_height();
    int barHeight = 32;  // Taller for labels + values
    int y = h - barHeight;

    // Draw panel background and border
    s_tft.fillRect(0, y, w, barHeight, COLOR_PANEL);
    s_tft.drawFastHLine(0, y, w, COLOR_SPARK2);

    s_tft.setTextSize(1);
    int centerY = y + (barHeight / 2) - 4;

    // Layout: evenly space 3 sections across width
    // Section 1 (left): Temperature (color coded)
    // Section 2 (center): WAN + indicator (color coded by signal)
    // Section 3 (right): POOL + indicator (color coded by ping)
    int sectionW = w / 3;

    // Temperature on left - color coded with label
    float temp = temperatureRead();
    uint16_t tempColor = getTempColor(temp);
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN, centerY - 6);
    s_tft.print("TEMP");
    s_tft.setTextColor(tempColor);
    s_tft.setCursor(MARGIN, centerY + 6);
    s_tft.print((int)temp);
    s_tft.print("C");

    // WAN Status - center section (color coded by signal strength)
    int wanX = sectionW;
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(wanX, centerY - 6);
    s_tft.print("WAN");
    uint16_t wifiColor = data->wifiConnected ? getWifiColor(data->wifiRssi) : COLOR_ERROR;
    s_tft.fillCircle(wanX + 4, centerY + 8, 4, wifiColor);
    if (data->wifiConnected) {
        s_tft.setTextColor(wifiColor);
        s_tft.setCursor(wanX + 12, centerY + 4);
        s_tft.print(data->wifiRssi);
    }

    // POOL Status - right section (color coded by ping)
    int poolX = sectionW * 2;
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(poolX, centerY - 6);
    s_tft.print("POOL");
    uint16_t pingColor = data->poolConnected ? getPingColor(data->avgLatency) : COLOR_ERROR;
    s_tft.fillCircle(poolX + 4, centerY + 8, 4, pingColor);
    if (data->poolConnected && data->avgLatency > 0) {
        s_tft.setTextColor(pingColor);
        s_tft.setCursor(poolX + 14, centerY + 4);
        s_tft.print(data->avgLatency);
    }
}

static void drawMiningScreen(const display_data_t *data) {
    int w = display_get_width();
    int y = HEADER_HEIGHT + 8;
    bool isPortrait = display_is_portrait();

    // Hashrate panel with glow effect
    s_tft.fillRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 38, 4, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 38, 4, COLOR_ACCENT);

    s_tft.setTextSize(2);
    s_tft.setCursor(MARGIN + 4, y + 6);
    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.print(formatHashrate(data->hashRate));

    // Shares on right side of hashrate panel
    // Portrait: shift toward center to fit 5+ digit share counts (e.g., "12345/12345")
    int sharesX = isPortrait ? (w - 75) : (w - 100);
    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(sharesX, y + 4);
    s_tft.print("Shares");
    s_tft.setTextColor(COLOR_FG);
    s_tft.setCursor(sharesX, y + 16);
    String shares = String(data->sharesAccepted) + "/" + String(data->sharesAccepted + data->sharesRejected);
    s_tft.print(shares);

    y += 44;

    s_tft.setTextSize(1);

    // Stats grid with panels
    // Landscape: 3 cols x 2 rows
    // Portrait:  2 cols x 3 rows
    int cols = isPortrait ? 2 : 3;
    int boxW = (w - (cols + 1) * MARGIN) / cols;

    struct { const char *label; String value; uint16_t color; } stats[] = {
        {"Best",     formatDifficulty(data->bestDifficulty), COLOR_SPARK1},
        {"Hashes",   formatNumber(data->totalHashes), COLOR_FG},
        {"Uptime",   formatUptime(data->uptimeSeconds), COLOR_FG},
        {"Jobs",     String(data->templates), COLOR_FG},
        {"32-bit",   String(data->blocks32), COLOR_SPARK2},
        {"Blocks",   String(data->blocksFound), COLOR_SUCCESS},
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
    s_tft.setTextColor(data->poolConnected ? COLOR_SUCCESS : COLOR_ERROR);
    
    // Truncate pool name if needed
    String poolName = data->poolName ? data->poolName : "Disconnected";
    if (isPortrait && poolName.length() > 12) poolName = poolName.substring(0, 10) + "..";
    s_tft.print(poolName);

    // Pool workers on right
    if (data->poolWorkersTotal > 0) {
        s_tft.setTextColor(COLOR_SPARK1);
        s_tft.setCursor(w - 90, y);
        s_tft.print(String(data->poolWorkersTotal) + " miners");
    }

    y += 14;

    // Pool difficulty
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Diff: ");
    s_tft.setTextColor(COLOR_FG);
    s_tft.print(formatDifficulty(data->poolDifficulty));

    // Your workers on address
    if (data->poolWorkersAddress > 0) {
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(w - 90, y);
        s_tft.print("You: ");
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.print(String(data->poolWorkersAddress));
    }

    y += 14;

    // IP address (full width since ping moved to status bar)
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("IP: ");
    s_tft.setTextColor(COLOR_FG);
    s_tft.print(data->ipAddress ? data->ipAddress : "---");

    drawBottomStatusBar(data);
}

static void drawStatsScreen(const display_data_t *data) {
    int w = display_get_width();
    int y = HEADER_HEIGHT + 8;

    // BTC Price panel
    s_tft.fillRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 38, 4, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 38, 4, COLOR_SPARK1);

    s_tft.setTextSize(2);
    s_tft.setCursor(MARGIN + 4, y + 6);
    s_tft.setTextColor(COLOR_SPARK1);
    if (data->btcPrice > 0) {
        s_tft.print("$");
        s_tft.print(String(data->btcPrice, 0));
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
    s_tft.print(data->blockHeight > 0 ? String(data->blockHeight) : "---");

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
    s_tft.print(data->halfHourFee > 0 ? String(data->halfHourFee) + " sat" : "---");

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
        s_tft.print(String(data->poolWorkersTotal) + " on pool");
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
    s_tft.print(String(data->sharesAccepted));

    drawBottomStatusBar(data);
}

static void drawClockScreen(const display_data_t *data) {
    int w = display_get_width();
    int h = display_get_height();

    // Get current time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setTextSize(2);
        s_tft.setCursor(w / 2 - 60, h / 2 - 10);
        s_tft.print("No Time");
        return;
    }

    // Time panel
    int y = HEADER_HEIGHT + 20;
    s_tft.fillRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 60, 6, COLOR_PANEL);
    s_tft.drawRoundRect(MARGIN - 4, y - 4, w - 2*MARGIN + 8, 60, 6, COLOR_ACCENT);

    // Large time display
    char timeStr[16];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setTextSize(4);
    // Center text approximation (4 chars * 6px * 4 scale = 96px for "HH:MM")
    s_tft.setCursor(w / 2 - 96, y + 10);
    s_tft.print(timeStr);

    y += 70;

    // Date
    char dateStr[32];
    strftime(dateStr, sizeof(dateStr), "%a, %b %d %Y", &timeinfo);

    s_tft.setTextColor(COLOR_FG);
    s_tft.setTextSize(2);
    s_tft.setCursor(w / 2 - 90, y);
    s_tft.print(dateStr);

    // Mining summary panel at bottom
    y = h - (display_is_portrait() ? 90 : 55);
    s_tft.fillRoundRect(MARGIN - 4, y, w - 2*MARGIN + 8, 50, 4, COLOR_PANEL);

    y += 8;
    s_tft.setTextSize(1);

    // Hashrate
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Hash: ");
    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.print(formatHashrate(data->hashRate));

    // BTC price on right
    if (data->btcPrice > 0) {
        s_tft.setTextColor(COLOR_SPARK1);
        s_tft.setCursor(w - 85, y);
        s_tft.print("$");
        s_tft.print(String(data->btcPrice, 0));
    }

    y += 16;

    // Shares
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN + 2, y);
    s_tft.print("Shares: ");
    s_tft.setTextColor(COLOR_FG);
    s_tft.print(String(data->sharesAccepted));

    // Block height on right
    if (data->blockHeight > 0) {
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(w - 85, y);
        s_tft.print("Blk ");
        s_tft.setTextColor(COLOR_FG);
        s_tft.print(String(data->blockHeight));
    }

    drawBottomStatusBar(data);
}

// ============================================================
// Public API
// ============================================================

void display_init(uint8_t rotation, uint8_t brightness) {
    // Enable 5V power for T-Display S3 (required before display init)
    #ifdef PIN_ENABLE5V
        pinMode(PIN_ENABLE5V, OUTPUT);
        digitalWrite(PIN_ENABLE5V, HIGH);
        delay(10);  // Allow power to stabilize
    #endif

    // Initialize TFT
    s_tft.init();
    s_rotation = rotation;
    s_tft.setRotation(rotation);
    s_tft.fillScreen(COLOR_BG);

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
        // Smaller logo and text for T-Display boards
        drawSparkLogo(w/2 - 25, 20, 50);

        s_tft.setTextSize(2);
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.setCursor(w/2 - 60, 80);
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
        // Compact version display for small screens
        s_tft.setTextSize(1);
        s_tft.setTextColor(COLOR_SPARK2);
        s_tft.setCursor(w/2 - 35, 105);
        s_tft.print("V");
        s_tft.print(getMajorVersion());
        s_tft.setTextColor(COLOR_DIM);
        s_tft.print(" (" AUTO_VERSION ")");

        // Shorter tagline
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(w/2 - 45, 125);
        s_tft.print("Solo BTC Mining");

        // Credits
        s_tft.setTextColor(COLOR_SPARK2);
        s_tft.setCursor(w/2 - 25, 145);
        s_tft.print("by Sneeze");
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
}

void display_update(const display_data_t *data) {
    if (!data) return;

    // Check if anything changed
    bool dataChanged = (data->totalHashes != s_lastData.totalHashes) ||
        (abs(data->hashRate - s_lastData.hashRate) > 100) ||
        (data->sharesAccepted != s_lastData.sharesAccepted);

    bool statusChanged = (data->poolConnected != s_lastData.poolConnected) ||
        (data->wifiConnected != s_lastData.wifiConnected);

    if (!s_needsRedraw && !dataChanged && !statusChanged) return;

    // Full screen clear only on screen change
    if (s_needsRedraw) {
        s_tft.fillScreen(COLOR_BG);
    }

    // Header: redraw on screen change or status change
    if (s_needsRedraw || statusChanged) {
        drawHeader(data);
    }

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
    return s_rotation;
}

void display_set_rotation(uint8_t rotation) {
    if (rotation > 3) rotation = 0;
    s_rotation = rotation;
    s_tft.setRotation(s_rotation);
    s_tft.fillScreen(COLOR_BG);
    s_needsRedraw = true;
    Serial.printf("[DISPLAY] Rotation set to %d\n", s_rotation);
}

void display_set_inverted(bool inverted) {
    s_tft.invertDisplay(inverted);
    Serial.printf("[DISPLAY] Color inversion %s\n", inverted ? "enabled" : "disabled");
}

void display_show_reset_countdown(int seconds) {
    s_tft.fillScreen(COLOR_BG);
    int w = s_tft.width();
    int h = s_tft.height();
    
    s_tft.setTextColor(COLOR_ERROR);  // Red
    s_tft.setTextSize(6);
    s_tft.setCursor(w/2 - 18, h/2 - 40);
    s_tft.print(seconds);
    
    s_tft.setTextSize(2);
    s_tft.setTextColor(COLOR_FG);  // White
    s_tft.setCursor(w/2 - 75, h/2 + 30);
    s_tft.print("Factory Reset");
    
    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_DIM);  // Dim
    s_tft.setCursor(w/2 - 65, h/2 + 60);
    s_tft.print("Release button to cancel");
}

void display_show_reset_complete() {
    s_tft.fillScreen(COLOR_BG);
    int w = s_tft.width();
    int h = s_tft.height();
    
    s_tft.setTextColor(COLOR_SUCCESS);  // Green
    s_tft.setTextSize(2);
    s_tft.setCursor(w/2 - 65, h/2 - 10);
    s_tft.print("Resetting...");
}

bool display_touched() {
    // TODO: Implement touch detection with XPT2046
    return false;
}

void display_handle_touch() {
    // TODO: Implement touch handling
    // For now, just cycle screens
    display_next_screen();
}

void display_show_ap_config(const char *ssid, const char *password, const char *ip) {
    s_tft.fillScreen(COLOR_BG);
    int w = s_tft.width();

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

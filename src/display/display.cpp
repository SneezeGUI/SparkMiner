/*
 * SparkMiner - Display Driver Implementation
 * TFT display for CYD (Cheap Yellow Display) boards
 *
 * Based on BitsyMiner by Justin Williams (GPL v3)
 */

#include <Arduino.h>
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

// PWM settings for backlight
#define LEDC_CHANNEL    0
#define LEDC_FREQ       5000
#define LEDC_RESOLUTION 12

// Colors (RGB565)
#define COLOR_BG        0x1082  // Dark blue-gray
#define COLOR_FG        0xFFFF  // White
#define COLOR_ACCENT    0xFD20  // Orange
#define COLOR_SUCCESS   0x07E0  // Green
#define COLOR_ERROR     0xF800  // Red
#define COLOR_DIM       0x7BEF  // Gray

// Layout
#define SCREEN_W        320
#define SCREEN_H        240
#define MARGIN          10
#define LINE_HEIGHT     22
#define HEADER_HEIGHT   40

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

// ============================================================
// Screen Drawing Functions
// ============================================================

static void drawHeader(const display_data_t *data) {
    // Background
    s_tft.fillRect(0, 0, SCREEN_W, HEADER_HEIGHT, COLOR_ACCENT);

    // Title
    s_tft.setTextColor(COLOR_BG);
    s_tft.setTextSize(2);
    s_tft.setCursor(MARGIN, 12);
    s_tft.print("SparkMiner");

    // Status icons (right side)
    int iconX = SCREEN_W - MARGIN - 10;

    // Pool status
    s_tft.fillCircle(iconX, 20, 6, data->poolConnected ? COLOR_SUCCESS : COLOR_ERROR);
    iconX -= 20;

    // WiFi status
    s_tft.fillCircle(iconX, 20, 6, data->wifiConnected ? COLOR_SUCCESS : COLOR_ERROR);
}

static void drawMiningScreen(const display_data_t *data) {
    int y = HEADER_HEIGHT + MARGIN;

    s_tft.setTextColor(COLOR_FG);
    s_tft.setTextSize(1);

    // Hashrate (large)
    s_tft.setTextSize(2);
    s_tft.setCursor(MARGIN, y);
    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.print(formatHashrate(data->hashRate));
    y += 30;

    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_FG);

    // Stats grid
    struct { const char *label; String value; } stats[] = {
        {"Hashes",     formatNumber(data->totalHashes)},
        {"Best Diff",  formatDifficulty(data->bestDifficulty)},
        {"Shares",     String(data->sharesAccepted) + "/" + String(data->sharesAccepted + data->sharesRejected)},
        {"Jobs",       String(data->templates)},
        {"32-bit",     String(data->blocks32)},
        {"Blocks",     String(data->blocksFound)},
        {"Uptime",     formatUptime(data->uptimeSeconds)},
        {"Pool Diff",  formatDifficulty(data->poolDifficulty)},
    };

    for (int i = 0; i < 8; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = MARGIN + col * (SCREEN_W / 2);
        int ly = y + row * LINE_HEIGHT;

        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(x, ly);
        s_tft.print(stats[i].label);
        s_tft.print(": ");

        s_tft.setTextColor(COLOR_FG);
        s_tft.print(stats[i].value);
    }

    y += 4 * LINE_HEIGHT + 10;

    // Pool info
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN, y);
    s_tft.print("Pool: ");
    s_tft.setTextColor(data->poolConnected ? COLOR_SUCCESS : COLOR_ERROR);
    s_tft.print(data->poolName ? data->poolName : "Disconnected");

    y += LINE_HEIGHT;

    // IP address
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN, y);
    s_tft.print("IP: ");
    s_tft.setTextColor(COLOR_FG);
    s_tft.print(data->ipAddress ? data->ipAddress : "Not connected");
}

static void drawStatsScreen(const display_data_t *data) {
    int y = HEADER_HEIGHT + MARGIN;

    s_tft.setTextColor(COLOR_FG);
    s_tft.setTextSize(1);

    // BTC Price (large)
    s_tft.setTextSize(2);
    s_tft.setCursor(MARGIN, y);
    s_tft.setTextColor(COLOR_ACCENT);
    if (data->btcPrice > 0) {
        s_tft.print("$");
        s_tft.print(String(data->btcPrice, 0));
    } else {
        s_tft.print("Loading...");
    }
    y += 30;

    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_FG);

    // Network stats
    struct { const char *label; String value; } stats[] = {
        {"Block Height", data->blockHeight > 0 ? String(data->blockHeight) : "---"},
        {"Network Hash", data->networkHashrate.length() > 0 ? data->networkHashrate : "---"},
        {"Difficulty",   data->networkDifficulty.length() > 0 ? data->networkDifficulty : "---"},
        {"Fee (30min)",  data->halfHourFee > 0 ? String(data->halfHourFee) + " sat/vB" : "---"},
    };

    for (int i = 0; i < 4; i++) {
        s_tft.setTextColor(COLOR_DIM);
        s_tft.setCursor(MARGIN, y);
        s_tft.print(stats[i].label);
        s_tft.print(": ");

        s_tft.setTextColor(COLOR_FG);
        s_tft.print(stats[i].value);
        y += LINE_HEIGHT;
    }

    y += 10;

    // Local mining summary
    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setCursor(MARGIN, y);
    s_tft.print("Your Mining:");
    y += LINE_HEIGHT;

    s_tft.setTextColor(COLOR_FG);
    s_tft.setCursor(MARGIN, y);
    s_tft.print("Hashrate: ");
    s_tft.print(formatHashrate(data->hashRate));
    y += LINE_HEIGHT;

    s_tft.setCursor(MARGIN, y);
    s_tft.print("Best: ");
    s_tft.print(formatDifficulty(data->bestDifficulty));
}

static void drawClockScreen(const display_data_t *data) {
    // Get current time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        s_tft.setTextColor(COLOR_FG);
        s_tft.setTextSize(2);
        s_tft.setCursor(SCREEN_W / 2 - 60, SCREEN_H / 2 - 10);
        s_tft.print("No Time");
        return;
    }

    // Large time display
    char timeStr[16];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setTextSize(4);
    s_tft.setCursor(SCREEN_W / 2 - 96, HEADER_HEIGHT + 30);
    s_tft.print(timeStr);

    // Date
    char dateStr[32];
    strftime(dateStr, sizeof(dateStr), "%a, %b %d %Y", &timeinfo);

    s_tft.setTextColor(COLOR_FG);
    s_tft.setTextSize(2);
    s_tft.setCursor(SCREEN_W / 2 - 90, HEADER_HEIGHT + 80);
    s_tft.print(dateStr);

    // Mining summary at bottom
    int y = SCREEN_H - 60;
    s_tft.setTextSize(1);
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN, y);
    s_tft.print("Hashrate: ");
    s_tft.setTextColor(COLOR_FG);
    s_tft.print(formatHashrate(data->hashRate));

    y += LINE_HEIGHT;
    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(MARGIN, y);
    s_tft.print("Shares: ");
    s_tft.setTextColor(COLOR_FG);
    s_tft.print(String(data->sharesAccepted));

    // BTC price on right
    if (data->btcPrice > 0) {
        s_tft.setTextColor(COLOR_ACCENT);
        s_tft.setCursor(SCREEN_W - 100, y - LINE_HEIGHT);
        s_tft.print("$");
        s_tft.print(String(data->btcPrice, 0));
    }
}

// ============================================================
// Public API
// ============================================================

void display_init(uint8_t rotation, uint8_t brightness) {
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

    // Show boot screen
    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setTextSize(3);
    s_tft.setCursor(40, 80);
    s_tft.print("SparkMiner");

    s_tft.setTextColor(COLOR_FG);
    s_tft.setTextSize(1);
    s_tft.setCursor(80, 130);
    s_tft.print("v" AUTO_VERSION);

    s_tft.setTextColor(COLOR_DIM);
    s_tft.setCursor(60, 160);
    s_tft.print("BitsyMiner + NerdMiner");

    delay(1500);

    s_needsRedraw = true;
    Serial.println("[DISPLAY] Initialized");
}

void display_update(const display_data_t *data) {
    if (!data) return;

    // Check if data changed significantly
    bool changed = s_needsRedraw ||
        (data->totalHashes != s_lastData.totalHashes) ||
        (abs(data->hashRate - s_lastData.hashRate) > 100) ||
        (data->sharesAccepted != s_lastData.sharesAccepted) ||
        (data->poolConnected != s_lastData.poolConnected);

    if (!changed) return;

    // Clear screen
    s_tft.fillScreen(COLOR_BG);

    // Draw header
    drawHeader(data);

    // Draw current screen
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
    // Flip 180 degrees: 0<->2, 1<->3
    s_rotation = (s_rotation + 2) % 4;
    s_tft.setRotation(s_rotation);
    s_tft.fillScreen(COLOR_BG);
    s_needsRedraw = true;
    Serial.printf("[DISPLAY] Screen flipped, rotation=%d\n", s_rotation);
    return s_rotation;
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

    s_tft.setTextColor(COLOR_ACCENT);
    s_tft.setTextSize(2);
    s_tft.setCursor(60, 20);
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

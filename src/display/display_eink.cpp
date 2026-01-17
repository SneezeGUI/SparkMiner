/*
 * SparkMiner - E-Ink Display Driver Implementation
 * U8g2-based driver for monochrome e-ink displays
 *
 * GPL v3 License
 */

#include <Arduino.h>
#include <board_config.h>
#include "display/display_eink.h"

#if USE_EINK_DISPLAY

#include <HT_lCMEN2R13EFC1.h>
#include <SPI.h>

// ============================================================
// Configuration
// ============================================================

// Default pins (can be overridden in board_config.h)
#ifndef EINK_MOSI_PIN
    #define EINK_MOSI_PIN 2
#endif

#ifndef EINK_MISO_PIN
    #define EINK_MISO_PIN -1
#endif

#ifndef EINK_CLK_PIN
    #define EINK_CLK_PIN 3
#endif

#ifndef EINK_CS_PIN
    #define EINK_CS_PIN 4
#endif 

#ifndef EINK_DC_PIN
    #define EINK_DC_PIN 5
#endif 

#ifndef EINK_RST_PIN
    #define EINK_RST_PIN 6
#endif 

#ifndef EINK_BUSY_PIN
    #define EINK_BUSY_PIN 7
#endif 

#ifndef EINK_EPD_EN_PIN
    #define EINK_EPD_EN_PIN 45
#endif

// Display dimensions (set in board_config.h)
#ifndef EINK_WIDHT
    #define EINK_WIDTH 250
#endif

#ifndef EINK_HEIGHT
    #define EINK_HEIGHT 122
#endif

#ifndef EINK_SMALL_FONT
    #define EINK_SMALL_FONT ArialMT_Plain_10
#endif

#ifndef EINK_MEDIUM_FONT
    #define EINK_MEDIUM_FONT ArialMT_Plain_16
#endif

#ifndef EINK_BIG_FONT
    #define EINK_BIG_FONT ArialMT_Plain_24
#endif
#ifndef EINK_DEFAULT_FONT
    #define EINK_DEFAULT_FONT EINK_MEDIUM_FONT
#endif


// Screen cycling (EINK has limited space, fewer screens)
#define EINK_SCREEN_MAIN    0
#define EINK_SCREEN_STATS   1
#define EINK_SCREEN_COUNT   2

// ============================================================
// GxEPD2 Display Object
// ============================================================

// Create display instance for 2.13" ICMEN2R13EFC1
// For now hardcoded for Heltec Wireless Paper v1.1 using Heltec Example Drivers
HT_ICMEN2R13EFC1 htDisplay(EINK_RST_PIN, EINK_DC_PIN, EINK_CS_PIN, EINK_BUSY_PIN, EINK_CLK_PIN, EINK_MOSI_PIN, EINK_MISO_PIN, 6000000); // rst,dc,cs,busy,sck,mosi,miso,frequency

// ============================================================
// State
// ============================================================

static uint8_t s_currentScreen = EINK_SCREEN_MAIN;
static uint8_t s_brightness = 255;
static uint8_t s_rotation = 0;
static uint8_t s_fontHeight = 0;
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

void printScreen() {
    htDisplay.update(BLACK_BUFFER);
    htDisplay.display();
    htDisplay.updateData(0x10); // load as old image
}

void clearScreen() {
    htDisplay.clear();
    htDisplay.update(BLACK_BUFFER);
    htDisplay.updateData(0x10); // load as old image
}

void clearFullScreen() {
    clearScreen();
    htDisplay.display();
}

static void drawMainScreen(const display_data_t *data) {
    clearScreen();

    uint16_t w;

    // Header line with status icons
    // WiFi status
    w = htDisplay.getStringWidth("W");
    if(data->wifiConnected) {
        htDisplay.drawString((w / 3), 0, "W");
    } else {
        htDisplay.drawString((w / 3), 0, "-");
    }

    // Pool status
    if (data->poolConnected) {
        htDisplay.drawString((w + (w / 3)), 0, "P");
    } else {
        htDisplay.drawString((w + (w / 3)), 0, "-");
    }
    // Uptime (right aligned)
    String uptime = formatUptimeCompact(data->uptimeSeconds);
    w = htDisplay.getStringWidth(uptime.c_str());
    htDisplay.drawString((EINK_WIDTH - w), 0, uptime.c_str());

    // Separator line
    htDisplay.drawHorizontalLine(0, (s_fontHeight + 2), EINK_WIDTH);

    // Large hashrate display
    htDisplay.setFont(ArialMT_Plain_24);
    String hashrate = formatHashrateCompact(data->hashRate) + "H/s";
    w = htDisplay.getStringWidth(hashrate.c_str());
    htDisplay.drawString(((EINK_WIDTH - w) / 2), ((EINK_HEIGHT - s_fontHeight) / 2), hashrate.c_str());

    // Revert to normal font
    htDisplay.setFont(EINK_DEFAULT_FONT);

    // Separator line
    htDisplay.drawHorizontalLine(0, (EINK_HEIGHT - s_fontHeight - 2), EINK_WIDTH);

    // Bottom stats row
    // Shares (left)
    String shares = "Shares: " + String(data->sharesAccepted);
    htDisplay.drawString(0, (EINK_HEIGHT - s_fontHeight), shares.c_str());

    // Best diff (right)
    String best = "Best Difficulty:" + formatDiffCompact(data->bestDifficulty);
    w = htDisplay.getStringWidth(best.c_str());
    htDisplay.drawString((EINK_WIDTH - w), (EINK_HEIGHT - s_fontHeight), best.c_str());
    
    printScreen();
}

static void drawStatsScreen(const display_data_t *data) {
    clearScreen();

    // Title
    htDisplay.drawString(0, 0, "STATS");
    htDisplay.drawHorizontalLine(0, (s_fontHeight + 2), EINK_WIDTH);

    // Pool info
    String poolLine = "Pool: " + String(data->poolName);
    poolLine += " (";
    poolLine += data->poolConnected ? "OK" : "---";
    poolLine += ")";
    htDisplay.drawString(0, ((s_fontHeight * 2) + 2), poolLine.c_str());

    // Difficulty
    String diffLine = "Pool Difficulty: " + formatDiffCompact(data->poolDifficulty);
    htDisplay.drawString(0, ((s_fontHeight * 3) + 4), diffLine.c_str());

    // Templates
    String templLine = "Templates: " + String(data->templates);
    htDisplay.drawString(0, ((s_fontHeight * 4) + 6), templLine.c_str());

    // WiFi signal
    String rssiLine = "RSSI: ";
    if (data->wifiConnected) {
        rssiLine += String(data->wifiRssi) + "dBm";
    } else {
        rssiLine += "---";
    }
    htDisplay.drawString(0, ((s_fontHeight * 5) + 8), rssiLine.c_str());

    printScreen();
}

// ============================================================
// Public API Implementation
// ============================================================

void eink_display_init(uint8_t rotation, uint8_t brightness) {
    Serial.printf("[EINK] Initializing E-Ink display\n");

    // Enable EPD Power
    pinMode(EINK_EPD_EN_PIN, OUTPUT);
    digitalWrite(EINK_EPD_EN_PIN, LOW);
    delay(100);

    // Initialize SPI with custom pins
    SPI.begin(EINK_CLK_PIN, EINK_MISO_PIN, EINK_MOSI_PIN, EINK_CS_PIN);

    // Initialize EPD
    htDisplay.init();

    // Set rotation
    s_rotation = rotation;
    if (rotation == 2) {
        htDisplay.screenRotate(ANGLE_180_DEGREE);
    } else {
        htDisplay.screenRotate(ANGLE_0_DEGREE);
    }

    // Default Font
    htDisplay.setFont(EINK_DEFAULT_FONT);
    s_fontHeight = EINK_DEFAULT_FONT[1];
    
    // Show boot screen
    eink_display_show_boot();

    htDisplay.setPartial();

    Serial.println("[EINK] Display initialized");
}

void eink_display_update(const display_data_t *data) {
    switch (s_currentScreen) {
        case EINK_SCREEN_MAIN:
            drawMainScreen(data);
            break;
        case EINK_SCREEN_STATS:
            drawStatsScreen(data);
            break;
        default:
            drawMainScreen(data);
            break;
    }
    s_needsRedraw = false;
}

void eink_display_set_brightness(uint8_t brightness) {
    // No brightness for E-Ink
}

void eink_display_next_screen() {
    s_currentScreen = (s_currentScreen + 1) % EINK_SCREEN_COUNT;
    s_needsRedraw = true;
    Serial.printf("[EINK] Screen: %d\n", s_currentScreen);
}

void eink_display_show_ap_config(const char *ssid, const char *password, const char *ip) {
    clearScreen();

    // Center WiFi AP stats
    htDisplay.drawString(10, 2, "WiFi Setup");
    htDisplay.drawHorizontalLine(0, (s_fontHeight + 4), EINK_WIDTH);
    String ssid_text = "SSID: " + String(ssid);
    htDisplay.drawString(0, (6 + (2 * s_fontHeight)), ssid_text.c_str());
    String pass_text = "Pass: " + String(password);
    htDisplay.drawString(0, (6 + (3 * s_fontHeight)), pass_text.c_str());
    String ip_text = "IP: " + String(ip);
    htDisplay.drawString(0, (6 + (4 * s_fontHeight)), ip_text.c_str());

    printScreen();
}

void eink_display_show_boot() {
    clearFullScreen();

    // Center "SparkMiner" title, version below
    const char *title = "SparkMiner";
    const char *version = AUTO_VERSION;
    uint16_t w; 

    // Center title horizontally 
    htDisplay.setFont(EINK_BIG_FONT);
    w = htDisplay.getStringWidth(title);
    int16_t x = (EINK_WIDTH - w) / 2; 
    int16_t y = ((EINK_HEIGHT - s_fontHeight) / 2);
    htDisplay.drawString(x, y, title);
    // Center version
    htDisplay.setFont(EINK_DEFAULT_FONT);
    w = htDisplay.getStringWidth(version);
    htDisplay.drawString(((EINK_WIDTH - w) / 2), (y + (2 * s_fontHeight)), version);

    printScreen();
}

void eink_display_show_reset_countdown(int seconds) {
    clearScreen();

    // Does not work, due to other screens being drawn anyway and the fact that eink has long refresh rate

    // prepare data
    String countdown = String(seconds);
    uint16_t w; 
    const char *text = "FACTORY RESET";
    w = htDisplay.getStringWidth(text);
    htDisplay.drawString(((EINK_WIDTH - w) / 2), s_fontHeight, text);
    w = htDisplay.getStringWidth(countdown.c_str());
    htDisplay.drawString(((EINK_WIDTH - w) / 2), ((EINK_HEIGHT - s_fontHeight) / 2), countdown.c_str());

    printScreen();
}

void eink_display_show_reset_complete() {
    clearScreen();

    // Does not work, due to other screens being drawn anyway and the fact that eink has long refresh rate

    const char *text = "RESET COMPLETE";
    uint16_t w = htDisplay.getStringWidth(text);
    htDisplay.drawString(((EINK_WIDTH - w) / 2), ((EINK_HEIGHT - s_fontHeight) / 2), text);
    
    printScreen();
}

void eink_display_redraw() {
    s_needsRedraw = true;
}

uint8_t eink_display_flip_rotation() {
    s_rotation = (s_rotation == 0) ? 2 : 0;

    if(s_rotation == 2) {
        htDisplay.screenRotate(ANGLE_180_DEGREE);
    } else {
        htDisplay.screenRotate(ANGLE_0_DEGREE);
    }

    s_needsRedraw = true;
    return s_rotation;
}

void eink_display_set_rotation(uint8_t rotation) {
    // implement only 0 and 2 (180 degree flip)
    s_rotation = (rotation >= 2) ? 2 : 0;

    if(s_rotation == 2) {
        htDisplay.screenRotate(ANGLE_180_DEGREE);
    } else {
        htDisplay.screenRotate(ANGLE_0_DEGREE);
    }

    s_needsRedraw = true;
}

void eink_display_set_inverted(bool inverted) {
    htDisplay.setInverted(inverted);
    s_inverted = inverted;

    s_needsRedraw = true;
}

uint16_t eink_display_get_width() {
    return EINK_WIDTH;
}

uint16_t eink_display_get_height() {
    return EINK_HEIGHT;
}

bool eink_display_is_portrait() {
    return false;  // EINKs are typically landscape
}

uint8_t eink_display_get_screen() {
    return s_currentScreen;
}

void eink_display_set_screen(uint8_t screen) {
    if (screen < EINK_SCREEN_COUNT) {
        s_currentScreen = screen;
        s_needsRedraw = true;
    }
}

// ============================================================
// Display Driver Interface
// ============================================================

static DisplayDriver s_einkDriver = {
    .init = eink_display_init,
    .update = eink_display_update,
    .set_brightness = eink_display_set_brightness,
    .next_screen = eink_display_next_screen,
    .show_ap_config = eink_display_show_ap_config,
    .show_boot = eink_display_show_boot,
    .show_reset_countdown = eink_display_show_reset_countdown,
    .show_reset_complete = eink_display_show_reset_complete,
    .redraw = eink_display_redraw,
    .flip_rotation = eink_display_flip_rotation,
    .set_inverted = eink_display_set_inverted,
    .get_width = eink_display_get_width,
    .get_height = eink_display_get_height,
    .is_portrait = eink_display_is_portrait,
    .get_screen = eink_display_get_screen,
    .set_screen = eink_display_set_screen,
    .name = "Heltec E_INK"
};

DisplayDriver* eink_get_driver() {
    return &s_einkDriver;
}

// ============================================================
// Standard display API wrappers (for main.cpp compatibility)
// ============================================================

void display_init(uint8_t rotation, uint8_t brightness) {
    eink_display_init(rotation, brightness);
}

void display_update(const display_data_t *data) {
    eink_display_update(data);
}

void display_set_brightness(uint8_t brightness) {
    // No brightness for E-INK
}

void display_set_screen(uint8_t screen) {
    eink_display_set_screen(screen);
}

uint8_t display_get_screen() {
    return eink_display_get_screen();
}

void display_next_screen() {
    eink_display_next_screen();
}

void display_redraw() {
    eink_display_redraw();
}

uint8_t display_flip_rotation() {
    return eink_display_flip_rotation();
}

void display_set_rotation(uint8_t rotation) {
    eink_display_set_rotation(rotation);
}

uint16_t display_get_width() {
    return eink_display_get_width();
}

uint16_t display_get_height() {
    return eink_display_get_height();
}

bool display_is_portrait() {
    return eink_display_is_portrait();
}

bool display_touched() {
    return false;  // E-INK has no touch
}

void display_handle_touch() {
    // No-op for E-INK
}

void display_show_ap_config(const char *ssid, const char *password, const char *ip) {
    eink_display_show_ap_config(ssid, password, ip);
}

void display_set_inverted(bool inverted) {
    eink_display_set_inverted(inverted);
}

void display_show_reset_countdown(int seconds) {
    eink_display_show_reset_countdown(seconds);
}

void display_show_reset_complete() {
    eink_display_show_reset_complete();
}

#endif // USE_EINK_DISPLAY


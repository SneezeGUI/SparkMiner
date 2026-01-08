/*
 * SparkMiner - Display Driver
 * TFT display support for CYD (Cheap Yellow Display) boards
 *
 * Based on BitsyMiner by Justin Williams (GPL v3)
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <board_config.h>

// Screen types
#define SCREEN_MINING       0
#define SCREEN_STATS        1
#define SCREEN_CLOCK        2
#define SCREEN_AP_CONFIG    3

// Display data structures
// NOTE: Using fixed char arrays instead of Arduino String to prevent heap fragmentation
typedef struct {
    // Mining stats
    uint64_t totalHashes;
    double hashRate;
    double bestDifficulty;
    uint32_t sharesAccepted;
    uint32_t sharesRejected;
    uint32_t templates;
    uint32_t blocks32;
    uint32_t blocksFound;
    uint32_t uptimeSeconds;
    uint32_t avgLatency;        // Average pool latency in ms

    // Pool info
    bool poolConnected;
    const char *poolName;
    double poolDifficulty;

    // Pool stats (from API) - fixed char arrays
    int poolWorkersTotal;       // Total workers on pool
    int poolWorkersAddress;     // Workers on your address
    char poolHashrate[24];      // Pool total hashrate
    char addressBestDiff[24];   // Your best difficulty on pool

    // Network info
    bool wifiConnected;
    int8_t wifiRssi;            // WiFi signal strength in dBm
    const char *ipAddress;

    // Live stats (from API) - fixed char arrays
    float btcPrice;
    uint32_t blockHeight;
    char networkHashrate[24];
    char networkDifficulty[24];
    int halfHourFee;

} display_data_t;

#if USE_DISPLAY

/**
 * Initialize display hardware
 * @param rotation Screen rotation (0-3)
 * @param brightness Initial brightness (0-100)
 */
void display_init(uint8_t rotation, uint8_t brightness);

/**
 * Update display with current data
 * Call periodically from monitor task
 */
void display_update(const display_data_t *data);

/**
 * Set display brightness
 * @param brightness 0-100
 */
void display_set_brightness(uint8_t brightness);

/**
 * Set current screen
 * @param screen SCREEN_* constant
 */
void display_set_screen(uint8_t screen);

/**
 * Get current screen
 */
uint8_t display_get_screen();

/**
 * Cycle to next screen
 */
void display_next_screen();

/**
 * Force full redraw
 */
void display_redraw();

/**
 * Cycle screen rotation (0 -> 1 -> 2 -> 3 -> 0)
 * @return New rotation value (0-3)
 */
uint8_t display_flip_rotation();

/**
 * Get current display width
 */
uint16_t display_get_width();

/**
 * Get current display height
 */
uint16_t display_get_height();

/**
 * Check if display is in portrait mode
 */
bool display_is_portrait();

/**
 * Check if screen was touched
 */
bool display_touched();

/**
 * Handle touch input
 */
void display_handle_touch();

/**
 * Show AP configuration screen with QR code
 */
void display_show_ap_config(const char *ssid, const char *password, const char *ip);

/**
 * Set display color inversion
 * @param inverted true to invert colors, false for normal
 */
void display_set_inverted(bool inverted);

/**
 * Show factory reset countdown
 * @param seconds Seconds remaining (3, 2, 1)
 */
void display_show_reset_countdown(int seconds);

/**
 * Show factory reset complete message
 */
void display_show_reset_complete();

#else

// Stub functions for headless builds
static inline void display_init(uint8_t rotation, uint8_t brightness) {}
static inline void display_update(const display_data_t *data) {}
static inline void display_set_brightness(uint8_t brightness) {}
static inline void display_set_screen(uint8_t screen) {}
static inline uint8_t display_get_screen() { return 0; }
static inline void display_next_screen() {}
static inline void display_redraw() {}
static inline uint8_t display_flip_rotation() { return 0; }
static inline bool display_touched() { return false; }
static inline void display_handle_touch() {}
static inline void display_show_ap_config(const char *ssid, const char *password, const char *ip) {}
static inline void display_set_inverted(bool inverted) {}
static inline void display_show_reset_countdown(int seconds) {}
static inline void display_show_reset_complete() {}

#endif // USE_DISPLAY

#endif // DISPLAY_H

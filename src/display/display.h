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

    // Pool info
    bool poolConnected;
    const char *poolName;
    double poolDifficulty;

    // Network info
    bool wifiConnected;
    const char *ipAddress;

    // Live stats (from API)
    float btcPrice;
    uint32_t blockHeight;
    String networkHashrate;
    String networkDifficulty;
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
 * Flip screen rotation 180 degrees
 * @return New rotation value (0-3)
 */
uint8_t display_flip_rotation();

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

#endif // USE_DISPLAY

#endif // DISPLAY_H

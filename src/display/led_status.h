/*
 * SparkMiner - LED Status Driver
 * Visual feedback via RGB LED for headless builds
 *
 * Provides mining status indication through LED color and pulse patterns:
 * - Yellow solid: Waiting for WiFi config
 * - Yellow pulse: AP mode active
 * - Blue slow pulse: Connecting to WiFi/pool
 * - Green fast pulse: Mining active
 * - White flash: Share accepted
 * - Red solid: Error state
 *
 * GPL v3 License
 */

#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <stdint.h>

// LED Status States
typedef enum {
    LED_STATUS_OFF,           // LED off
    LED_STATUS_BOOT,          // Yellow solid - booting
    LED_STATUS_AP_MODE,       // Yellow pulse - AP config mode
    LED_STATUS_CONNECTING,    // Blue slow pulse - connecting
    LED_STATUS_MINING,        // Green fast pulse - actively mining
    LED_STATUS_SHARE_FOUND,   // White flash - share accepted (temporary)
    LED_STATUS_BLOCK_FOUND,   // Rainbow - block found! (temporary)
    LED_STATUS_ERROR          // Red solid - error state
} led_status_t;

/**
 * Initialize LED status driver
 * Sets up FastLED and initial state
 */
void led_status_init();

/**
 * Set current LED status
 * @param status The status to display
 */
void led_status_set(led_status_t status);

/**
 * Get current LED status
 * @return Current status
 */
led_status_t led_status_get();

/**
 * Trigger share found flash
 * Temporarily shows white flash, returns to previous state
 */
void led_status_share_found();

/**
 * Trigger block found celebration
 * Rainbow animation, returns to previous state
 */
void led_status_block_found();

/**
 * Update LED animation
 * Call this from monitor task loop (~10-100ms interval)
 */
void led_status_update();

/**
 * Toggle LED on/off (for user button control)
 */
void led_status_toggle();

/**
 * Check if LED is enabled
 * @return true if LED feedback is on
 */
bool led_status_is_enabled();

#endif // LED_STATUS_H

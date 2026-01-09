/*
 * SparkMiner - OLED Display Driver Header
 * U8g2-based driver for small monochrome OLED displays
 *
 * Supported displays:
 * - 128x64 SSD1306 (I2C or SPI)
 * - 128x32 SSD1306 (I2C)
 * - 72x40 SSD1306 (0.42" displays)
 *
 * GPL v3 License
 */

#ifndef DISPLAY_OLED_H
#define DISPLAY_OLED_H

#include <Arduino.h>
#include <board_config.h>
#include "display/display_interface.h"
#include "display/display.h"

#if USE_OLED_DISPLAY

/**
 * Initialize OLED display hardware
 * @param rotation Screen rotation (0 or 2 for 180 flip)
 * @param brightness Contrast level (0-100)
 */
void oled_display_init(uint8_t rotation, uint8_t brightness);

/**
 * Update OLED display with current data
 * @param data Pointer to display data structure
 */
void oled_display_update(const display_data_t *data);

/**
 * Set OLED contrast/brightness
 * @param brightness 0-100
 */
void oled_display_set_brightness(uint8_t brightness);

/**
 * Cycle to next screen
 */
void oled_display_next_screen();

/**
 * Show AP configuration screen
 */
void oled_display_show_ap_config(const char *ssid, const char *password, const char *ip);

/**
 * Show boot screen
 */
void oled_display_show_boot();

/**
 * Show reset countdown
 */
void oled_display_show_reset_countdown(int seconds);

/**
 * Show reset complete
 */
void oled_display_show_reset_complete();

/**
 * Force redraw
 */
void oled_display_redraw();

/**
 * Flip rotation
 */
uint8_t oled_display_flip_rotation();

/**
 * Set inverted colors
 */
void oled_display_set_inverted(bool inverted);

/**
 * Get display dimensions
 */
uint16_t oled_display_get_width();
uint16_t oled_display_get_height();

/**
 * Check portrait mode
 */
bool oled_display_is_portrait();

/**
 * Screen management
 */
uint8_t oled_display_get_screen();
void oled_display_set_screen(uint8_t screen);

/**
 * Get the OLED display driver
 */
DisplayDriver* oled_get_driver();

#endif // USE_OLED_DISPLAY

#endif // DISPLAY_OLED_H

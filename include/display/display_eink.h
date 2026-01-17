/*
 * SparkMiner - E-Ink Display Driver Implementation
 * U8g2-based driver for monochrome e-ink displays
 *
 * GPL v3 License
 */

#ifndef DISPLAY_EINK_H
#define DISPLAY_EINK_H

#include <Arduino.h>
#include <board_config.h>
#include "display/display_interface.h"
#include "display/display.h"

#if USE_EINK_DISPLAY

/**
 * Initialize E-INK display hardware
 * @param rotation Screen rotation (0 or 2 for 180 flip)
 * @param brightness Not used for e-ink display
 */
void eink_display_init(uint8_t rotation, uint8_t brightness);

/**
 * Update E-INK display with current data
 * @param data Pointer to display data structure
 */
void eink_display_update(const display_data_t *data);

/**
 * Cycle to next screen
 */
void eink_display_next_screen();

/**
 * Show AP configuration screen
 */
void eink_display_show_ap_config(const char *ssid, const char *password, const char *ip);

/**
 * Show boot screen
 */
void eink_display_show_boot();

/**
 * Show reset countdown
 */
void eink_display_show_reset_countdown(int seconds);

/**
 * Show reset complete
 */
void eink_display_show_reset_complete();

/**
 * Force redraw
 */
void eink_display_redraw();

/**
 * Flip rotation
 */
uint8_t eink_display_flip_rotation();

/**
 * Set inverted colors
 */
void eink_display_set_inverted(bool inverted);

/**
 * Get display dimensions
 */
uint16_t eink_display_get_width();
uint16_t eink_display_get_height();

/**
 * Check portrait mode
 */
bool eink_display_is_portrait();

/**
 * Screen management
 */
uint8_t eink_display_get_screen();
void eink_display_set_screen(uint8_t screen);

/**
 * Get the E-INK display driver
 */
DisplayDriver* eink_get_driver();

#endif // USE_EINK_DISPLAY

#endif // DISPLAY_EINK_H


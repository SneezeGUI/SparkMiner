/*
 * SparkMiner - OLED Display Driver (header)
 * Declarations only - implementation lives in src/display/display_oled.cpp
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

#include <U8g2lib.h>
#include <Wire.h>

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

// Screen cycling
#define OLED_SCREEN_MAIN    0
#define OLED_SCREEN_STATS   1
#define OLED_SCREEN_COUNT   2

// Public driver API
void oled_display_init(uint8_t rotation, uint8_t brightness);
void oled_display_update(const display_data_t *data);
void oled_display_set_brightness(uint8_t brightness);
void oled_display_next_screen();
void oled_display_show_ap_config(const char *ssid, const char *password, const char *ip);
void oled_display_show_boot();
void oled_display_show_reset_countdown(int seconds);
void oled_display_show_reset_complete();
void oled_display_redraw();
uint8_t oled_display_flip_rotation();
void oled_display_set_rotation(uint8_t rotation);
void oled_display_set_inverted(bool inverted);
uint16_t oled_display_get_width();
uint16_t oled_display_get_height();
bool oled_display_is_portrait();
uint8_t oled_display_get_screen();
void oled_display_set_screen(uint8_t screen);

DisplayDriver* oled_get_driver();

#endif // USE_OLED_DISPLAY

#endif // DISPLAY_OLED_H
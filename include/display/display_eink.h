/*
 * SparkMiner - E-Ink Display Driver (header)
 * Declarations only - implementation lives in src/display/display_eink.cpp
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

#include <HT_lCMEN2R13EFC1.h>
#include <SPI.h>

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
#ifndef EINK_WIDTH
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

// Screen cycling
#define EINK_SCREEN_MAIN    0
#define EINK_SCREEN_STATS   1
#define EINK_SCREEN_COUNT   2

// Public driver API
void eink_display_init(uint8_t rotation, uint8_t brightness);
void eink_display_update(const display_data_t *data);
void eink_display_set_brightness(uint8_t brightness);
void eink_display_next_screen();
void eink_display_show_ap_config(const char *ssid, const char *password, const char *ip);
void eink_display_show_boot();
void eink_display_show_reset_countdown(int seconds);
void eink_display_show_reset_complete();
void eink_display_redraw();
uint8_t eink_display_flip_rotation();
void eink_display_set_rotation(uint8_t rotation);
void eink_display_set_inverted(bool inverted);
uint16_t eink_display_get_width();
uint16_t eink_display_get_height();
bool eink_display_is_portrait();
uint8_t eink_display_get_screen();
void eink_display_set_screen(uint8_t screen);

DisplayDriver* eink_get_driver();

#endif // USE_EINK_DISPLAY

#endif // DISPLAY_EINK_H
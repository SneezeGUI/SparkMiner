/*
 * SparkMiner - Display Manager
 * Routes display calls to the active driver (TFT, LED, or Serial)
 *
 * GPL v3 License
 */

#include <Arduino.h>
#include <board_config.h>
#include "display/display_interface.h"
#include "display.h"

// ============================================================
// Global Driver State
// ============================================================

static DisplayDriver *s_activeDriver = NULL;

// ============================================================
// Driver Registration
// ============================================================

void display_register_driver(DisplayDriver *driver) {
    if (driver) {
        s_activeDriver = driver;
        Serial.printf("[DISPLAY] Registered driver: %s\n", driver->name ? driver->name : "unknown");
    }
}

DisplayDriver* display_get_driver(void) {
    return s_activeDriver;
}

// ============================================================
// Public API Implementation
// Routes calls to the active driver
// ============================================================

#if USE_DISPLAY

// These are implemented in display.cpp (TFT driver)
// The TFT driver registers itself and the functions in display.cpp
// serve as the implementation. No routing needed for TFT-only builds.

#elif USE_OLED_DISPLAY

// These are implemented in display_oled.cpp (U8g2 OLED driver)
// OLED driver provides its own display_* function implementations.

#elif USE_LED_STATUS

// LED-only builds: implement stubs that work with LED status
// LED status is updated separately via monitor.cpp

void display_init(uint8_t rotation, uint8_t brightness) {
    Serial.println("[DISPLAY] LED-only mode (no TFT)");
}

void display_update(const display_data_t *data) {
    // LED status updated via led_status_update() in monitor task
}

void display_set_brightness(uint8_t brightness) {
    // Could adjust LED brightness here
}

void display_set_screen(uint8_t screen) {
    // No screens in LED mode
}

uint8_t display_get_screen() {
    return 0;
}

void display_next_screen() {
    // No screens in LED mode
}

void display_redraw() {
    // Nothing to redraw
}

uint8_t display_flip_rotation() {
    return 0;
}

void display_set_rotation(uint8_t rotation) {
    // No-op for LED-only display
}

uint16_t display_get_width() {
    return 0;
}

uint16_t display_get_height() {
    return 0;
}

bool display_is_portrait() {
    return false;
}

bool display_touched() {
    return false;
}

void display_handle_touch() {
    // No touch in LED mode
}

void display_show_ap_config(const char *ssid, const char *password, const char *ip) {
    Serial.printf("[AP] SSID: %s, Pass: %s, IP: %s\n", ssid, password, ip);
}

void display_set_inverted(bool inverted) {
    // No inversion in LED mode
}

void display_show_reset_countdown(int seconds) {
    Serial.printf("[RESET] %d seconds...\n", seconds);
}

void display_show_reset_complete() {
    Serial.println("[RESET] Complete");
}

#else

// Headless builds (no display, no LED): Serial output only

void display_init(uint8_t rotation, uint8_t brightness) {
    Serial.println("[DISPLAY] Headless mode (serial only)");
}

void display_update(const display_data_t *data) {
    // Periodic stats output handled by monitor task
}

void display_set_brightness(uint8_t brightness) {}
void display_set_screen(uint8_t screen) {}
uint8_t display_get_screen() { return 0; }
void display_next_screen() {}
void display_redraw() {}
uint8_t display_flip_rotation() { return 0; }
void display_set_rotation(uint8_t rotation) {}
uint16_t display_get_width() { return 0; }
uint16_t display_get_height() { return 0; }
bool display_is_portrait() { return false; }
bool display_touched() { return false; }
void display_handle_touch() {}

void display_show_ap_config(const char *ssid, const char *password, const char *ip) {
    Serial.println();
    Serial.println("=== WiFi Setup ===");
    Serial.printf("Connect to: %s\n", ssid);
    Serial.printf("Password:   %s\n", password);
    Serial.printf("Then open:  http://%s\n", ip);
    Serial.println("==================");
}

void display_set_inverted(bool inverted) {}

void display_show_reset_countdown(int seconds) {
    Serial.printf("[RESET] Factory reset in %d...\n", seconds);
}

void display_show_reset_complete() {
    Serial.println("[RESET] Factory reset complete, restarting...");
}

#endif

/*
 * SparkMiner - WiFi Manager
 * Captive portal for WiFi and pool configuration
 *
 * Uses WiFiManager library for initial setup
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

/**
 * Initialize WiFi manager
 * - Tries to connect to stored WiFi
 * - Falls back to AP mode with captive portal
 * - Saves credentials on successful connection
 */
void wifi_manager_init();

/**
 * Start WiFi manager in blocking mode
 * Won't return until WiFi is connected
 */
void wifi_manager_blocking();

/**
 * Start WiFi manager in non-blocking mode
 * Returns immediately, check wifi_manager_is_connected()
 */
void wifi_manager_start();

/**
 * Process WiFi manager events
 * Call periodically in non-blocking mode
 */
void wifi_manager_process();

/**
 * Check if WiFi is connected
 */
bool wifi_manager_is_connected();

/**
 * Force configuration portal
 * Opens AP even if credentials exist
 */
void wifi_manager_reset();

/**
 * Get current IP address as string
 */
const char* wifi_manager_get_ip();

#endif // WIFI_MANAGER_H

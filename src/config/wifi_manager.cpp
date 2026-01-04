/*
 * SparkMiner - WiFi Manager Implementation
 * Captive portal for WiFi and pool configuration
 */

#include <Arduino.h>
#include <WiFiManager.h>
#include <board_config.h>
#include "wifi_manager.h"
#include "nvs_config.h"
#include "../stratum/stratum.h"
#include "../display/display.h"

// WiFiManager instance
static WiFiManager s_wm;
static bool s_initialized = false;
static bool s_portalRunning = false;
static char s_ipAddress[16] = "0.0.0.0";

// Custom parameters for pool configuration
static WiFiManagerParameter* s_paramWallet = NULL;
static WiFiManagerParameter* s_paramWorkerName = NULL;
static WiFiManagerParameter* s_paramPoolUrl = NULL;
static WiFiManagerParameter* s_paramPoolPort = NULL;
static WiFiManagerParameter* s_paramPoolPassword = NULL;

// ============================================================
// Callbacks
// ============================================================

static void saveParamsCallback() {
    Serial.println("[WIFI] Saving configuration...");

    miner_config_t *config = nvs_config_get();

    // Get values from parameters
    if (s_paramWallet && s_paramWallet->getValue()) {
        strncpy(config->wallet, s_paramWallet->getValue(), MAX_WALLET_LEN);
    }
    if (s_paramWorkerName && s_paramWorkerName->getValue()) {
        strncpy(config->workerName, s_paramWorkerName->getValue(), 31);
    }
    if (s_paramPoolUrl && s_paramPoolUrl->getValue()) {
        strncpy(config->poolUrl, s_paramPoolUrl->getValue(), MAX_POOL_URL_LEN);
    }
    if (s_paramPoolPort && s_paramPoolPort->getValue()) {
        config->poolPort = atoi(s_paramPoolPort->getValue());
    }
    if (s_paramPoolPassword && s_paramPoolPassword->getValue()) {
        strncpy(config->poolPassword, s_paramPoolPassword->getValue(), MAX_PASSWORD_LEN);
    }

    // Save to NVS
    if (nvs_config_save(config)) {
        Serial.println("[WIFI] Configuration saved successfully");

        // Update stratum with new settings
        stratum_set_pool(config->poolUrl, config->poolPort,
                        config->wallet, config->poolPassword, config->workerName);
        stratum_reconnect();
    } else {
        Serial.println("[WIFI] Failed to save configuration");
    }
}

static void configModeCallback(WiFiManager *wm) {
    Serial.println("[WIFI] Entered config mode");
    Serial.printf("[WIFI] AP: %s\n", wm->getConfigPortalSSID().c_str());
    Serial.printf("[WIFI] IP: %s\n", WiFi.softAPIP().toString().c_str());
    s_portalRunning = true;

    // Show setup screen on display
    #if USE_DISPLAY
        display_show_ap_config(
            wm->getConfigPortalSSID().c_str(),
            AP_PASSWORD,
            WiFi.softAPIP().toString().c_str()
        );
    #endif
}

// ============================================================
// Public API
// ============================================================

void wifi_manager_init() {
    if (s_initialized) return;

    miner_config_t *config = nvs_config_get();

    // Create AP SSID from MAC address
    char apSSID[32];
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(apSSID, sizeof(apSSID), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);

    // Configure WiFiManager
    s_wm.setDebugOutput(false);
    s_wm.setMinimumSignalQuality(20);
    s_wm.setConnectTimeout(30);
    s_wm.setConfigPortalTimeout(180);  // 3 minutes
    s_wm.setSaveParamsCallback(saveParamsCallback);
    s_wm.setAPCallback(configModeCallback);

    // Create custom parameters
    // Note: WiFiManager copies the values, so we can use temporary strings
    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%d", config->poolPort);

    s_paramWallet = new WiFiManagerParameter("wallet", "BTC Wallet Address",
        config->wallet, MAX_WALLET_LEN);
    s_paramWorkerName = new WiFiManagerParameter("worker", "Worker Name (optional)",
        config->workerName, 31);
    s_paramPoolUrl = new WiFiManagerParameter("pool_url", "Pool URL",
        config->poolUrl, MAX_POOL_URL_LEN);
    s_paramPoolPort = new WiFiManagerParameter("pool_port", "Pool Port",
        portStr, 6);
    s_paramPoolPassword = new WiFiManagerParameter("pool_pass", "Pool Password",
        config->poolPassword, MAX_PASSWORD_LEN);

    // Add parameters to portal
    s_wm.addParameter(s_paramWallet);
    s_wm.addParameter(s_paramWorkerName);
    s_wm.addParameter(s_paramPoolUrl);
    s_wm.addParameter(s_paramPoolPort);
    s_wm.addParameter(s_paramPoolPassword);

    // Add custom HTML header
    s_wm.setCustomHeadElement("<style>body{background:#1a1a2e;color:#eee;}"
        "input{background:#16213e;color:#fff;border:1px solid #0f3460;}"
        "button{background:#e94560;}</style>");

    s_initialized = true;
    Serial.println("[WIFI] Manager initialized");
}

void wifi_manager_blocking() {
    if (!s_initialized) {
        wifi_manager_init();
    }

    miner_config_t *config = nvs_config_get();
    char apSSID[32];
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(apSSID, sizeof(apSSID), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);

    // If no config at all, disable timeout - stay in portal until configured
    bool hasAnyConfig = (config->ssid[0] != '\0') || (config->wallet[0] != '\0');
    if (!hasAnyConfig) {
        Serial.println("[WIFI] No configuration found - portal will stay open");
        s_wm.setConfigPortalTimeout(0);  // No timeout
    }

    Serial.println("[WIFI] Starting connection (blocking)...");
    Serial.printf("[WIFI] Connect to AP '%s' to configure\n", apSSID);

    // Try to connect, fall back to AP if needed
    bool connected = s_wm.autoConnect(apSSID, AP_PASSWORD);

    if (connected) {
        Serial.println("[WIFI] Connected!");
        Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
        strncpy(s_ipAddress, WiFi.localIP().toString().c_str(), sizeof(s_ipAddress));

        // Save WiFi credentials to our config
        strncpy(config->ssid, WiFi.SSID().c_str(), MAX_SSID_LENGTH);
        strncpy(config->wifiPassword, WiFi.psk().c_str(), MAX_PASSWORD_LEN);
        nvs_config_save(config);
    } else {
        Serial.println("[WIFI] Connection failed or portal timed out");

        // If still no valid config, restart to re-enter portal
        if (!nvs_config_is_valid()) {
            Serial.println("[WIFI] No valid config - restarting for setup...");
            delay(2000);
            ESP.restart();
        }
    }

    s_portalRunning = false;
}

void wifi_manager_start() {
    if (!s_initialized) {
        wifi_manager_init();
    }

    miner_config_t *config = nvs_config_get();

    // If we have stored credentials, try to connect directly
    if (config->ssid[0] != '\0') {
        Serial.printf("[WIFI] Connecting to %s...\n", config->ssid);
        WiFi.begin(config->ssid, config->wifiPassword);

        // Wait up to 10 seconds
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            strncpy(s_ipAddress, WiFi.localIP().toString().c_str(), sizeof(s_ipAddress));
            return;
        }
    }

    // Fall back to blocking mode with portal
    wifi_manager_blocking();
}

void wifi_manager_process() {
    if (s_portalRunning) {
        s_wm.process();
    }
}

bool wifi_manager_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

void wifi_manager_reset() {
    Serial.println("[WIFI] Resetting WiFi settings...");
    s_wm.resetSettings();

    // Also clear from our config
    miner_config_t *config = nvs_config_get();
    config->ssid[0] = '\0';
    config->wifiPassword[0] = '\0';
    nvs_config_save(config);

    // Restart to trigger portal
    ESP.restart();
}

const char* wifi_manager_get_ip() {
    return s_ipAddress;
}

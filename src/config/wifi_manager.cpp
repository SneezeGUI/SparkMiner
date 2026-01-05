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

// Custom parameters
static WiFiManagerParameter* s_paramWallet = NULL;
static WiFiManagerParameter* s_paramWorkerName = NULL;
static WiFiManagerParameter* s_paramPoolUrl = NULL;
static WiFiManagerParameter* s_paramPoolPort = NULL;
static WiFiManagerParameter* s_paramPoolPassword = NULL;

static WiFiManagerParameter* s_paramBackupPoolUrl = NULL;
static WiFiManagerParameter* s_paramBackupPoolPort = NULL;
static WiFiManagerParameter* s_paramBackupWallet = NULL;
static WiFiManagerParameter* s_paramBackupPoolPassword = NULL;

static WiFiManagerParameter* s_paramBrightness = NULL;
static WiFiManagerParameter* s_paramDifficulty = NULL;

// Custom HTML parameters buffers
static char s_rotationHtml[1024];
static char s_invertHtml[512];
static WiFiManagerParameter* s_paramRotation = NULL;
static WiFiManagerParameter* s_paramInvert = NULL;

// Buffers for numeric values
static char s_bufPoolPort[8];
static char s_bufBackupPort[8];
static char s_bufBrightness[8];
static char s_bufDifficulty[16];

// ============================================================ 
// Callbacks
// ============================================================ 

static void saveParamsCallback() {
    Serial.println("[WIFI] Saving configuration...");

    miner_config_t *config = nvs_config_get();

    // Primary Pool
    if (s_paramWallet && strlen(s_paramWallet->getValue()) > 0) {
        strncpy(config->wallet, s_paramWallet->getValue(), MAX_WALLET_LEN);
    }
    if (s_paramWorkerName) {
        strncpy(config->workerName, s_paramWorkerName->getValue(), 31);
    }
    if (s_paramPoolUrl && strlen(s_paramPoolUrl->getValue()) > 0) {
        strncpy(config->poolUrl, s_paramPoolUrl->getValue(), MAX_POOL_URL_LEN);
    }
    if (s_paramPoolPort) {
        config->poolPort = atoi(s_paramPoolPort->getValue());
    }
    if (s_paramPoolPassword) {
        strncpy(config->poolPassword, s_paramPoolPassword->getValue(), MAX_PASSWORD_LEN);
    }

    // Backup Pool
    if (s_paramBackupPoolUrl) {
        strncpy(config->backupPoolUrl, s_paramBackupPoolUrl->getValue(), MAX_POOL_URL_LEN);
    }
    if (s_paramBackupPoolPort) {
        config->backupPoolPort = atoi(s_paramBackupPoolPort->getValue());
    }
    if (s_paramBackupWallet) {
        strncpy(config->backupWallet, s_paramBackupWallet->getValue(), MAX_WALLET_LEN);
    }
    if (s_paramBackupPoolPassword) {
        strncpy(config->backupPoolPassword, s_paramBackupPoolPassword->getValue(), MAX_PASSWORD_LEN);
    }

    // Display & Miner
    if (s_paramBrightness) {
        int b = atoi(s_paramBrightness->getValue());
        if (b < 0) b = 0;
        if (b > 100) b = 100;
        config->brightness = b;
    }
    if (s_paramRotation) {
        config->rotation = atoi(s_paramRotation->getValue());
    }
    if (s_paramInvert) {
        config->invertColors = (atoi(s_paramInvert->getValue()) == 1);
    }
    if (s_paramDifficulty) {
        config->targetDifficulty = atof(s_paramDifficulty->getValue());
        if (config->targetDifficulty < 1e-9) config->targetDifficulty = 1e-9;
    }

    // Save to NVS
    if (nvs_config_save(config)) {
        Serial.println("[WIFI] Configuration saved successfully");

        // Update stratum
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

    // Create AP SSID
    char apSSID[32];
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(apSSID, sizeof(apSSID), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);

    // Prepare Buffers
    snprintf(s_bufPoolPort, sizeof(s_bufPoolPort), "%d", config->poolPort);
    snprintf(s_bufBackupPort, sizeof(s_bufBackupPort), "%d", config->backupPoolPort);
    snprintf(s_bufBrightness, sizeof(s_bufBrightness), "%d", config->brightness);
    snprintf(s_bufDifficulty, sizeof(s_bufDifficulty), "%.8f", config->targetDifficulty);

    // Create Parameters
    // We use 'new' to allocate persistent objects as WiFiManager stores pointers

    s_paramWallet = new WiFiManagerParameter("wallet", "BTC Wallet Address", config->wallet, MAX_WALLET_LEN);
    s_paramWorkerName = new WiFiManagerParameter("worker", "Worker Name", config->workerName, 31);
    s_paramPoolUrl = new WiFiManagerParameter("pool_url", "Primary Pool URL", config->poolUrl, MAX_POOL_URL_LEN);
    s_paramPoolPort = new WiFiManagerParameter("pool_port", "Primary Pool Port", s_bufPoolPort, 6);
    s_paramPoolPassword = new WiFiManagerParameter("pool_pass", "Primary Pool Password", config->poolPassword, MAX_PASSWORD_LEN);

    s_paramBackupPoolUrl = new WiFiManagerParameter("bk_pool_url", "Backup Pool URL", config->backupPoolUrl, MAX_POOL_URL_LEN);
    s_paramBackupPoolPort = new WiFiManagerParameter("bk_pool_port", "Backup Pool Port", s_bufBackupPort, 6);
    s_paramBackupWallet = new WiFiManagerParameter("bk_wallet", "Backup Wallet (optional)", config->backupWallet, MAX_WALLET_LEN);
    s_paramBackupPoolPassword = new WiFiManagerParameter("bk_pool_pass", "Backup Password", config->backupPoolPassword, MAX_PASSWORD_LEN);

    s_paramBrightness = new WiFiManagerParameter("bright", "Brightness (0-100)", s_bufBrightness, 4);
    s_paramDifficulty = new WiFiManagerParameter("diff", "Target Difficulty", s_bufDifficulty, 16);

    // Custom HTML for Rotation
    const char* rotLabels[] = {
        "Portrait (USB Right)",
        "Landscape (USB Bottom)",
        "Portrait (USB Left)",
        "Landscape (USB Top)"
    };
    
    strcpy(s_rotationHtml, "<br><label>Screen Rotation</label><select name='rotation'>");
    for(int i=0; i<4; i++) {
        strcat(s_rotationHtml, "<option value='");
        char val[2]; sprintf(val, "%d", i);
        strcat(s_rotationHtml, val);
        strcat(s_rotationHtml, "'");
        if(config->rotation == i) strcat(s_rotationHtml, " selected");
        strcat(s_rotationHtml, ">");
        strcat(s_rotationHtml, rotLabels[i]);
        strcat(s_rotationHtml, "</option>");
    }
    strcat(s_rotationHtml, "</select>");
    s_paramRotation = new WiFiManagerParameter("rotation", "Rotation", "0", 1, s_rotationHtml);

    // Custom HTML for Invert (using Select for reliability)
    strcpy(s_invertHtml, "<label>Invert Colors</label><select name='invert'>");
    strcat(s_invertHtml, "<option value='0'");
    if(!config->invertColors) strcat(s_invertHtml, " selected");
    strcat(s_invertHtml, ">Normal</option>");
    strcat(s_invertHtml, "<option value='1'");
    if(config->invertColors) strcat(s_invertHtml, " selected");
    strcat(s_invertHtml, ">Inverted</option></select>");
    s_paramInvert = new WiFiManagerParameter("invert", "Invert", "0", 1, s_invertHtml);

    // Configure WiFiManager
    s_wm.setDebugOutput(false);
    s_wm.setMinimumSignalQuality(20);
    s_wm.setConnectTimeout(30);
    s_wm.setConfigPortalTimeout(180);
    s_wm.setSaveParamsCallback(saveParamsCallback);
    s_wm.setAPCallback(configModeCallback);
    s_wm.setBreakAfterConfig(true); 

    // Dark Theme CSS
    const char* customCSS = "<style>"
        "body{background-color:#000000;color:#ffffff;font-family:Helvetica,Arial,sans-serif;}"
        "h1{color:#ff6800;}"
        "h3{color:#ffd700;}"
        "input,select{display:block;width:100%;box-sizing:border-box;margin:5px 0;padding:8px;border-radius:4px;background:#181818;color:#ffffff;border:1px solid #525252;}"
        "button{background:#ff6800;color:#000000;border:none;font-weight:bold;cursor:pointer;margin-top:15px;padding:10px;width:100%;border-radius:4px;}"
        "button:hover{background:#ff8c00;}"
        "div{padding:5px 0;}"
        "</style>";
    s_wm.setCustomHeadElement(customCSS);

    // Add Parameters
    s_wm.addParameter(s_paramWallet);
    s_wm.addParameter(s_paramWorkerName);
    
    // Separators aren't standard, just adding sequentially
    s_wm.addParameter(s_paramPoolUrl);
    s_wm.addParameter(s_paramPoolPort);
    s_wm.addParameter(s_paramPoolPassword);

    s_wm.addParameter(s_paramBackupPoolUrl);
    s_wm.addParameter(s_paramBackupPoolPort);
    s_wm.addParameter(s_paramBackupWallet);
    s_wm.addParameter(s_paramBackupPoolPassword);

    s_wm.addParameter(s_paramBrightness);
    s_wm.addParameter(s_paramDifficulty);
    s_wm.addParameter(s_paramRotation);
    s_wm.addParameter(s_paramInvert);

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
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
static char s_brightnessHtml[512];
static char s_difficultyHtml[768];
static WiFiManagerParameter* s_paramRotation = NULL;
static WiFiManagerParameter* s_paramTimezone = NULL;
static WiFiManagerParameter* s_paramInvert = NULL;

// Stats API parameters
static WiFiManagerParameter* s_paramStatsHeader = NULL;
static WiFiManagerParameter* s_paramStatsEnabled = NULL;
static WiFiManagerParameter* s_paramStatsApiUrl = NULL;
static WiFiManagerParameter* s_paramStatsProxy = NULL;
static WiFiManagerParameter* s_paramHttpsStats = NULL;
static char s_statsEnabledHtml[512];
static char s_httpsStatsHtml[512];

// Buffers for text inputs only
static char s_bufPoolPort[8];
static char s_bufBackupPort[8];

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

    // Display & Miner settings
    if (s_paramBrightness) {
        int b = atoi(s_paramBrightness->getValue());
        if (b < 0) b = 0;
        if (b > 100) b = 100;
        config->brightness = b;
    }
    if (s_paramDifficulty) {
        config->targetDifficulty = atof(s_paramDifficulty->getValue());
        if (config->targetDifficulty < 1e-9) config->targetDifficulty = 1e-9;
    }
    if (s_paramRotation) {
        config->rotation = atoi(s_paramRotation->getValue());
    }
    if (s_paramTimezone) {
        config->timezoneOffset = atoi(s_paramTimezone->getValue());
    }
    if (s_paramInvert) {
        config->invertColors = (atoi(s_paramInvert->getValue()) == 1);
    }

    // Stats API
    if (s_paramStatsEnabled) {
        config->statsEnabled = (atoi(s_paramStatsEnabled->getValue()) == 1);
    }
    if (s_paramStatsApiUrl) {
        strncpy(config->statsApiUrl, s_paramStatsApiUrl->getValue(), 127);
        config->statsApiUrl[127] = '\0';
    }
    if (s_paramStatsProxy) {
        strncpy(config->statsProxyUrl, s_paramStatsProxy->getValue(), 127);
        config->statsProxyUrl[127] = '\0';
    }
    if (s_paramHttpsStats) {
        config->enableHttpsStats = (atoi(s_paramHttpsStats->getValue()) == 1);
    }

    // Save to NVS
    if (nvs_config_save(config)) {
        Serial.println("[WIFI] Configuration saved successfully");

        // Apply display settings immediately
        #if USE_DISPLAY || USE_OLED_DISPLAY
            display_set_brightness(config->brightness);
            display_set_rotation(config->rotation);
            display_set_inverted(config->invertColors);
        #endif

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

    // Brightness dropdown
    const int brightValues[] = {10, 25, 50, 75, 100};
    strcpy(s_brightnessHtml, "<br><select name='bright'>");
    for(int i=0; i<5; i++) {
        char opt[64];
        sprintf(opt, "<option value='%d'%s>%d%%</option>",
            brightValues[i],
            (config->brightness == brightValues[i]) ? " selected" : "",
            brightValues[i]);
        strcat(s_brightnessHtml, opt);
    }
    strcat(s_brightnessHtml, "</select>");
    // Use config value as default for hidden input (not hardcoded)
    static char s_bufBrightness[8];
    snprintf(s_bufBrightness, sizeof(s_bufBrightness), "%d", config->brightness);
    s_paramBrightness = new WiFiManagerParameter("bright", "Brightness", s_bufBrightness, 4, s_brightnessHtml);
    // Difficulty dropdown (common solo mining values)
    const double diffValues[] = {0.00001, 0.0001, 0.001, 0.0014, 0.01, 0.1, 1.0};
    const char* diffLabels[] = {"0.00001 (Easiest)", "0.0001", "0.001", "0.0014 (Default)", "0.01", "0.1", "1.0 (Hardest)"};
    strcpy(s_difficultyHtml, "<br><select name='diff'>");
    for(int i=0; i<7; i++) {
        char opt[96];
        // Check if current difficulty matches (within small epsilon)
        bool selected = (config->targetDifficulty > diffValues[i] * 0.99 &&
                        config->targetDifficulty < diffValues[i] * 1.01);
        sprintf(opt, "<option value='%.6f'%s>%s</option>",
            diffValues[i],
            selected ? " selected" : "",
            diffLabels[i]);
        strcat(s_difficultyHtml, opt);
    }
    strcat(s_difficultyHtml, "</select>");
    // Use config value as default for hidden input
    static char s_bufDifficulty[16];
    snprintf(s_bufDifficulty, sizeof(s_bufDifficulty), "%.6f", config->targetDifficulty);
    s_paramDifficulty = new WiFiManagerParameter("diff", "Target Difficulty", s_bufDifficulty, 10, s_difficultyHtml);

    // Custom HTML for Rotation
    // TFT rotation: 0,2=Portrait, 1,3=Landscape (ILI9341 standard)
    // USB position based on CYD board physical layout
    const char* rotLabels[] = {
        "Portrait - USB Bottom",
        "Landscape - USB Right",
        "Portrait - USB Top",
        "Landscape - USB Left"
    };
    
    strcpy(s_rotationHtml, "<br><select name='rotation'>");
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
    // Use config value as default for hidden input
    static char s_bufRotation[4];
    snprintf(s_bufRotation, sizeof(s_bufRotation), "%d", config->rotation);
    s_paramRotation = new WiFiManagerParameter("rotation", "Screen Rotation", s_bufRotation, 2, s_rotationHtml);

    // Timezone dropdown
    // Common timezones from UTC-12 to UTC+14
    static char s_timezoneHtml[2048];
    strcpy(s_timezoneHtml, "<br><select name='tz'>");
    for (int i = -12; i <= 14; i++) {
        char opt[128];
        sprintf(opt, "<option value='%d'%s>UTC%s%d</option>",
            i,
            (config->timezoneOffset == i) ? " selected" : "",
            (i >= 0) ? "+" : "",
            i);
        strcat(s_timezoneHtml, opt);
    }
    strcat(s_timezoneHtml, "</select>");
    // Use config value as default for hidden input
    static char s_bufTimezone[8];
    snprintf(s_bufTimezone, sizeof(s_bufTimezone), "%d", config->timezoneOffset);
    s_paramTimezone = new WiFiManagerParameter("tz", "Timezone Offset", s_bufTimezone, 4, s_timezoneHtml);

    // Custom HTML for Color Theme
    // invert_colors=true means light mode (white bg), false means dark mode (black bg)
    strcpy(s_invertHtml, "<br><select name='invert'>");
    strcat(s_invertHtml, "<option value='0'");
    if(!config->invertColors) strcat(s_invertHtml, " selected");
    strcat(s_invertHtml, ">Dark (Default)</option>");
    strcat(s_invertHtml, "<option value='1'");
    if(config->invertColors) strcat(s_invertHtml, " selected");
    strcat(s_invertHtml, ">Light</option></select>");
    // Use config value as default for hidden input
    s_paramInvert = new WiFiManagerParameter("invert", "Color Theme", config->invertColors ? "1" : "0", 2, s_invertHtml);

    // Stats API Settings
    const char* statsHeader = "<br><h3>Stats API Settings</h3><div style='font-size:80%;color:#aaa'>Priority: Custom API &gt; Proxy &gt; Direct HTTPS</div>";
    s_paramStatsHeader = new WiFiManagerParameter(statsHeader);

    // Stats enabled dropdown
    strcpy(s_statsEnabledHtml, "<br><select name='stats_en'>");
    strcat(s_statsEnabledHtml, "<option value='1'");
    if(config->statsEnabled) strcat(s_statsEnabledHtml, " selected");
    strcat(s_statsEnabledHtml, ">Enabled</option>");
    strcat(s_statsEnabledHtml, "<option value='0'");
    if(!config->statsEnabled) strcat(s_statsEnabledHtml, " selected");
    strcat(s_statsEnabledHtml, ">Disabled</option></select>");
    s_paramStatsEnabled = new WiFiManagerParameter("stats_en", "Live Stats", config->statsEnabled ? "1" : "0", 2, s_statsEnabledHtml);

    // Custom stats API URL (for stratum proxy /stats endpoint)
    s_paramStatsApiUrl = new WiFiManagerParameter("stats_api", "Custom API URL (http://host:port/stats)", config->statsApiUrl, 128);

    // HTTP proxy URL
    s_paramStatsProxy = new WiFiManagerParameter("stats_proxy", "HTTP Proxy (http://host:port)", config->statsProxyUrl, 128);

    // Direct HTTPS dropdown
    strcpy(s_httpsStatsHtml, "<br><select name='https_stats'>");
    strcat(s_httpsStatsHtml, "<option value='0'");
    if(!config->enableHttpsStats) strcat(s_httpsStatsHtml, " selected");
    strcat(s_httpsStatsHtml, ">Direct HTTPS: Disabled (Stable)</option>");
    strcat(s_httpsStatsHtml, "<option value='1'");
    if(config->enableHttpsStats) strcat(s_httpsStatsHtml, " selected");
    strcat(s_httpsStatsHtml, ">Direct HTTPS: Enabled (Unstable)</option></select>");
    // Use config value as default for hidden input
    s_paramHttpsStats = new WiFiManagerParameter("https_stats", "Direct HTTPS", config->enableHttpsStats ? "1" : "0", 2, s_httpsStatsHtml);

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
        // WiFi network links - visible on dark background
        "a{color:#ff6800;text-decoration:none;display:block;padding:10px;margin:5px 0;background:#181818;border-radius:4px;border:1px solid #525252;}"
        "a:hover{background:#2a2a2a;color:#ff8c00;}"
        "a:visited{color:#ff6800;}"
        "input,select{display:block;width:100%;box-sizing:border-box;margin:5px 0;padding:8px;border-radius:4px;background:#181818;color:#ffffff;border:1px solid #525252;}"
        "button{background:#ff6800;color:#000000;border:none;font-weight:bold;cursor:pointer;margin-top:15px;padding:10px;width:100%;border-radius:4px;}"
        "button:hover{background:#ff8c00;}"
        "div{padding:5px 0;}"
        // Hide redundant text inputs for dropdown fields (dropdowns handle these)
        "input[name='bright'],input[name='diff'],input[name='rotation'],input[name='invert'],input[name='https_stats'],input[name='tz'],input[name='stats_en']{display:none;}"
        "</style>"
        "<script>"
        // Sync dropdown values to hidden inputs on load and change
        "function syncDropdowns(){"
        "console.log('[SYNC] Starting dropdown sync...');"
        "var dd=['bright','diff','rotation','tz','invert','https_stats','stats_en'];"
        "dd.forEach(function(n){"
        "var sel=document.querySelector('select[name=\"'+n+'\"]');"
        "var inp=document.querySelector('input[name=\"'+n+'\"]');"
        "if(sel&&inp){"
        "console.log('[SYNC] '+n+': select='+sel.value+', hidden='+inp.value+' -> syncing');"
        "inp.value=sel.value;"  // Sync on load
        "console.log('[SYNC] '+n+': after sync hidden='+inp.value);"
        "sel.addEventListener('change',function(){"
        "console.log('[CHANGE] '+n+': select changed to '+sel.value);"
        "inp.value=sel.value;"
        "console.log('[CHANGE] '+n+': hidden updated to '+inp.value);"
        "});"
        "}else{"
        "console.log('[SYNC] '+n+': MISSING - sel='+!!sel+', inp='+!!inp);"
        "}"
        "});"
        "console.log('[SYNC] Dropdown sync complete');"
        "}"
        // Log all values before form submit
        "function logFormValues(){"
        "var dd=['bright','diff','rotation','tz','invert','https_stats','stats_en'];"
        "console.log('[SUBMIT] Form values before submit:');"
        "dd.forEach(function(n){"
        "var sel=document.querySelector('select[name=\"'+n+'\"]');"
        "var inp=document.querySelector('input[name=\"'+n+'\"]');"
        "console.log('[SUBMIT] '+n+': select='+(sel?sel.value:'N/A')+', hidden='+(inp?inp.value:'N/A'));"
        "});"
        "}"
        // Attach submit handler
        "document.addEventListener('DOMContentLoaded',function(){"
        "var form=document.querySelector('form');"
        "if(form){form.addEventListener('submit',logFormValues);}"
        "});"
        // Run sync after DOM is fully loaded
        "if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',syncDropdowns);}else{syncDropdowns();}"
        // SSID auto-fill when clicking network link
        "function c(l){console.log('[SSID] Clicked: '+l.innerText);var s=document.querySelector('input[name=\"s\"]');if(s){s.value=l.innerText||l.textContent;console.log('[SSID] Set to: '+s.value);}}"
        "</script>";
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
    s_wm.addParameter(s_paramTimezone);
    s_wm.addParameter(s_paramInvert);

    s_wm.addParameter(s_paramStatsHeader);
    s_wm.addParameter(s_paramStatsEnabled);
    s_wm.addParameter(s_paramStatsApiUrl);
    s_wm.addParameter(s_paramStatsProxy);
    s_wm.addParameter(s_paramHttpsStats);

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
    // SD card stats backup does NOT count as configuration!
    bool hasAnyConfig = (config->ssid[0] != '\0') || (config->wallet[0] != '\0');
    if (!hasAnyConfig) {
        Serial.println("[WIFI] No valid configuration found - portal will stay open indefinitely");
        Serial.println("[WIFI] (SD card stats backup does not bypass WiFi setup)");
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
        config->ssid[MAX_SSID_LENGTH] = '\0';
        strncpy(config->wifiPassword, WiFi.psk().c_str(), MAX_PASSWORD_LEN);
        config->wifiPassword[MAX_PASSWORD_LEN] = '\0';

        // Configure NTP
        long gmtOffset = config->timezoneOffset * 3600L;
        configTime(gmtOffset, 0, "pool.ntp.org", "time.nist.gov");
        Serial.printf("[NTP] Time sync started (UTC%+d)\n", config->timezoneOffset);

        Serial.printf("[WIFI] Saving credentials for SSID: %s\n", config->ssid);
        if (nvs_config_save(config)) {
            Serial.println("[WIFI] Configuration saved to NVS successfully");
        } else {
            Serial.println("[WIFI] ERROR: Failed to save config to NVS!");
        }
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

    // Check if we have valid configuration (SD card with stats doesn't count as config)
    bool hasWifiConfig = (config->ssid[0] != '\0');
    bool hasPoolConfig = (config->wallet[0] != '\0');

    if (!hasWifiConfig && !hasPoolConfig) {
        // No config at all - go directly to captive portal
        Serial.println("[WIFI] No configuration found (SD stats don't count as config)");
        Serial.println("[WIFI] Entering WiFi configuration mode...");
        wifi_manager_blocking();
        return;
    }

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
            
            // Configure NTP
            long gmtOffset = config->timezoneOffset * 3600L;
            configTime(gmtOffset, 0, "pool.ntp.org", "time.nist.gov");
            Serial.printf("[NTP] Time sync started (UTC%+d)\n", config->timezoneOffset);
            
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
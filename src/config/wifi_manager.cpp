/*
 * SparkMiner - WiFi Manager Implementation
 * Captive portal for WiFi and pool configuration
 */

#include <Arduino.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#ifdef USE_SUPERDMZ
#include <SuperDMZ.h>
#endif
#include "../mining/miner.h"
#include "../stratum/stratum.h"
#include <board_config.h>
#include "wifi_manager.h"
#include "nvs_config.h"
#include "../stratum/stratum.h"
#include "../display/display.h"

// WiFiManager instance
static WiFiManager s_wm;
static WebServer s_apiServer(80);
#ifdef USE_SUPERDMZ
static SuperDMZ s_tunnel;
static bool s_tunnelStarted = false;
#endif
static bool s_apiServerStarted = false;
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
static char s_screenTimeoutHtml[512];
static char s_difficultyHtml[768];
static WiFiManagerParameter* s_paramRotation = NULL;
static WiFiManagerParameter* s_paramTimezone = NULL;
static WiFiManagerParameter* s_paramInvert = NULL;
static WiFiManagerParameter* s_paramScreenTimeout = NULL;

// Stats API parameters
static WiFiManagerParameter* s_paramStatsHeader = NULL;
static WiFiManagerParameter* s_paramStatsEnabled = NULL;
static WiFiManagerParameter* s_paramStatsApiUrl = NULL;
static WiFiManagerParameter* s_paramStatsProxy = NULL;
static WiFiManagerParameter* s_paramHttpsStats = NULL;
static WiFiManagerParameter* s_paramApiEnabled = NULL;
static char s_statsEnabledHtml[512];
static char s_httpsStatsHtml[512];
static char s_apiEnabledHtml[512];

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
    if (s_paramScreenTimeout) {
        config->screenTimeout = atoi(s_paramScreenTimeout->getValue());
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
    if (s_paramApiEnabled) {
        config->apiEnabled = (atoi(s_paramApiEnabled->getValue()) == 1);
    }

    // Save to NVS
    if (nvs_config_save(config)) {
        Serial.println("[WIFI] Configuration saved successfully");

        #if (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
            display_set_brightness(config->brightness);
            display_set_rotation(config->rotation);
            display_set_inverted(config->invertColors);
        #endif

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

    #if (USE_DISPLAY || USE_OLED_DISPLAY || USE_EINK_DISPLAY)
        display_show_ap_config(
            wm->getConfigPortalSSID().c_str(),
            AP_PASSWORD,
            WiFi.softAPIP().toString().c_str()
        );
    #endif
}

// ============================================================
// Stats API Handlers (AxeOS/ESP-Miner compatible)
// ============================================================

static void apiFormatHashrate(double h, char* buf, size_t len) {
    if (h >= 1e12)      snprintf(buf, len, "%.2f TH/s", h / 1e12);
    else if (h >= 1e9)  snprintf(buf, len, "%.2f GH/s", h / 1e9);
    else if (h >= 1e6)  snprintf(buf, len, "%.2f MH/s", h / 1e6);
    else if (h >= 1e3)  snprintf(buf, len, "%.2f kH/s", h / 1e3);
    else                snprintf(buf, len, "%.0f H/s", h);
}

static double apiCurrentHashrate() {
    mining_stats_t* stats = miner_get_stats();
    if (!stats || stats->startTime == 0) return 0.0;
    uint32_t elapsed = millis() - stats->startTime;
    if (elapsed == 0) return 0.0;
    return (double)stats->hashes / (elapsed / 1000.0);
}

static void handleSystemInfo() {
    miner_config_t* config = nvs_config_get();
    mining_stats_t* stats = miner_get_stats();
    mining_persistence_t* persist = nvs_stats_get();
    
    StaticJsonDocument<1024> doc;
    
    doc["deviceModel"] = BOARD_NAME;
    doc["firmwareVersion"] = AUTO_VERSION;
    doc["hostname"] = config->workerName;
    
    double hr = apiCurrentHashrate();
    char hrStr[24];
    apiFormatHashrate(hr, hrStr, sizeof(hrStr));
    doc["hashRate"] = hr;
    doc["hashRateString"] = hrStr;
    
    doc["sharesAccepted"] = persist->lifetimeAccepted;
    doc["sharesRejected"] = persist->lifetimeRejected;
    doc["bestDifficulty"] = persist->bestDifficultyEver;
    doc["blocksFound"] = persist->lifetimeBlocks;
    
    doc["uptimeSeconds"] = millis() / 1000;
    doc["sessionCount"] = persist->sessionCount;
    
    doc["pool"] = stratum_get_pool();
    doc["poolPort"] = config->poolPort;
    doc["wallet"] = config->wallet;
    doc["worker"] = config->workerName;
    
    String response;
    serializeJson(doc, response);
    s_apiServer.sendHeader("Access-Control-Allow-Origin", "*");
    s_apiServer.sendHeader("Connection", "close");
    s_apiServer.send(200, "application/json", response);
}

static void handleSystemStatistics() {
    mining_stats_t* stats = miner_get_stats();
    mining_persistence_t* persist = nvs_stats_get();
    
    StaticJsonDocument<1024> doc;
    
    if (stats) {
        doc["sessionHashes"] = stats->hashes;
        doc["sessionShares"] = stats->shares;
        doc["sessionAccepted"] = stats->accepted;
        doc["sessionRejected"] = stats->rejected;
        doc["sessionBlocks"] = stats->blocks;
        doc["sessionBestDifficulty"] = stats->bestDifficulty;
        doc["sessionTemplates"] = stats->templates;
        doc["avgLatencyMs"] = stats->avgLatency;
    }
    
    doc["lifetimeHashes"] = persist->lifetimeHashes;
    doc["lifetimeShares"] = persist->lifetimeShares;
    doc["lifetimeAccepted"] = persist->lifetimeAccepted;
    doc["lifetimeRejected"] = persist->lifetimeRejected;
    doc["lifetimeBlocks"] = persist->lifetimeBlocks;
    doc["lifetimeUptimeSeconds"] = persist->totalUptimeSeconds;
    doc["bestDifficultyEver"] = persist->bestDifficultyEver;
    
    String response;
    serializeJson(doc, response);
    s_apiServer.sendHeader("Access-Control-Allow-Origin", "*");
    s_apiServer.sendHeader("Connection", "close");
    s_apiServer.send(200, "application/json", response);
}

static void handleRoot() {
    double hr = apiCurrentHashrate();
    char hrStr[24];
    apiFormatHashrate(hr, hrStr, sizeof(hrStr));
    
    miner_config_t* config = nvs_config_get();
    mining_persistence_t* persist = nvs_stats_get();
    mining_stats_t* stats = miner_get_stats();
    
    uint32_t uptime = millis() / 1000;
    uint32_t hours = uptime / 3600;
    uint32_t minutes = (uptime % 3600) / 60;
    uint32_t seconds = uptime % 60;
    
    char uptimeStr[16];
    snprintf(uptimeStr, sizeof(uptimeStr), "%uh %um", hours, minutes);
    
    char hashesStr[16];
    uint64_t totalHashes = stats ? stats->hashes : 0;
    if (totalHashes >= 1000000ULL) {
        snprintf(hashesStr, sizeof(hashesStr), "%.2fM", totalHashes / 1000000.0);
    } else if (totalHashes >= 1000ULL) {
        snprintf(hashesStr, sizeof(hashesStr), "%.1fk", totalHashes / 1000.0);
    } else {
        snprintf(hashesStr, sizeof(hashesStr), "%llu", totalHashes);
    }
    
    char hrValue[16] = "0";
    char hrUnit[8] = "H/s";
    char* spacePtr = strchr(hrStr, ' ');
    if (spacePtr != NULL) {
        size_t valLen = spacePtr - hrStr;
        if (valLen < sizeof(hrValue)) {
            strncpy(hrValue, hrStr, valLen);
            hrValue[valLen] = '\0';
        }
        strncpy(hrUnit, spacePtr + 1, sizeof(hrUnit) - 1);
        hrUnit[sizeof(hrUnit) - 1] = '\0';
    }
    
    char bestStr[16];
    snprintf(bestStr, sizeof(bestStr), "%.4f", persist->bestDifficultyEver);
    
    uint32_t totalShares = persist->lifetimeAccepted + persist->lifetimeRejected;
    char sharesStr[16];
    snprintf(sharesStr, sizeof(sharesStr), "%u/%u", persist->lifetimeAccepted, totalShares);
    
    String ipStr = WiFi.localIP().toString();
    
    uint32_t ping = stats ? stats->avgLatency : 0;
    char pingStr[16];
    snprintf(pingStr, sizeof(pingStr), "%ums", ping);
    
    String html = F("<!DOCTYPE html><html><head>");
    html += F("<meta charset='UTF-8'>");
    html += F("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    html += F("<meta http-equiv='refresh' content='5'>");
    html += F("<title>SparkMiner v2</title>");
    html += F("<style>");
    html += F("*{box-sizing:border-box;margin:0;padding:0;}");
    html += F("body{font-family:'Courier New',Consolas,monospace;background:#000;color:#ddd;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px;}");
    html += F(".screen{max-width:640px;width:100%;background:#000;border:2px solid #8a3a00;border-radius:10px;padding:14px;box-shadow:0 0 20px rgba(138,58,0,0.25),inset 0 0 15px rgba(138,58,0,0.05);}");
    html += F(".header{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #8a3a00;padding-bottom:8px;margin-bottom:12px;}");
    html += F(".title{font-size:1.2em;color:#cc5500;font-weight:bold;letter-spacing:1px;display:flex;align-items:center;gap:6px;}");
    html += F(".header-leds{display:flex;gap:12px;font-size:0.65em;color:#666;text-transform:uppercase;letter-spacing:1px;}");
    html += F(".header-leds span{display:flex;align-items:center;gap:4px;}");
    html += F(".led{display:inline-block;width:7px;height:7px;border-radius:50%;background:#4a8a4a;box-shadow:0 0 6px rgba(74,138,74,0.6);}");
    html += F(".led.red{background:#8a3a3a;box-shadow:0 0 6px rgba(138,58,58,0.6);}");
    html += F(".hashrate-box{border:1px solid #8a3a00;border-radius:6px;padding:12px;margin-bottom:10px;text-align:center;background:rgba(138,58,0,0.04);}");
    html += F(".hashrate-value{font-size:2em;color:#cc5500;font-weight:bold;letter-spacing:1px;}");
    html += F(".hashrate-unit{font-size:0.5em;color:#8a3a00;margin-left:6px;}");
    html += F(".shares-inline{font-size:0.55em;color:#999;margin-left:12px;vertical-align:middle;}");
    html += F(".metrics{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-bottom:10px;}");
    html += F(".metric-box{border:1px solid #8a3a00;border-radius:5px;padding:7px 9px;background:rgba(138,58,0,0.03);}");
    html += F(".metric-label{font-size:0.65em;color:#cc5500;text-transform:uppercase;letter-spacing:1px;margin-bottom:3px;font-weight:bold;}");
    html += F(".metric-value{font-size:1.15em;color:#ddd;font-weight:bold;}");
    html += F(".metric-value.small{font-size:0.9em;}");
    html += F(".pool-box{border:1px solid #8a3a00;border-radius:5px;padding:9px 12px;margin-bottom:10px;background:rgba(138,58,0,0.03);}");
    html += F(".pool-row{display:flex;justify-content:space-between;font-size:0.8em;padding:2px 0;gap:10px;}");
    html += F(".pool-label{color:#cc5500;font-weight:bold;}");
    html += F(".pool-value{color:#ccc;text-align:right;word-break:break-all;}");
    html += F(".leds-bar{display:flex;justify-content:space-around;align-items:center;border-top:1px solid #8a3a00;padding-top:8px;margin-top:4px;}");
    html += F(".led-item{display:flex;align-items:center;gap:5px;font-size:0.7em;color:#8a3a00;font-weight:bold;text-transform:uppercase;letter-spacing:1px;}");
    html += F(".led-dot{width:8px;height:8px;border-radius:50%;background:#4a8a4a;box-shadow:0 0 6px rgba(74,138,74,0.6);}");
    html += F("@media (max-width:500px){.hashrate-value{font-size:1.5em;}.metric-value{font-size:0.9em;}.metrics{grid-template-columns:1fr 1fr;}.header-leds{font-size:0.55em;gap:8px;}.title{font-size:1em;}}");
    html += F("</style></head><body>");
    
    html += F("<div class='screen'>");
    
    html += F("<div class='header'>");
    html += F("<div class='title'><span>⚡</span><span>SparkMiner v2</span></div>");
    html += F("<div class='header-leds'>");
    html += F("<span><span class='led'></span> SDC</span>");
    html += F("<span><span class='led'></span> WiFi</span>");
    html += F("<span><span class='led'></span> POOL</span>");
    html += F("</div></div>");
    
    html += F("<div class='hashrate-box'>");
    html += F("<span class='hashrate-value'>");
    html += hrValue;
    html += F("</span><span class='hashrate-unit'>");
    html += hrUnit;
    html += F("</span><span class='shares-inline'>Shares: ");
    html += sharesStr;
    html += F("</span></div>");
    
    html += F("<div class='metrics'>");
    
    html += F("<div class='metric-box'><div class='metric-label'>Best</div><div class='metric-value'>");
    html += bestStr;
    html += F("</div></div>");
    
    html += F("<div class='metric-box'><div class='metric-label'>Hashes</div><div class='metric-value small'>");
    html += hashesStr;
    html += F("</div></div>");
    
    html += F("<div class='metric-box'><div class='metric-label'>Uptime</div><div class='metric-value small'>");
    html += uptimeStr;
    html += F("</div></div>");
    
    html += F("<div class='metric-box'><div class='metric-label'>Templates</div><div class='metric-value'>");
    html += String(stats ? stats->templates : 0);
    html += F("</div></div>");
    
    html += F("<div class='metric-box'><div class='metric-label'>Blocks</div><div class='metric-value'>");
    html += String(persist->lifetimeBlocks);
    html += F("</div></div>");
    
    html += F("<div class='metric-box'><div class='metric-label'>Sessions</div><div class='metric-value'>");
    html += String(persist->sessionCount);
    html += F("</div></div>");
    
    html += F("</div>");
    
    html += F("<div class='pool-box'>");
    html += F("<div class='pool-row'><span class='pool-label'>Pool:</span><span class='pool-value'>");
    html += String(stratum_get_pool());
    html += F("</span></div>");
    
    html += F("<div class='pool-row'><span class='pool-label'>Diff:</span><span class='pool-value'>");
    html += String(config->targetDifficulty, 4);
    html += F("</span></div>");
    
    html += F("<div class='pool-row'><span class='pool-label'>IP:</span><span class='pool-value'>");
    html += ipStr;
    html += F("</span></div>");
    
    html += F("<div class='pool-row'><span class='pool-label'>Ping:</span><span class='pool-value'>");
    html += pingStr;
    html += F("</span></div>");
    html += F("</div>");
    
    html += F("<div class='leds-bar'>");
    html += F("<div class='led-item'><div class='led-dot'></div><span>SDC</span></div>");
    html += F("<div class='led-item'><div class='led-dot'></div><span>WiFi</span></div>");
    html += F("<div class='led-item'><div class='led-dot'></div><span>Pool</span></div>");
    html += F("</div>");
    
    html += F("</div></body></html>");
    
    s_apiServer.sendHeader("Access-Control-Allow-Origin", "*");
    s_apiServer.sendHeader("Connection", "close");
    s_apiServer.send(200, "text/html", html);
}

static void handleFavicon() {
    s_apiServer.send(204, "text/plain", "");
}

static void bindServerCallback() {
    miner_config_t* config = nvs_config_get();
    if (!config->apiEnabled) {
        Serial.println("[API] Stats API disabled, skipping route registration");
        return;
    }
    if (s_apiServerStarted) {
        Serial.println("[API] Stats API already registered");
        return;
    }
    Serial.println("[API] Registering stats API routes");
    s_apiServer.on("/", HTTP_GET, handleRoot);
    s_apiServer.on("/favicon.ico", HTTP_GET, handleFavicon);
    s_apiServer.on("/api/system/info", HTTP_GET, handleSystemInfo);
    s_apiServer.on("/api/system/statistics", HTTP_GET, handleSystemStatistics);
    s_apiServer.onNotFound([]() {
        s_apiServer.send(404, "text/plain", "Not Found");
    });
    s_apiServer.begin();
    s_apiServerStarted = true;
    Serial.printf("[API] Server started on port 80 (http://%s/)\n",
                  WiFi.localIP().toString().c_str());

#ifdef USE_SUPERDMZ
    // Iniciar túnel SuperDMZ (solo una vez)
    if (!s_tunnelStarted) {
        // ⚠️ IMPORTANTE: Reemplaza "TU_TOKEN_SUPERDMZ_AQUI" por tu token real de 48 caracteres
       // Lo obtienes en: https://superdmz.com/login/?page=tunnels
        const char* SUPERDMZ_TOKEN = "TU_TOKEN_SUPERDMZ_AQUI";
        if (s_tunnel.begin(SUPERDMZ_TOKEN, 80)) {
            s_tunnelStarted = true;
            Serial.println("[TUNNEL] SuperDMZ iniciado correctamente");
        } else {
            Serial.println("[TUNNEL] ERROR: No se pudo iniciar SuperDMZ");
        }
    }
#endif
}

// ============================================================ 
// Public API
// ============================================================ 

void wifi_manager_init() {
    if (s_initialized) return;

    miner_config_t *config = nvs_config_get();

    char apSSID[32];
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(apSSID, sizeof(apSSID), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);

    snprintf(s_bufPoolPort, sizeof(s_bufPoolPort), "%d", config->poolPort);
    snprintf(s_bufBackupPort, sizeof(s_bufBackupPort), "%d", config->backupPoolPort);

    s_paramWallet = new WiFiManagerParameter("wallet", "BTC Wallet Address", config->wallet, MAX_WALLET_LEN);
    s_paramWorkerName = new WiFiManagerParameter("worker", "Worker Name", config->workerName, 31);
    s_paramPoolUrl = new WiFiManagerParameter("pool_url", "Primary Pool URL", config->poolUrl, MAX_POOL_URL_LEN);
    s_paramPoolPort = new WiFiManagerParameter("pool_port", "Primary Pool Port", s_bufPoolPort, 6);
    s_paramPoolPassword = new WiFiManagerParameter("pool_pass", "Primary Pool Password", config->poolPassword, MAX_PASSWORD_LEN);

    s_paramBackupPoolUrl = new WiFiManagerParameter("bk_pool_url", "Backup Pool URL", config->backupPoolUrl, MAX_POOL_URL_LEN);
    s_paramBackupPoolPort = new WiFiManagerParameter("bk_pool_port", "Backup Pool Port", s_bufBackupPort, 6);
    s_paramBackupWallet = new WiFiManagerParameter("bk_wallet", "Backup Wallet (optional)", config->backupWallet, MAX_WALLET_LEN);
    s_paramBackupPoolPassword = new WiFiManagerParameter("bk_pool_pass", "Backup Password", config->backupPoolPassword, MAX_PASSWORD_LEN);

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
    static char s_bufBrightness[8];
    snprintf(s_bufBrightness, sizeof(s_bufBrightness), "%d", config->brightness);
    s_paramBrightness = new WiFiManagerParameter("bright", "Brightness", s_bufBrightness, 4, s_brightnessHtml);

    const int timeoutValues[] = {0, 30, 60, 120, 300};
    const char* timeoutLabels[] = {"Never", "30 seconds", "1 minute", "2 minutes", "5 minutes"};
    strcpy(s_screenTimeoutHtml, "<br><select name='scrn_to'>");
    for(int i=0; i<5; i++) {
        char opt[80];
        sprintf(opt, "<option value='%d'%s>%s</option>",
            timeoutValues[i],
            (config->screenTimeout == timeoutValues[i]) ? " selected" : "",
            timeoutLabels[i]);
        strcat(s_screenTimeoutHtml, opt);
    }
    strcat(s_screenTimeoutHtml, "</select>");
    static char s_bufScreenTimeout[8];
    snprintf(s_bufScreenTimeout, sizeof(s_bufScreenTimeout), "%d", config->screenTimeout);
    s_paramScreenTimeout = new WiFiManagerParameter("scrn_to", "Screen Timeout", s_bufScreenTimeout, 4, s_screenTimeoutHtml);

    const double diffValues[] = {0.00001, 0.0001, 0.001, 0.0014, 0.01, 0.1, 1.0};
    const char* diffLabels[] = {"0.00001 (Easiest)", "0.0001", "0.001", "0.0014 (Default)", "0.01", "0.1", "1.0 (Hardest)"};
    strcpy(s_difficultyHtml, "<br><select name='diff'>");
    for(int i=0; i<7; i++) {
        char opt[96];
        bool selected = (config->targetDifficulty > diffValues[i] * 0.99 &&
                        config->targetDifficulty < diffValues[i] * 1.01);
        sprintf(opt, "<option value='%.6f'%s>%s</option>",
            diffValues[i],
            selected ? " selected" : "",
            diffLabels[i]);
        strcat(s_difficultyHtml, opt);
    }
    strcat(s_difficultyHtml, "</select>");
    static char s_bufDifficulty[16];
    snprintf(s_bufDifficulty, sizeof(s_bufDifficulty), "%.6f", config->targetDifficulty);
    s_paramDifficulty = new WiFiManagerParameter("diff", "Target Difficulty", s_bufDifficulty, 10, s_difficultyHtml);

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
    static char s_bufRotation[4];
    snprintf(s_bufRotation, sizeof(s_bufRotation), "%d", config->rotation);
    s_paramRotation = new WiFiManagerParameter("rotation", "Screen Rotation", s_bufRotation, 2, s_rotationHtml);

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
    static char s_bufTimezone[8];
    snprintf(s_bufTimezone, sizeof(s_bufTimezone), "%d", config->timezoneOffset);
    s_paramTimezone = new WiFiManagerParameter("tz", "Timezone Offset", s_bufTimezone, 4, s_timezoneHtml);

    strcpy(s_invertHtml, "<br><select name='invert'>");
    strcat(s_invertHtml, "<option value='0'");
    if(!config->invertColors) strcat(s_invertHtml, " selected");
    strcat(s_invertHtml, ">Dark (Default)</option>");
    strcat(s_invertHtml, "<option value='1'");
    if(config->invertColors) strcat(s_invertHtml, " selected");
    strcat(s_invertHtml, ">Light</option></select>");
    s_paramInvert = new WiFiManagerParameter("invert", "Color Theme", config->invertColors ? "1" : "0", 2, s_invertHtml);

    const char* statsHeader = "<br><h3>Stats API Settings</h3><div style='font-size:80%;color:#aaa'>Priority: Custom API &gt; Proxy &gt; Direct HTTPS</div>";
    s_paramStatsHeader = new WiFiManagerParameter(statsHeader);

    strcpy(s_statsEnabledHtml, "<br><select name='stats_en'>");
    strcat(s_statsEnabledHtml, "<option value='1'");
    if(config->statsEnabled) strcat(s_statsEnabledHtml, " selected");
    strcat(s_statsEnabledHtml, ">Enabled</option>");
    strcat(s_statsEnabledHtml, "<option value='0'");
    if(!config->statsEnabled) strcat(s_statsEnabledHtml, " selected");
    strcat(s_statsEnabledHtml, ">Disabled</option></select>");
    s_paramStatsEnabled = new WiFiManagerParameter("stats_en", "Live Stats", config->statsEnabled ? "1" : "0", 2, s_statsEnabledHtml);

    s_paramStatsApiUrl = new WiFiManagerParameter("stats_api", "Custom API URL (http://host:port/stats)", config->statsApiUrl, 128);
    s_paramStatsProxy = new WiFiManagerParameter("stats_proxy", "HTTP Proxy (http://host:port)", config->statsProxyUrl, 128);

    strcpy(s_httpsStatsHtml, "<br><select name='https_stats'>");
    strcat(s_httpsStatsHtml, "<option value='0'");
    if(!config->enableHttpsStats) strcat(s_httpsStatsHtml, " selected");
    strcat(s_httpsStatsHtml, ">Direct HTTPS: Disabled (Stable)</option>");
    strcat(s_httpsStatsHtml, "<option value='1'");
    if(config->enableHttpsStats) strcat(s_httpsStatsHtml, " selected");
    strcat(s_httpsStatsHtml, ">Direct HTTPS: Enabled (Unstable)</option></select>");
    s_paramHttpsStats = new WiFiManagerParameter("https_stats", "Direct HTTPS", config->enableHttpsStats ? "1" : "0", 2, s_httpsStatsHtml);

    strcpy(s_apiEnabledHtml, "<br><select name='api_en'>");
    strcat(s_apiEnabledHtml, "<option value='1'");
    if(config->apiEnabled) strcat(s_apiEnabledHtml, " selected");
    strcat(s_apiEnabledHtml, ">Enabled</option>");
    strcat(s_apiEnabledHtml, "<option value='0'");
    if(!config->apiEnabled) strcat(s_apiEnabledHtml, " selected");
    strcat(s_apiEnabledHtml, ">Disabled</option></select>");
    s_paramApiEnabled = new WiFiManagerParameter("api_en", "Stats API Server", 
                                                  config->apiEnabled ? "1" : "0", 2, 
                                                  s_apiEnabledHtml);

    s_wm.setDebugOutput(false);
    s_wm.setMinimumSignalQuality(20);
    s_wm.setConnectTimeout(30);
    s_wm.setConfigPortalTimeout(180);
    s_wm.setSaveParamsCallback(saveParamsCallback);
    s_wm.setAPCallback(configModeCallback);
    s_wm.setBreakAfterConfig(true);

    const char* customCSS = "<style>"
        "body{background-color:#000000;color:#ffffff;font-family:Helvetica,Arial,sans-serif;}"
        "h1{color:#ff6800;}"
        "h3{color:#ffd700;}"
        "a{color:#ff6800;text-decoration:none;display:block;padding:10px;margin:5px 0;background:#181818;border-radius:4px;border:1px solid #525252;}"
        "a:hover{background:#2a2a2a;color:#ff8c00;}"
        "a:visited{color:#ff6800;}"
        "input,select{display:block;width:100%;box-sizing:border-box;margin:5px 0;padding:8px;border-radius:4px;background:#181818;color:#ffffff;border:1px solid #525252;}"
        "button{background:#ff6800;color:#000000;border:none;font-weight:bold;cursor:pointer;margin-top:15px;padding:10px;width:100%;border-radius:4px;}"
        "button:hover{background:#ff8c00;}"
        "div{padding:5px 0;}"
        "input[name='bright'],input[name='scrn_to'],input[name='diff'],input[name='rotation'],input[name='invert'],input[name='https_stats'],input[name='tz'],input[name='stats_en'],input[name='api_en']{display:none;}"
        "</style>"
        "<script>"
        "function syncDropdowns(){"
        "var dd=['bright','scrn_to','diff','rotation','tz','invert','https_stats','stats_en','api_en'];"
        "dd.forEach(function(n){"
        "var sel=document.querySelector('select[name=\"'+n+'\"]');"
        "var inp=document.querySelector('input[name=\"'+n+'\"]');"
        "if(sel&&inp){"
        "inp.value=sel.value;"
        "sel.addEventListener('change',function(){"
        "inp.value=sel.value;"
        "});"
        "}"
        "});"
        "}"
        "if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',syncDropdowns);}else{syncDropdowns();}"
        "</script>";
    s_wm.setCustomHeadElement(customCSS);

    s_wm.addParameter(s_paramWallet);
    s_wm.addParameter(s_paramWorkerName);
    s_wm.addParameter(s_paramPoolUrl);
    s_wm.addParameter(s_paramPoolPort);
    s_wm.addParameter(s_paramPoolPassword);
    s_wm.addParameter(s_paramBackupPoolUrl);
    s_wm.addParameter(s_paramBackupPoolPort);
    s_wm.addParameter(s_paramBackupWallet);
    s_wm.addParameter(s_paramBackupPoolPassword);
    s_wm.addParameter(s_paramBrightness);
    s_wm.addParameter(s_paramScreenTimeout);
    s_wm.addParameter(s_paramDifficulty);
    s_wm.addParameter(s_paramRotation);
    s_wm.addParameter(s_paramTimezone);
    s_wm.addParameter(s_paramInvert);
    s_wm.addParameter(s_paramStatsHeader);
    s_wm.addParameter(s_paramStatsEnabled);
    s_wm.addParameter(s_paramStatsApiUrl);
    s_wm.addParameter(s_paramStatsProxy);
    s_wm.addParameter(s_paramHttpsStats);
    s_wm.addParameter(s_paramApiEnabled);

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

    bool hasAnyConfig = (config->ssid[0] != '\0') || (config->wallet[0] != '\0');
    if (!hasAnyConfig) {
        Serial.println("[WIFI] No valid configuration found - portal will stay open indefinitely");
        Serial.println("[WIFI] (SD card stats backup does not bypass WiFi setup)");
        s_wm.setConfigPortalTimeout(0);
    }

    Serial.println("[WIFI] Starting connection (blocking)...");
    Serial.printf("[WIFI] Connect to AP '%s' to configure\n", apSSID);

    s_wm.setWebServerCallback(bindServerCallback);

    bool connected = s_wm.autoConnect(apSSID, AP_PASSWORD);

    if (connected) {
        Serial.println("[WIFI] Connected!");
        Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
        strncpy(s_ipAddress, WiFi.localIP().toString().c_str(), sizeof(s_ipAddress));

        WiFi.setSleep(false);

        strncpy(config->ssid, WiFi.SSID().c_str(), MAX_SSID_LENGTH);
        config->ssid[MAX_SSID_LENGTH] = '\0';
        strncpy(config->wifiPassword, WiFi.psk().c_str(), MAX_PASSWORD_LEN);
        config->wifiPassword[MAX_PASSWORD_LEN] = '\0';

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

    bool hasWifiConfig = (config->ssid[0] != '\0');
    bool hasPoolConfig = (config->wallet[0] != '\0');

    if (!hasWifiConfig && !hasPoolConfig) {
        Serial.println("[WIFI] No configuration found (SD stats don't count as config)");
        Serial.println("[WIFI] Entering WiFi configuration mode...");
        wifi_manager_blocking();
        return;
    }

    if (config->ssid[0] != '\0') {
        Serial.printf("[WIFI] Connecting to %s...\n", config->ssid);

        s_wm.setWebServerCallback(bindServerCallback);

        WiFi.begin(config->ssid, config->wifiPassword);

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

            WiFi.setSleep(false);

            long gmtOffset = config->timezoneOffset * 3600L;
            configTime(gmtOffset, 0, "pool.ntp.org", "time.nist.gov");
            Serial.printf("[NTP] Time sync started (UTC%+d)\n", config->timezoneOffset);
            bindServerCallback();

            return;
        }
    }

    wifi_manager_blocking();
}

void wifi_manager_process() {
    if (s_portalRunning) {
        s_wm.process();
    }
    if (s_apiServerStarted) {
        s_apiServer.handleClient();
    }
#ifdef USE_SUPERDMZ
    if (s_tunnelStarted) {
        s_tunnel.loop();
    }
#endif
}

bool wifi_manager_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

void wifi_manager_reset() {
    Serial.println("[WIFI] Resetting WiFi settings...");
    s_wm.resetSettings();

    miner_config_t *config = nvs_config_get();
    config->ssid[0] = '\0';
    config->wifiPassword[0] = '\0';
    nvs_config_save(config);

    ESP.restart();
}

const char* wifi_manager_get_ip() {
    return s_ipAddress;
}
/*
 * SparkMiner - Live Stats Implementation
 * Fetches BTC price and network stats from public APIs
 *
 * Proxy Support:
 * - HTTP proxy for HTTPS APIs (avoids SSL on ESP32)
 * - Supports authenticated proxies (user:pass@host:port)
 * - Health monitoring with auto-disable/re-enable
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include "live_stats.h"
#include "board_config.h"
#include "../config/nvs_config.h"

// ============================================================
// Globals
// ============================================================

static live_stats_t s_stats = {0};
static char s_wallet[128] = {0};
static SemaphoreHandle_t s_statsMutex = NULL;

// Update timers
static uint32_t s_lastPriceUpdate = 0;
static uint32_t s_lastBlockUpdate = 0;
static uint32_t s_lastFeesUpdate = 0;
static uint32_t s_lastPoolUpdate = 0;

// Proxy state
static bool s_proxyHealthy = true;
static uint32_t s_proxyFailCount = 0;
static uint32_t s_lastProxyCheck = 0;

// Parsed proxy config (cached to avoid repeated parsing)
static char s_proxyHost[64] = {0};
static uint16_t s_proxyPort = 0;
static char s_proxyAuth[128] = {0};  // Base64 encoded user:pass
static bool s_proxyConfigured = false;
static bool s_httpsEnabled = false;

// Error rate limiting
static uint32_t s_lastErrorLog = 0;
static uint32_t s_errorCount = 0;

// ============================================================
// Proxy URL Parser
// ============================================================

/**
 * Parse proxy URL: http://[user:pass@]host:port
 * Returns true if valid proxy URL
 */
static bool parseProxyUrl(const char *url) {
    s_proxyHost[0] = '\0';
    s_proxyPort = 0;
    s_proxyAuth[0] = '\0';
    s_proxyConfigured = false;

    if (!url || strlen(url) < 10) return false;

    // Must start with http://
    if (strncmp(url, "http://", 7) != 0) {
        Serial.println("[STATS] Proxy URL must start with http://");
        return false;
    }

    const char *hostStart = url + 7;  // Skip "http://"
    const char *atSign = strchr(hostStart, '@');
    const char *colonPort = NULL;
    const char *hostEnd = NULL;

    if (atSign) {
        // Has authentication: user:pass@host:port
        char authPart[96];
        size_t authLen = atSign - hostStart;
        if (authLen >= sizeof(authPart)) authLen = sizeof(authPart) - 1;
        strncpy(authPart, hostStart, authLen);
        authPart[authLen] = '\0';

        // Base64 encode for Proxy-Authorization header
        String encoded = base64::encode((uint8_t*)authPart, strlen(authPart));
        strncpy(s_proxyAuth, encoded.c_str(), sizeof(s_proxyAuth) - 1);
        s_proxyAuth[sizeof(s_proxyAuth) - 1] = '\0';

        hostStart = atSign + 1;
    }

    // Find port separator
    colonPort = strchr(hostStart, ':');
    if (!colonPort) {
        Serial.println("[STATS] Proxy URL must include port (e.g., :8080)");
        return false;
    }

    // Extract host
    size_t hostLen = colonPort - hostStart;
    if (hostLen >= sizeof(s_proxyHost)) hostLen = sizeof(s_proxyHost) - 1;
    strncpy(s_proxyHost, hostStart, hostLen);
    s_proxyHost[hostLen] = '\0';

    // Extract port
    s_proxyPort = atoi(colonPort + 1);
    if (s_proxyPort == 0) {
        Serial.println("[STATS] Invalid proxy port");
        return false;
    }

    s_proxyConfigured = true;
    Serial.printf("[STATS] Proxy configured: %s:%d %s\n",
                  s_proxyHost, s_proxyPort,
                  s_proxyAuth[0] ? "(authenticated)" : "");
    return true;
}

// ============================================================
// HTTP Fetch Functions
// ============================================================

static StaticJsonDocument<2048> s_jsonDoc;

static void logError(const char *context, int code) {
    s_errorCount++;
    uint32_t now = millis();
    if (now - s_lastErrorLog > 60000) {
        Serial.printf("[STATS] %s error: %d (count: %lu)\n", context, code, s_errorCount);
        s_lastErrorLog = now;
        s_errorCount = 0;
    }
}

/**
 * Fetch URL via HTTP proxy (path-forwarding mode)
 * Request format: GET /https://target.com/path HTTP/1.1
 * Compatible with Cloudflare Workers and similar edge proxies
 */
static bool fetchViaProxy(const char *targetUrl, JsonDocument &doc) {
    if (!s_proxyConfigured || !s_proxyHealthy) return false;

    WiFiClient client;
    client.setTimeout(8000);

    if (!client.connect(s_proxyHost, s_proxyPort)) {
        logError("Proxy connect", -1);
        s_proxyFailCount++;
        return false;
    }

    // Build HTTP request using path-forwarding format
    // GET /https://api.coingecko.com/api/v3/... HTTP/1.1
    // This is compatible with Cloudflare Workers
    String request = "GET /";
    request += targetUrl;  // Full URL becomes path
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += s_proxyHost;
    if (s_proxyPort != 80) {
        request += ":";
        request += String(s_proxyPort);
    }
    request += "\r\n";

    // Add proxy authentication if configured (as Authorization header)
    if (s_proxyAuth[0]) {
        request += "Authorization: Basic ";
        request += s_proxyAuth;
        request += "\r\n";
    }

    request += "User-Agent: SparkMiner/1.0 ESP32\r\n";
    request += "Accept: application/json\r\n";
    request += "Connection: close\r\n";
    request += "\r\n";

    client.print(request);

    // Wait for response
    uint32_t timeout = millis() + 8000;
    while (client.connected() && !client.available() && millis() < timeout) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (!client.available()) {
        logError("Proxy timeout", -2);
        s_proxyFailCount++;
        client.stop();
        return false;
    }

    // Read status line
    String statusLine = client.readStringUntil('\n');
    int statusCode = 0;
    if (statusLine.startsWith("HTTP/")) {
        int spaceIdx = statusLine.indexOf(' ');
        if (spaceIdx > 0) {
            statusCode = statusLine.substring(spaceIdx + 1).toInt();
        }
    }

    if (statusCode != 200) {
        logError("Proxy response", statusCode);
        s_proxyFailCount++;
        client.stop();
        return false;
    }

    // Skip headers
    while (client.connected() && client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() == 0) break;
    }

    // Read body
    String body = client.readString();
    client.stop();

    // Parse JSON
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        logError("Proxy JSON", -3);
        s_proxyFailCount++;
        return false;
    }

    // Success - reset fail count
    s_proxyFailCount = 0;
    if (!s_proxyHealthy) {
        s_proxyHealthy = true;
        Serial.println("[STATS] Proxy recovered, re-enabling HTTPS stats");
    }
    return true;
}

/**
 * Fetch URL directly via HTTPS (CPU intensive, may cause issues)
 */
static bool fetchHttpsDirect(const char *url, JsonDocument &doc) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    secureClient.setTimeout(5000);

    HTTPClient http;
    http.setUserAgent("SparkMiner/1.0 ESP32");
    http.setTimeout(5000);

    vTaskDelay(1);  // Yield before SSL handshake

    if (!http.begin(secureClient, url)) {
        logError("HTTPS connect", -1);
        return false;
    }

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();
        DeserializationError err = deserializeJson(doc, payload);
        return !err;
    }

    logError("HTTPS request", httpCode);
    http.end();
    return false;
}

/**
 * Fetch URL via HTTP (no SSL)
 */
static bool fetchHttp(const char *url, JsonDocument &doc) {
    WiFiClient client;
    client.setTimeout(5000);

    HTTPClient http;
    http.setUserAgent("SparkMiner/1.0 ESP32");
    http.setTimeout(5000);

    if (!http.begin(client, url)) {
        return false;
    }

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();
        DeserializationError err = deserializeJson(doc, payload);
        return !err;
    }

    http.end();
    return false;
}

/**
 * Fetch JSON from URL - auto-selects method based on config
 * For HTTPS URLs: proxy -> direct HTTPS (if enabled) -> skip
 * For HTTP URLs: direct HTTP
 */
static bool fetchJson(const char *url, JsonDocument &doc) {
    bool isHttps = strncmp(url, "https://", 8) == 0;

    if (!isHttps) {
        // HTTP - always fetch directly
        return fetchHttp(url, doc);
    }

    // HTTPS URL - need proxy or enableHttpsStats
    if (s_proxyConfigured && s_proxyHealthy) {
        return fetchViaProxy(url, doc);
    }

    if (s_httpsEnabled) {
        return fetchHttpsDirect(url, doc);
    }

    // HTTPS not available - skip silently
    return false;
}

// ============================================================
// Proxy Health Monitoring
// ============================================================

static void checkProxyHealth() {
    if (!s_proxyConfigured) return;

    // Check if we've exceeded failure threshold
    if (s_proxyFailCount >= PROXY_MAX_FAILURES && s_proxyHealthy) {
        s_proxyHealthy = false;
        Serial.printf("[STATS] Proxy unhealthy after %d failures, disabling HTTPS stats\n",
                      PROXY_MAX_FAILURES);
    }

    // Periodically retry unhealthy proxy
    if (!s_proxyHealthy) {
        uint32_t now = millis();
        if (now - s_lastProxyCheck > PROXY_HEALTH_CHECK_MS) {
            s_lastProxyCheck = now;
            Serial.println("[STATS] Checking proxy health...");

            // Try a simple request to test proxy
            s_proxyHealthy = true;  // Temporarily enable for test
            s_jsonDoc.clear();

            // Use CoinGecko ping endpoint - lightweight HTTPS test
            if (fetchViaProxy("https://api.coingecko.com/api/v3/ping", s_jsonDoc)) {
                Serial.println("[STATS] Proxy health check passed");
                s_proxyFailCount = 0;
            } else {
                s_proxyHealthy = false;
                Serial.println("[STATS] Proxy still unhealthy");
            }
        }
    }
}

// ============================================================
// API Updaters
// ============================================================

static void updatePrice() {
    // Only fetch if HTTPS is available (proxy or direct)
    if (!s_proxyConfigured && !s_httpsEnabled) return;

    s_jsonDoc.clear();
    if (fetchJson(API_BTC_PRICE, s_jsonDoc)) {
        if (s_jsonDoc.containsKey("bitcoin")) {
            xSemaphoreTake(s_statsMutex, portMAX_DELAY);
            s_stats.btcPriceUsd = s_jsonDoc["bitcoin"]["usd"];
            s_stats.priceTimestamp = millis();
            s_stats.priceValid = true;
            xSemaphoreGive(s_statsMutex);
        }
    }
}

static void updateBlockHeight() {
    // HTTP API - always works
    WiFiClient client;
    client.setTimeout(5000);

    HTTPClient http;
    http.setUserAgent("SparkMiner/1.0");
    http.setTimeout(5000);

    if (http.begin(client, API_BLOCK_HEIGHT)) {
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            uint32_t height = payload.toInt();

            if (height > 0) {
                xSemaphoreTake(s_statsMutex, portMAX_DELAY);
                s_stats.blockHeight = height;
                s_stats.blockTimestamp = millis();
                s_stats.blockValid = true;
                xSemaphoreGive(s_statsMutex);
            }
        }
        http.end();
    }
}

static void updateFees() {
    // HTTP API - always works
    s_jsonDoc.clear();
    if (fetchHttp(API_FEES, s_jsonDoc)) {
        xSemaphoreTake(s_statsMutex, portMAX_DELAY);
        s_stats.fastestFee = s_jsonDoc["fastestFee"];
        s_stats.halfHourFee = s_jsonDoc["halfHourFee"];
        s_stats.hourFee = s_jsonDoc["hourFee"];
        s_stats.feesTimestamp = millis();
        s_stats.feesValid = true;
        xSemaphoreGive(s_statsMutex);
    }
}

static void updatePoolStats() {
    // Only fetch if HTTPS is available (proxy or direct)
    if (!s_proxyConfigured && !s_httpsEnabled) return;
    if (strlen(s_wallet) == 0) return;

    char url[256];
    snprintf(url, sizeof(url), "%s%s", API_PUBLIC_POOL, s_wallet);

    s_jsonDoc.clear();
    if (fetchJson(url, s_jsonDoc)) {
        xSemaphoreTake(s_statsMutex, portMAX_DELAY);
        s_stats.poolWorkersCount = s_jsonDoc["workersCount"];
        const char *hashrate = s_jsonDoc["hashrate"];
        const char *bestDiff = s_jsonDoc["bestDifficulty"];
        if (hashrate) {
            strncpy(s_stats.poolTotalHashrate, hashrate, sizeof(s_stats.poolTotalHashrate) - 1);
            s_stats.poolTotalHashrate[sizeof(s_stats.poolTotalHashrate) - 1] = '\0';
        }
        if (bestDiff) {
            strncpy(s_stats.poolBestDifficulty, bestDiff, sizeof(s_stats.poolBestDifficulty) - 1);
            s_stats.poolBestDifficulty[sizeof(s_stats.poolBestDifficulty) - 1] = '\0';
        }
        s_stats.poolValid = true;
        xSemaphoreGive(s_statsMutex);
    }
}

// ============================================================
// Public API
// ============================================================

void live_stats_init() {
    s_statsMutex = xSemaphoreCreateMutex();

    // Load proxy config
    miner_config_t *config = nvs_config_get();
    if (config->statsProxyUrl[0]) {
        parseProxyUrl(config->statsProxyUrl);
    }
    s_httpsEnabled = config->enableHttpsStats;

    // Log configuration
    if (s_proxyConfigured) {
        Serial.println("[STATS] HTTPS stats enabled via proxy");
    } else if (s_httpsEnabled) {
        Serial.println("[STATS] HTTPS stats enabled (direct - may affect stability)");
    } else {
        Serial.println("[STATS] HTTPS stats disabled (HTTP APIs only)");
    }

    // Start background task
    xTaskCreatePinnedToCore(
        live_stats_task,
        "StatsTask",
        STATS_STACK,
        NULL,
        STATS_PRIORITY,
        NULL,
        STATS_CORE
    );
}

const live_stats_t *live_stats_get() {
    return &s_stats;
}

void live_stats_set_wallet(const char *wallet) {
    if (wallet) {
        strncpy(s_wallet, wallet, sizeof(s_wallet) - 1);
    }
}

void live_stats_update() {
    // Triggered manually - task handles autonomous updates
}

void live_stats_force_update() {
    s_lastPriceUpdate = 0;
    s_lastBlockUpdate = 0;
    s_lastFeesUpdate = 0;
    s_lastPoolUpdate = 0;
}

void live_stats_task(void *param) {
    // Initial delay to let WiFi settle
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    while (true) {
        if (WiFi.status() == WL_CONNECTED) {
            uint32_t now = millis();

            // Check proxy health periodically
            checkProxyHealth();

            // Stagger updates with generous yields
            if (now - s_lastBlockUpdate > UPDATE_BLOCK_MS) {
                updateBlockHeight();
                s_lastBlockUpdate = millis();
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }

            if (now - s_lastFeesUpdate > UPDATE_FEES_MS) {
                updateFees();
                s_lastFeesUpdate = millis();
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }

            // HTTPS APIs (only if proxy or direct HTTPS enabled)
            if (s_proxyConfigured || s_httpsEnabled) {
                if (now - s_lastPriceUpdate > UPDATE_PRICE_MS) {
                    updatePrice();
                    s_lastPriceUpdate = millis();
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                }

                if (now - s_lastPoolUpdate > UPDATE_POOL_MS) {
                    updatePoolStats();
                    s_lastPoolUpdate = millis();
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                }
            }
        }

        // Yield to let other tasks run
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

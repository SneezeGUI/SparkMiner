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
static uint32_t s_lastNetworkUpdate = 0;

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

// Stats configuration
static bool s_statsEnabled = true;       // Master enable/disable
static char s_customApiUrl[128] = {0};   // Custom unified stats API endpoint
static uint32_t s_lastCustomApiUpdate = 0;

// Error rate limiting
static uint32_t s_lastErrorLog = 0;
static uint32_t s_errorCount = 0;

// ============================================================
// Proxy URL Parser
// ============================================================

/**
 * Parse proxy configuration in multiple formats:
 *   1. URL format: http://[user:pass@]host:port
 *   2. Simple format: host:port:user:pass
 *   3. Simple format without auth: host:port
 * Returns true if valid proxy config
 */
static bool parseProxyUrl(const char *url) {
    s_proxyHost[0] = '\0';
    s_proxyPort = 0;
    s_proxyAuth[0] = '\0';
    s_proxyConfigured = false;

    if (!url || strlen(url) < 5) return false;

    // Check if it's URL format (starts with http://)
    if (strncmp(url, "http://", 7) == 0) {
        // URL format: http://[user:pass@]host:port
        const char *hostStart = url + 7;
        const char *atSign = strchr(hostStart, '@');

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
        const char *colonPort = strchr(hostStart, ':');
        if (!colonPort) {
            Serial.println("[STATS] Proxy URL must include port");
            return false;
        }

        // Extract host
        size_t hostLen = colonPort - hostStart;
        if (hostLen >= sizeof(s_proxyHost)) hostLen = sizeof(s_proxyHost) - 1;
        strncpy(s_proxyHost, hostStart, hostLen);
        s_proxyHost[hostLen] = '\0';

        // Extract port (stop at any trailing characters)
        s_proxyPort = atoi(colonPort + 1);
    } else {
        // Simple format: host:port[:user:pass]
        // Count colons to determine format
        int colonCount = 0;
        const char *colons[4] = {NULL};
        const char *p = url;
        while (*p && colonCount < 4) {
            if (*p == ':') {
                colons[colonCount++] = p;
            }
            p++;
        }

        if (colonCount < 1) {
            Serial.println("[STATS] Proxy must include port (host:port)");
            return false;
        }

        // Extract host (everything before first colon)
        size_t hostLen = colons[0] - url;
        if (hostLen >= sizeof(s_proxyHost)) hostLen = sizeof(s_proxyHost) - 1;
        strncpy(s_proxyHost, url, hostLen);
        s_proxyHost[hostLen] = '\0';

        // Extract port
        s_proxyPort = atoi(colons[0] + 1);

        // Check for auth (host:port:user:pass format)
        if (colonCount >= 3) {
            // Extract user (between 2nd and 3rd colon)
            char user[64] = {0};
            size_t userLen = colons[2] - (colons[1] + 1);
            if (userLen >= sizeof(user)) userLen = sizeof(user) - 1;
            strncpy(user, colons[1] + 1, userLen);
            user[userLen] = '\0';

            // Extract pass (after 3rd colon)
            const char *pass = colons[2] + 1;

            // Build auth string "user:pass" and base64 encode
            char authPart[96];
            snprintf(authPart, sizeof(authPart), "%s:%s", user, pass);
            String encoded = base64::encode((uint8_t*)authPart, strlen(authPart));
            strncpy(s_proxyAuth, encoded.c_str(), sizeof(s_proxyAuth) - 1);
            s_proxyAuth[sizeof(s_proxyAuth) - 1] = '\0';
        }
    }

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
 * Extract hostname from URL (e.g., "https://api.coingecko.com/path" -> "api.coingecko.com")
 */
static String extractHostFromUrl(const char *url) {
    const char *start = strstr(url, "://");
    if (!start) return "";
    start += 3;  // Skip "://"

    const char *end = strchr(start, '/');
    if (!end) end = start + strlen(start);

    // Check for port in host
    const char *colon = strchr(start, ':');
    if (colon && colon < end) end = colon;

    return String(start).substring(0, end - start);
}

/**
 * Extract path from URL (e.g., "https://api.coingecko.com/api/v3/..." -> "/api/v3/...")
 */
static String extractPathFromUrl(const char *url) {
    const char *start = strstr(url, "://");
    if (!start) return "/";
    start += 3;  // Skip "://"

    const char *path = strchr(start, '/');
    if (!path) return "/";

    return String(path);
}

// Proxy method preference: 0=auto, 1=GET (SSL bump), 2=CONNECT (tunnel)
static uint8_t s_proxyMethod = 0;

/**
 * Fetch URL via HTTP proxy using GET method (SSL bumping mode)
 * Request format: GET https://target.com/path HTTP/1.1
 * Requires proxy with SSL bumping (decrypts/re-encrypts HTTPS)
 */
static bool fetchViaProxyGet(const char *targetUrl, JsonDocument &doc) {
    WiFiClient client;
    client.setTimeout(8000);

    if (!client.connect(s_proxyHost, s_proxyPort)) {
        return false;
    }

    String targetHost = extractHostFromUrl(targetUrl);

    // Build HTTP request - full URL in request line for SSL bumping proxy
    String request = "GET ";
    request += targetUrl;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += targetHost;
    request += "\r\n";

    if (s_proxyAuth[0]) {
        request += "Proxy-Authorization: Basic ";
        request += s_proxyAuth;
        request += "\r\n";
    }

    request += "User-Agent: SparkMiner/1.0 ESP32\r\n";
    request += "Accept: application/json\r\n";
    request += "Connection: close\r\n\r\n";

    client.print(request);

    // Wait for response
    uint32_t timeout = millis() + 8000;
    while (client.connected() && !client.available() && millis() < timeout) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (!client.available()) {
        client.stop();
        return false;
    }

    // Read status line
    String statusLine = client.readStringUntil('\n');
    statusLine.trim();

    int statusCode = 0;
    if (statusLine.startsWith("HTTP/")) {
        int spaceIdx = statusLine.indexOf(' ');
        if (spaceIdx > 0) {
            statusCode = statusLine.substring(spaceIdx + 1).toInt();
        }
    }

    if (statusCode != 200) {
        Serial.printf("[STATS] Proxy error: %d\n", statusCode);
        client.stop();
        return false;
    }

    // Skip headers, look for Content-Length and Transfer-Encoding
    int contentLength = -1;
    bool chunked = false;
    while (client.connected() || client.available()) {
        if (!client.available()) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("Content-Length:")) {
            contentLength = line.substring(15).toInt();
        }
        if (line.indexOf("chunked") >= 0) {
            chunked = true;
        }
        if (line.length() == 0) break;
    }

    // Read body with size limit to prevent OOM
    const int MAX_BODY = 4096;
    String body;
    body.reserve(MAX_BODY);

    // Read body - continue while connected OR data available in buffer
    uint32_t readTimeout = millis() + 5000;
    while ((client.connected() || client.available()) && (int)body.length() < MAX_BODY && millis() < readTimeout) {
        if (client.available()) {
            char c = client.read();
            body += c;
        } else {
            vTaskDelay(1);
        }
    }
    client.stop();

    // Handle chunked transfer encoding
    if (chunked && body.length() > 0) {
        String decoded;
        decoded.reserve(body.length());
        int pos = 0;
        while (pos < (int)body.length()) {
            // Read chunk size (hex)
            int lineEnd = body.indexOf('\n', pos);
            if (lineEnd < 0) break;
            String sizeLine = body.substring(pos, lineEnd);
            sizeLine.trim();
            int chunkSize = strtol(sizeLine.c_str(), NULL, 16);
            if (chunkSize == 0) break;  // End of chunks
            pos = lineEnd + 1;
            // Read chunk data
            if (pos + chunkSize <= (int)body.length()) {
                decoded += body.substring(pos, pos + chunkSize);
            }
            pos += chunkSize + 2;  // Skip data + \r\n
        }
        body = decoded;
    }

    if (body.length() == 0) {
        Serial.println("[STATS] Proxy: empty response");
        return false;
    }

    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        logError("Proxy JSON", err.code());
        return false;
    }
    return true;
}

/**
 * Fetch URL via HTTP proxy using CONNECT method (HTTPS tunneling)
 *
 * NOTE: ESP32 Arduino WiFiClientSecure doesn't support upgrading existing
 * TCP connections to TLS. This would require direct mbedtls API usage.
 * For now, use a proxy with SSL bumping (GET method) instead.
 *
 * If CONNECT tunnel is needed, implement via mbedtls_ssl_set_bio() with
 * the existing socket fd from client.fd().
 */
static bool fetchViaProxyConnect(const char *targetUrl, JsonDocument &doc) {
    // CONNECT tunneling not yet implemented for ESP32
    // Use proxy with SSL bumping and GET method instead
    (void)targetUrl;
    (void)doc;
    return false;
}

/**
 * Fetch URL via HTTP proxy - tries both methods with auto-detection
 * 1. GET method (SSL bumping) - simpler, lower memory
 * 2. CONNECT method (tunneling) - works without SSL bumping
 */
static bool fetchViaProxy(const char *targetUrl, JsonDocument &doc) {
    if (!s_proxyConfigured || !s_proxyHealthy) return false;

    bool success = false;

    // Try preferred method first, or auto-detect
    if (s_proxyMethod == 0 || s_proxyMethod == 1) {
        // Try GET method (SSL bumping)
        success = fetchViaProxyGet(targetUrl, doc);
        if (success) {
            if (s_proxyMethod == 0) s_proxyMethod = 1;
            s_proxyFailCount = 0;
            return true;
        }
    }

    if (s_proxyMethod == 0 || s_proxyMethod == 2) {
        // Try CONNECT method (tunneling)
        success = fetchViaProxyConnect(targetUrl, doc);
        if (success) {
            if (s_proxyMethod == 0) s_proxyMethod = 2;
            s_proxyFailCount = 0;
            return true;
        }
    }

    // Both methods failed
    s_proxyFailCount++;
    return false;
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
// Custom API Endpoint (Unified Stats)
// ============================================================

/**
 * Fetch stats from custom unified API endpoint (e.g., stratum proxy /stats)
 * This endpoint returns all external stats in one HTTP call, avoiding
 * HTTPS overhead on ESP32.
 *
 * Expected response format:
 * {
 *   "btc_price_usd": 105420.50,
 *   "block_height": 881234,
 *   "network_hashrate": "789.5 EH/s",
 *   "network_difficulty": "95.67T",
 *   "fee_half_hour": 12,
 *   "fee_fastest": 25,
 *   "external_stats_age": 45
 * }
 */
static bool fetchFromCustomApi() {
    if (s_customApiUrl[0] == '\0') return false;

    s_jsonDoc.clear();
    if (!fetchHttp(s_customApiUrl, s_jsonDoc)) {
        logError("Custom API", -1);
        return false;
    }

    xSemaphoreTake(s_statsMutex, portMAX_DELAY);

    // BTC Price
    if (s_jsonDoc.containsKey("btc_price_usd")) {
        float price = s_jsonDoc["btc_price_usd"];
        if (price > 0) {
            s_stats.btcPriceUsd = price;
            s_stats.priceTimestamp = millis();
            s_stats.priceValid = true;
        }
    }

    // Block height
    if (s_jsonDoc.containsKey("block_height")) {
        uint32_t height = s_jsonDoc["block_height"];
        if (height > 0) {
            s_stats.blockHeight = height;
            s_stats.blockTimestamp = millis();
            s_stats.blockValid = true;
        }
    }

    // Network hashrate (pre-formatted string from proxy)
    if (s_jsonDoc.containsKey("network_hashrate")) {
        const char *hr = s_jsonDoc["network_hashrate"];
        if (hr && strlen(hr) > 0) {
            strncpy(s_stats.networkHashrate, hr, sizeof(s_stats.networkHashrate) - 1);
            s_stats.networkHashrate[sizeof(s_stats.networkHashrate) - 1] = '\0';
            s_stats.networkValid = true;
        }
    }

    // Network difficulty (pre-formatted string from proxy)
    if (s_jsonDoc.containsKey("network_difficulty")) {
        const char *diff = s_jsonDoc["network_difficulty"];
        if (diff && strlen(diff) > 0) {
            strncpy(s_stats.networkDifficulty, diff, sizeof(s_stats.networkDifficulty) - 1);
            s_stats.networkDifficulty[sizeof(s_stats.networkDifficulty) - 1] = '\0';
        }
    }

    // Fees
    if (s_jsonDoc.containsKey("fee_half_hour")) {
        s_stats.halfHourFee = s_jsonDoc["fee_half_hour"];
        s_stats.feesTimestamp = millis();
        s_stats.feesValid = true;
    }
    if (s_jsonDoc.containsKey("fee_fastest")) {
        s_stats.fastestFee = s_jsonDoc["fee_fastest"];
    }
    if (s_jsonDoc.containsKey("fee_hour")) {
        s_stats.hourFee = s_jsonDoc["fee_hour"];
    }

    // Pool Stats from Proxy
    if (s_jsonDoc.containsKey("workers")) {
        s_stats.poolWorkersCount = s_jsonDoc["workers"];
        s_stats.poolValid = true;
    }

    if (s_jsonDoc.containsKey("failovers")) {
        s_stats.failovers = s_jsonDoc["failovers"];
    }

    if (s_jsonDoc.containsKey("pool_name")) {
        const char *name = s_jsonDoc["pool_name"];
        if (name) {
            strncpy(s_stats.poolName, name, sizeof(s_stats.poolName) - 1);
            s_stats.poolName[sizeof(s_stats.poolName) - 1] = '\0';
            s_stats.poolValid = true;
        }
    }

    xSemaphoreGive(s_statsMutex);

    Serial.printf("[STATS] Custom API updated: BTC $%.0f, Block %lu, Workers %d\n",
                  s_stats.btcPriceUsd, s_stats.blockHeight, s_stats.poolWorkersCount);
    return true;
}

// ============================================================
// API Updaters
// ============================================================

static void updatePrice() {
    // Only fetch if HTTPS is available (proxy or direct)
    if (!s_proxyConfigured && !s_httpsEnabled) return;
    if (s_proxyConfigured && !s_proxyHealthy) return;

    s_jsonDoc.clear();
    if (fetchJson(API_BTC_PRICE, s_jsonDoc)) {
        if (s_jsonDoc.containsKey("bitcoin")) {
            xSemaphoreTake(s_statsMutex, portMAX_DELAY);
            s_stats.btcPriceUsd = s_jsonDoc["bitcoin"]["usd"];
            s_stats.priceTimestamp = millis();
            s_stats.priceValid = true;
            xSemaphoreGive(s_statsMutex);
            Serial.printf("[STATS] BTC price updated: $%.0f\n", s_stats.btcPriceUsd);
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
    if (s_proxyConfigured && !s_proxyHealthy) return;
    if (strlen(s_wallet) == 0) {
        Serial.println("[STATS] Pool stats skipped: no wallet configured");
        return;
    }

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
        Serial.printf("[STATS] Pool stats updated: %d workers\n", s_stats.poolWorkersCount);
    }
}

static void updateNetworkHashrate() {
    // Only fetch if HTTPS is available (proxy or direct)
    if (!s_proxyConfigured && !s_httpsEnabled) return;
    if (s_proxyConfigured && !s_proxyHealthy) return;

    // The hashrate API returns ~400KB response with historical data.
    // We use a filter to extract only the fields we need, ignoring the "hashrates" array.
    StaticJsonDocument<128> filter;
    filter["currentHashrate"] = true;
    filter["currentDifficulty"] = true;

    WiFiClient client;
    client.setTimeout(8000);

    if (!client.connect(s_proxyHost, s_proxyPort)) {
        return;
    }

    String targetHost = extractHostFromUrl(API_HASHRATE);

    // Build request
    String request = "GET ";
    request += API_HASHRATE;
    request += " HTTP/1.1\r\nHost: ";
    request += targetHost;
    request += "\r\n";
    if (s_proxyAuth[0]) {
        request += "Proxy-Authorization: Basic ";
        request += s_proxyAuth;
        request += "\r\n";
    }
    request += "Connection: close\r\n\r\n";
    client.print(request);

    // Wait for response
    uint32_t timeout = millis() + 10000;
    while (client.connected() && !client.available() && millis() < timeout) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (!client.available()) {
        client.stop();
        return;
    }

    // Skip HTTP headers
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) break;
    }

    // Parse JSON with filter (ignores hashrates array, saves memory)
    s_jsonDoc.clear();
    DeserializationError err = deserializeJson(s_jsonDoc, client, DeserializationOption::Filter(filter));
    client.stop();

    if (err) {
        Serial.printf("[STATS] Hashrate parse error: %s\n", err.c_str());
        return;
    }

    xSemaphoreTake(s_statsMutex, portMAX_DELAY);

    // Parse hashrate (hashes/s)
    double hashrate = s_jsonDoc["currentHashrate"] | 0.0;
    if (hashrate > 0) {
        s_stats.networkHashrateRaw = hashrate;

        // Format hashrate string
        if (hashrate > 1e18) {
            snprintf(s_stats.networkHashrate, sizeof(s_stats.networkHashrate), "%.2f EH/s", hashrate / 1e18);
        } else if (hashrate > 1e15) {
            snprintf(s_stats.networkHashrate, sizeof(s_stats.networkHashrate), "%.2f PH/s", hashrate / 1e15);
        } else {
            snprintf(s_stats.networkHashrate, sizeof(s_stats.networkHashrate), "%.2f TH/s", hashrate / 1e12);
        }
    }

    // Get difficulty if available
    double diff = s_jsonDoc["currentDifficulty"] | 0.0;
    if (diff > 0) {
        s_stats.difficultyRaw = diff;
        snprintf(s_stats.networkDifficulty, sizeof(s_stats.networkDifficulty), "%.2f T", diff / 1e12);
    }

    s_stats.networkValid = true;
    xSemaphoreGive(s_statsMutex);
    Serial.printf("[STATS] Network: %s, Diff: %s\n", s_stats.networkHashrate, s_stats.networkDifficulty);
}

static void updateNetworkDifficulty() {
    // Only fetch if HTTPS is available (proxy or direct)
    if (!s_proxyConfigured && !s_httpsEnabled) return;
    if (s_proxyConfigured && !s_proxyHealthy) return;

    s_jsonDoc.clear();
    if (fetchJson(API_DIFFICULTY, s_jsonDoc)) {
        xSemaphoreTake(s_statsMutex, portMAX_DELAY);
        s_stats.difficultyProgress = s_jsonDoc["progressPercent"];
        double change = s_jsonDoc["difficultyChange"]; // API returns float
        s_stats.difficultyChange = (int32_t)change;
        xSemaphoreGive(s_statsMutex);
        Serial.printf("[STATS] Difficulty adj: %.1f%% progress, %.1f%% change\n", 
                      s_stats.difficultyProgress, change);
    }
}

// ============================================================
// Public API
// ============================================================

void live_stats_init() {
    s_statsMutex = xSemaphoreCreateMutex();

    // Load stats config
    miner_config_t *config = nvs_config_get();

    // Master enable/disable
    s_statsEnabled = config->statsEnabled;
    if (!s_statsEnabled) {
        Serial.println("[STATS] Live stats DISABLED by user");
        return;  // Don't start task if disabled
    }

    // Custom API endpoint (highest priority)
    if (config->statsApiUrl[0]) {
        strncpy(s_customApiUrl, config->statsApiUrl, sizeof(s_customApiUrl) - 1);
        s_customApiUrl[sizeof(s_customApiUrl) - 1] = '\0';
        Serial.printf("[STATS] Custom API endpoint: %s\n", s_customApiUrl);
    }

    // HTTP proxy config
    if (config->statsProxyUrl[0]) {
        parseProxyUrl(config->statsProxyUrl);
    }

    // Direct HTTPS mode
    s_httpsEnabled = config->enableHttpsStats;

    // Log configuration priority
    if (s_customApiUrl[0]) {
        Serial.println("[STATS] Mode: Custom API (HTTP - no SSL overhead)");
    } else if (s_proxyConfigured) {
        Serial.println("[STATS] Mode: Proxy (HTTPS via SSL bumping)");
    } else if (s_httpsEnabled) {
        Serial.println("[STATS] Mode: Direct HTTPS (may affect hashrate)");
    } else {
        Serial.println("[STATS] Mode: HTTP-only (block height, fees)");
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

void live_stats_get_copy(live_stats_t *dest) {
    if (!dest) return;
    xSemaphoreTake(s_statsMutex, portMAX_DELAY);
    memcpy(dest, &s_stats, sizeof(live_stats_t));
    xSemaphoreGive(s_statsMutex);
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
    s_lastNetworkUpdate = 0;
}

void live_stats_task(void *param) {
    // Initial delay to let WiFi settle
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // Force immediate first update by backdating timers
    uint32_t bootTime = millis();
    s_lastBlockUpdate = bootTime - UPDATE_BLOCK_MS - 1000;
    s_lastFeesUpdate = bootTime - UPDATE_FEES_MS - 2000;
    s_lastPriceUpdate = bootTime - UPDATE_PRICE_MS - 3000;
    s_lastPoolUpdate = bootTime - UPDATE_POOL_MS - 4000;
    s_lastNetworkUpdate = bootTime - UPDATE_NETWORK_MS - 5000;
    s_lastCustomApiUpdate = bootTime - UPDATE_PRICE_MS - 1000;

    Serial.println("[STATS] Task started");

    while (true) {
        if (WiFi.status() == WL_CONNECTED) {
            uint32_t now = millis();

            // Priority 1: Custom API endpoint (fetches all stats in one call)
            if (s_customApiUrl[0]) {
                if (now - s_lastCustomApiUpdate > UPDATE_PRICE_MS) {
                    fetchFromCustomApi();
                    s_lastCustomApiUpdate = millis();
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                }
            } else {
                // Priority 2/3/4: Use individual APIs

                // Check proxy health periodically
                checkProxyHealth();

                // HTTP APIs (always available - no SSL)
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

                    if (now - s_lastNetworkUpdate > UPDATE_NETWORK_MS) {
                        updateNetworkHashrate();
                        vTaskDelay(500 / portTICK_PERIOD_MS);
                        updateNetworkDifficulty();
                        s_lastNetworkUpdate = millis();
                        vTaskDelay(500 / portTICK_PERIOD_MS);
                    }
                }
            }
        }

        // Yield to let other tasks run
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

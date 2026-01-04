/*
 * SparkMiner - Live Stats Implementation
 * Fetches BTC price and network stats from public APIs
 *
 * SSL error rate limiting: Only log once per 60 seconds to reduce spam
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "live_stats.h"
#include "board_config.h"

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

// SSL error rate limiting
static uint32_t s_lastSSLErrorLog = 0;
static uint32_t s_sslErrorCount = 0;

// ============================================================
// Helper Functions
// ============================================================

static void logSSLError(const char *context, int code) {
    s_sslErrorCount++;
    uint32_t now = millis();
    // Only log once per minute to avoid serial spam
    if (now - s_lastSSLErrorLog > 60000) {
        Serial.printf("[STATS] SSL %s error: %d (count: %lu, rate limited)\n",
                      context, code, s_sslErrorCount);
        s_lastSSLErrorLog = now;
        s_sslErrorCount = 0;
    }
}

static bool fetchJson(const char *url, DynamicJsonDocument &doc) {
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate verification
    client.setTimeout(10000);

    HTTPClient http;
    http.setUserAgent("SparkMiner/1.0 ESP32");
    http.setTimeout(10000);

    if (!http.begin(client, url)) {
        logSSLError("connect", -1);
        return false;
    }

    int httpCode = http.GET();
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            DeserializationError err = deserializeJson(doc, payload);
            http.end();
            if (err) {
                return false;
            }
            return true;
        }
    } else {
        logSSLError("request", httpCode);
    }

    http.end();
    return false;
}

// ============================================================
// API Updaters
// ============================================================

static void updatePrice() {
    DynamicJsonDocument doc(1024);
    if (fetchJson(API_BTC_PRICE, doc)) {
        if (doc.containsKey("bitcoin")) {
            xSemaphoreTake(s_statsMutex, portMAX_DELAY);
            s_stats.btcPriceUsd = doc["bitcoin"]["usd"];
            s_stats.priceTimestamp = millis();
            s_stats.priceValid = true;
            xSemaphoreGive(s_statsMutex);
        }
    }
}

static void updateBlockHeight() {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);

    HTTPClient http;
    http.setUserAgent("SparkMiner/1.0");
    http.setTimeout(10000);

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
        } else if (httpCode < 0) {
            logSSLError("block", httpCode);
        }
        http.end();
    }
}

static void updateFees() {
    DynamicJsonDocument doc(1024);
    if (fetchJson(API_FEES, doc)) {
        xSemaphoreTake(s_statsMutex, portMAX_DELAY);
        s_stats.fastestFee = doc["fastestFee"];
        s_stats.halfHourFee = doc["halfHourFee"];
        s_stats.hourFee = doc["hourFee"];
        s_stats.feesTimestamp = millis();
        s_stats.feesValid = true;
        xSemaphoreGive(s_statsMutex);
    }
}

static void updatePoolStats() {
    if (strlen(s_wallet) == 0) return;

    char url[256];
    snprintf(url, sizeof(url), "%s%s", API_PUBLIC_POOL, s_wallet);

    DynamicJsonDocument doc(2048);
    if (fetchJson(url, doc)) {
        xSemaphoreTake(s_statsMutex, portMAX_DELAY);
        s_stats.poolWorkersCount = doc["workersCount"];
        s_stats.poolTotalHashrate = doc["hashrate"].as<String>();
        s_stats.poolBestDifficulty = doc["bestDifficulty"].as<String>();
        s_stats.poolValid = true;
        xSemaphoreGive(s_statsMutex);
    }
}

// ============================================================
// Public API
// ============================================================

void live_stats_init() {
    s_statsMutex = xSemaphoreCreateMutex();

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
    Serial.println("[STATS] Live stats initialized (HTTPS enabled)");

    // Initial delay to let WiFi settle
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    while (true) {
        if (WiFi.status() == WL_CONNECTED) {
            uint32_t now = millis();

            // Stagger updates to avoid memory spikes
            if (now - s_lastPriceUpdate > UPDATE_PRICE_MS) {
                updatePrice();
                s_lastPriceUpdate = millis();
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }

            if (now - s_lastBlockUpdate > UPDATE_BLOCK_MS) {
                updateBlockHeight();
                s_lastBlockUpdate = millis();
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }

            if (now - s_lastFeesUpdate > UPDATE_FEES_MS) {
                updateFees();
                s_lastFeesUpdate = millis();
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }

            if (now - s_lastPoolUpdate > UPDATE_POOL_MS) {
                updatePoolStats();
                s_lastPoolUpdate = millis();
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

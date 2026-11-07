/*
 * SparkMiner - Local HTTP Stats API (Issue #12)
 * AxeOS / ESP-Miner compatible endpoints for HashWatcher & Home Assistant
 *
 * GPL v3 License
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <stdarg.h>
#include <board_config.h>
#include "web_api.h"
#include "../config/nvs_config.h"
#include "../mining/miner.h"
#include "../stratum/stratum.h"
#include "../stats/live_stats.h"

#ifndef AUTO_VERSION
    #define AUTO_VERSION "dev"
#endif

// Internal ESP32 temperature sensor (rough, uncalibrated on classic ESP32)
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2) || \
    defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    #define WEB_API_HAS_TEMP 1
#else
    #define WEB_API_HAS_TEMP 0
#endif

// ============================================================
// State
// ============================================================

static WebServer *s_server = nullptr;
static TaskHandle_t s_task = nullptr;
static bool s_running = false;
static bool s_mdnsStarted = false;
static char s_hostname[32] = "";
static IPAddress s_lastIp;

// Hashrate tracking: hash-counter snapshot every 10 s for 1 h (360 x 12 bytes)
#define HR_SLOT_MS      10000
#define HR_SLOTS        361
typedef struct {
    uint64_t hashes;
    uint32_t ms;
} hr_slot_t;
static hr_slot_t s_hrRing[HR_SLOTS];
static uint16_t s_hrHead = 0;       // Next write index
static uint16_t s_hrCount = 0;      // Valid entries
static uint32_t s_lastHrSample = 0;
static double s_hashRateEma = 0.0;  // H/s, 10 s samples, alpha 0.3
static bool s_emaInit = false;

// Statistics history (AxeOS /api/system/statistics)
typedef struct {
    float hashrate;      // GH/s (AxeOS unit)
    float hashrate1m;    // GH/s
    float hashrate10m;   // GH/s
    float hashrate1h;    // GH/s
    float asicTemp;      // Internal ESP32 temp, degC
    int8_t wifiRssi;
    uint32_t freeHeap;
    float responseTime;  // ms
    uint32_t timestamp;  // ms since boot
} stat_sample_t;
static stat_sample_t s_stats[WEB_API_STATS_SAMPLES];
static uint16_t s_statHead = 0;
static uint16_t s_statCount = 0;
static uint32_t s_lastStatSample = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// Event log (GET /api/log)
static char s_log[WEB_API_LOG_LINES][WEB_API_LOG_LINE_LEN];
static uint32_t s_logMs[WEB_API_LOG_LINES];
static uint16_t s_logHead = 0;
static uint16_t s_logCount = 0;
static uint32_t s_logTotal = 0;
static portMUX_TYPE s_logMux = portMUX_INITIALIZER_UNLOCKED;

// ============================================================
// Helpers
// ============================================================

static void buildHostname() {
    if (s_hostname[0]) return;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_hostname, sizeof(s_hostname), "sparkminer-%02x%02x", mac[4], mac[5]);
}

static uint64_t readHashes() {
    // 64-bit volatile read is not atomic on Xtensa/RISC-V; read until stable
    mining_stats_t *m = miner_get_stats();
    uint64_t a, b;
    do {
        a = m->hashes;
        b = m->hashes;
    } while (a != b && (b - a) > 1000000ULL);
    return b;
}

// Average H/s over the last windowMs, using the hash-counter ring buffer
static double hashRateOver(uint32_t windowMs) {
    double rate = 0.0;
    portENTER_CRITICAL(&s_mux);
    if (s_hrCount >= 2) {
        uint16_t newest = (s_hrHead + HR_SLOTS - 1) % HR_SLOTS;
        uint16_t steps = windowMs / HR_SLOT_MS;
        if (steps > s_hrCount - 1) steps = s_hrCount - 1;
        if (steps < 1) steps = 1;
        uint16_t oldest = (newest + HR_SLOTS - steps) % HR_SLOTS;
        uint32_t dt = s_hrRing[newest].ms - s_hrRing[oldest].ms;
        uint64_t dh = s_hrRing[newest].hashes - s_hrRing[oldest].hashes;
        if (dt > 0) rate = (double)dh * 1000.0 / (double)dt;
    }
    portEXIT_CRITICAL(&s_mux);
    return rate;
}

static float readTemp() {
#if WEB_API_HAS_TEMP
    float t = temperatureRead();
    // Classic ESP32 without a working sensor reports ~53.3 (raw 128)
    if (isnan(t) || t < -40.0f || t > 150.0f) return 0.0f;
    return t;
#else
    return 0.0f;
#endif
}

static void sampleTick() {
    uint32_t now = millis();

    // Hash-counter snapshot every 10 s
    if (s_lastHrSample == 0 || now - s_lastHrSample >= HR_SLOT_MS) {
        uint64_t h = readHashes();

        // EMA of the 10 s instantaneous rate
        if (s_hrCount > 0) {
            uint16_t prev = (s_hrHead + HR_SLOTS - 1) % HR_SLOTS;
            uint32_t dt = now - s_hrRing[prev].ms;
            if (dt > 0) {
                double inst = (double)(h - s_hrRing[prev].hashes) * 1000.0 / (double)dt;
                if (!s_emaInit) { s_hashRateEma = inst; s_emaInit = true; }
                else s_hashRateEma = 0.3 * inst + 0.7 * s_hashRateEma;
            }
        }

        portENTER_CRITICAL(&s_mux);
        s_hrRing[s_hrHead].hashes = h;
        s_hrRing[s_hrHead].ms = now;
        s_hrHead = (s_hrHead + 1) % HR_SLOTS;
        if (s_hrCount < HR_SLOTS) s_hrCount++;
        portEXIT_CRITICAL(&s_mux);
        s_lastHrSample = now;
    }

    // Statistics history sample
    if (s_lastStatSample == 0 || now - s_lastStatSample >= WEB_API_STATS_INTERVAL_MS) {
        stat_sample_t s;
        s.hashrate    = (float)(s_hashRateEma / 1e9);
        s.hashrate1m  = (float)(hashRateOver(60000) / 1e9);
        s.hashrate10m = (float)(hashRateOver(600000) / 1e9);
        s.hashrate1h  = (float)(hashRateOver(3600000) / 1e9);
        s.asicTemp    = readTemp();
        s.wifiRssi    = (WiFi.status() == WL_CONNECTED) ? (int8_t)WiFi.RSSI() : 0;
        s.freeHeap    = ESP.getFreeHeap();
        s.responseTime = (float)miner_get_stats()->avgLatency;
        s.timestamp   = now;

        portENTER_CRITICAL(&s_mux);
        s_stats[s_statHead] = s;
        s_statHead = (s_statHead + 1) % WEB_API_STATS_SAMPLES;
        if (s_statCount < WEB_API_STATS_SAMPLES) s_statCount++;
        portEXIT_CRITICAL(&s_mux);
        s_lastStatSample = now;
    }
}

// Mask wallet unless exposure is explicitly enabled: bc1qxy...wlh
static void maskWallet(const char *in, char *out, size_t outLen) {
    size_t n = strlen(in);
    if (n == 0) { out[0] = '\0'; return; }
    if (nvs_api_expose_wallet()) {
        strncpy(out, in, outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }
    // Keep a ".worker" suffix visible, mask the address part
    const char *dot = strchr(in, '.');
    size_t addrLen = dot ? (size_t)(dot - in) : n;
    if (addrLen <= 10) {
        snprintf(out, outLen, "***%s", dot ? dot : "");
    } else {
        snprintf(out, outLen, "%.6s...%.4s%s", in, in + addrLen - 4, dot ? dot : "");
    }
}

static void jsonEscapeAppend(String &s, const char *v) {
    s += '"';
    for (const char *p = v ? v : ""; *p; p++) {
        char c = *p;
        if (c == '"' || c == '\\') { s += '\\'; s += c; }
        else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); s += b; }
        else s += c;
    }
    s += '"';
}

// Tiny JSON writer helpers (avoid a large JsonDocument on a small stack)
static void kStr(String &s, const char *k, const char *v) {
    s += '"'; s += k; s += "\":"; jsonEscapeAppend(s, v); s += ',';
}
static void kNum(String &s, const char *k, double v, int decimals = 0) {
    char b[40];
    if (isnan(v) || isinf(v)) v = 0;
    if (decimals == 0) snprintf(b, sizeof(b), "\"%s\":%.0f,", k, v);
    else snprintf(b, sizeof(b), "\"%s\":%.*f,", k, decimals, v);
    s += b;
}
static void kU64(String &s, const char *k, uint64_t v) {
    char b[48];
    snprintf(b, sizeof(b), "\"%s\":%llu,", k, (unsigned long long)v);
    s += b;
}
static void kBool(String &s, const char *k, bool v) {
    s += '"'; s += k; s += "\":"; s += v ? "true" : "false"; s += ',';
}
static void closeObj(String &s) {
    if (s.length() && s[s.length() - 1] == ',') s.setCharAt(s.length() - 1, '}');
    else s += '}';
}

static void sendCors() {
    s_server->sendHeader("Access-Control-Allow-Origin", "*");
    s_server->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    s_server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    s_server->sendHeader("Cache-Control", "no-store");
}

// Collect everything both JSON endpoints need in one place
typedef struct {
    double hashRateHs, hr1m, hr10m, hr1h;
    double bestDiffEver, bestSessionDiff, poolDiff;
    uint32_t accepted, rejected, lifetimeAccepted, lifetimeRejected;
    uint32_t blocks, lifetimeBlocks, templates, matches32;
    uint64_t sessionHashes, lifetimeHashes;
    uint32_t uptime, totalUptime, avgLatency;
    uint32_t submitted;
    bool poolConnected, usingBackup;
} snap_t;

static void takeSnapshot(snap_t *o) {
    mining_stats_t *m = miner_get_stats();
    mining_persistence_t *p = nvs_stats_get();

    o->hashRateHs = s_hashRateEma;
    o->hr1m  = hashRateOver(60000);
    o->hr10m = hashRateOver(600000);
    o->hr1h  = hashRateOver(3600000);

    o->bestSessionDiff = m->bestDifficulty;
    o->bestDiffEver = (m->bestDifficulty > p->bestDifficultyEver) ? m->bestDifficulty : p->bestDifficultyEver;
    o->poolDiff = miner_get_difficulty();

    o->submitted = m->shares;
    o->accepted = m->accepted;
    o->rejected = m->rejected;
    o->lifetimeAccepted = p->lifetimeAccepted + m->accepted;
    o->lifetimeRejected = p->lifetimeRejected + m->rejected;
    o->blocks = m->blocks;
    o->lifetimeBlocks = p->lifetimeBlocks + m->blocks;
    o->templates = m->templates;
    o->matches32 = m->matches32;
    o->sessionHashes = readHashes();
    o->lifetimeHashes = p->lifetimeHashes + o->sessionHashes;
    o->uptime = millis() / 1000;
    o->totalUptime = p->totalUptimeSeconds + o->uptime;
    o->avgLatency = m->avgLatency;
    o->poolConnected = stratum_is_connected();
    o->usingBackup = stratum_is_backup();
}

// ============================================================
// Handlers
// ============================================================

// GET /api/system/info - AxeOS / ESP-Miner compatible
static void handleSystemInfo() {
    snap_t sn;
    takeSnapshot(&sn);
    miner_config_t *cfg = nvs_config_get();
    live_stats_t ls;
    live_stats_get_copy(&ls);

    char user[160], fbUser[160], full[200];
    if (cfg->workerName[0]) snprintf(full, sizeof(full), "%s.%s", cfg->wallet, cfg->workerName);
    else snprintf(full, sizeof(full), "%s", cfg->wallet);
    maskWallet(full, user, sizeof(user));
    const char *bw = cfg->backupWallet[0] ? cfg->backupWallet : cfg->wallet;
    if (cfg->workerName[0]) snprintf(full, sizeof(full), "%s.%s", bw, cfg->workerName);
    else snprintf(full, sizeof(full), "%s", bw);
    maskWallet(full, fbUser, sizeof(fbUser));

    char mac[18];
    uint8_t m[6];
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    char fullHost[48];
    snprintf(fullHost, sizeof(fullHost), "%s.local", s_hostname);

    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    String s;
    s.reserve(2300);
    s = "{";

    // --- Power / thermal (no power monitor on ESP32 boards -> 0) ---
    kNum(s, "power", 0);
    kNum(s, "voltage", 0);
    kNum(s, "current", 0);
    kNum(s, "temp", readTemp(), 1);
    kNum(s, "vrTemp", 0);
    kNum(s, "maxPower", 0);
    kNum(s, "nominalVoltage", 0);
    kNum(s, "coreVoltage", 0);
    kNum(s, "coreVoltageActual", 0);
    kNum(s, "frequency", getCpuFrequencyMhz());
    kNum(s, "actualFrequency", getCpuFrequencyMhz());
    kNum(s, "expectedHashrate", 0);
    kNum(s, "fanspeed", 0);
    kNum(s, "fanrpm", 0);
    kNum(s, "overheat_mode", 0);

    // --- Mining (AxeOS units: hashRate in GH/s) ---
    kNum(s, "hashRate", sn.hashRateHs / 1e9, 9);
    kNum(s, "hashRate_1m", sn.hr1m / 1e9, 9);
    kNum(s, "hashRate_10m", sn.hr10m / 1e9, 9);
    kNum(s, "hashRate_1h", sn.hr1h / 1e9, 9);
    double errPct = (sn.accepted + sn.rejected) ? (100.0 * sn.rejected / (sn.accepted + sn.rejected)) : 0.0;
    kNum(s, "errorPercentage", errPct, 2);
    kNum(s, "sharesAccepted", sn.accepted);
    kNum(s, "sharesRejected", sn.rejected);
    kNum(s, "bestDiff", sn.bestDiffEver, 4);
    kNum(s, "bestSessionDiff", sn.bestSessionDiff, 4);
    kNum(s, "poolDifficulty", sn.poolDiff, 6);
    kNum(s, "responseTime", sn.avgLatency);
    kNum(s, "workReceived", sn.templates);
    kNum(s, "blockFound", sn.blocks > 0 ? 1 : 0);
    kNum(s, "blockHeight", ls.blockValid ? ls.blockHeight : 0);
    kNum(s, "networkDifficulty", ls.networkValid ? ls.difficultyRaw : 0);
    kNum(s, "isUsingFallbackStratum", sn.usingBackup ? 1 : 0);

    // --- System ---
    kNum(s, "freeHeap", ESP.getFreeHeap());
    kNum(s, "minFreeHeap", ESP.getMinFreeHeap());
    kNum(s, "maxAllocHeap", ESP.getMaxAllocHeap());
    kNum(s, "isPSRAMAvailable", psramFound() ? 1 : 0);
    kNum(s, "uptimeSeconds", sn.uptime);
    kNum(s, "totalUptimeSeconds", sn.totalUptime);
    kU64(s, "totalHashes", sn.lifetimeHashes);
    kNum(s, "smallCoreCount", ESP.getChipCores());
    kStr(s, "ASICModel", ESP.getChipModel());
    kStr(s, "deviceModel", "SparkMiner");
    kStr(s, "boardVersion", BOARD_NAME);
    kStr(s, "version", AUTO_VERSION);
    kStr(s, "axeOSVersion", AUTO_VERSION);
    kStr(s, "idfVersion", esp_get_idf_version());
    kStr(s, "runningPartition", "app0");
    kStr(s, "hostname", s_hostname);
    kStr(s, "fullHostname", fullHost);
    kStr(s, "mdnsHostname", s_hostname);
    kStr(s, "macAddr", mac);
    kStr(s, "ssid", wifiOk ? WiFi.SSID().c_str() : "");
    kStr(s, "ipv4", wifiOk ? WiFi.localIP().toString().c_str() : "");
    kStr(s, "wifiStatus", wifiOk ? "Connected!" : "Disconnected");
    kNum(s, "wifiRSSI", wifiOk ? WiFi.RSSI() : 0);
    kNum(s, "apEnabled", 0);

    // --- Pool ---
    kStr(s, "stratumURL", cfg->poolUrl);
    kNum(s, "stratumPort", cfg->poolPort);
    kStr(s, "stratumUser", user);
    kNum(s, "stratumSuggestedDifficulty", cfg->targetDifficulty, 6);
    kStr(s, "fallbackStratumURL", cfg->backupPoolUrl);
    kNum(s, "fallbackStratumPort", cfg->backupPoolPort);
    kStr(s, "fallbackStratumUser", fbUser);
    kStr(s, "poolConnectionInfo", sn.poolConnected ? "Connected" : "Disconnected");

    // --- Display ---
    kStr(s, "display", BOARD_NAME);
    kNum(s, "rotation", cfg->rotation);
    kNum(s, "invertscreen", cfg->invertColors ? 1 : 0);
    kNum(s, "displayTimeout", cfg->screenTimeout);

    // --- SparkMiner extensions (native units, not part of AxeOS) ---
    kNum(s, "hashRateHs", sn.hashRateHs, 1);
    kNum(s, "hashRateKHs", sn.hashRateHs / 1e3, 2);
    kNum(s, "hashRate_1mKHs", sn.hr1m / 1e3, 2);
    kNum(s, "hashRate_10mKHs", sn.hr10m / 1e3, 2);
    kNum(s, "hashRate_1hKHs", sn.hr1h / 1e3, 2);
    kNum(s, "totalSharesAccepted", sn.lifetimeAccepted);
    kNum(s, "totalSharesRejected", sn.lifetimeRejected);
    kNum(s, "totalBlocksFound", sn.lifetimeBlocks);
    kNum(s, "matches32bit", sn.matches32);
    kBool(s, "poolConnected", sn.poolConnected);
    closeObj(s);

    sendCors();
    s_server->send(200, "application/json", s);
}

// GET /api/sparkminer - compact native stats, friendly for HA REST sensors
static void handleNative() {
    snap_t sn;
    takeSnapshot(&sn);
    live_stats_t ls;
    live_stats_get_copy(&ls);

    String s;
    s.reserve(900);
    s = "{";
    kNum(s, "hashrate", sn.hashRateHs, 1);
    kNum(s, "hashrate_1m", sn.hr1m, 1);
    kNum(s, "hashrate_10m", sn.hr10m, 1);
    kNum(s, "hashrate_1h", sn.hr1h, 1);
    kNum(s, "shares_submitted", sn.submitted);
    kNum(s, "shares_accepted", sn.accepted);
    kNum(s, "shares_rejected", sn.rejected);
    kNum(s, "total_shares_accepted", sn.lifetimeAccepted);
    kNum(s, "total_shares_rejected", sn.lifetimeRejected);
    kNum(s, "best_diff", sn.bestDiffEver, 6);
    kNum(s, "best_session_diff", sn.bestSessionDiff, 6);
    kNum(s, "pool_diff", sn.poolDiff, 6);
    kNum(s, "blocks_found", sn.lifetimeBlocks);
    kNum(s, "matches_32bit", sn.matches32);
    kNum(s, "templates", sn.templates);
    kU64(s, "session_hashes", sn.sessionHashes);
    kU64(s, "total_hashes", sn.lifetimeHashes);
    kNum(s, "uptime", sn.uptime);
    kNum(s, "total_uptime", sn.totalUptime);
    kNum(s, "pool_latency_ms", sn.avgLatency);
    kBool(s, "pool_connected", sn.poolConnected);
    kBool(s, "pool_backup", sn.usingBackup);
    kStr(s, "pool", stratum_get_pool());
    kNum(s, "temp", readTemp(), 1);
    kNum(s, "rssi", WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    kNum(s, "free_heap", ESP.getFreeHeap());
    kNum(s, "cpu_mhz", getCpuFrequencyMhz());
    kNum(s, "block_height", ls.blockValid ? ls.blockHeight : 0);
    kStr(s, "version", AUTO_VERSION);
    kStr(s, "board", BOARD_NAME);
    closeObj(s);

    sendCors();
    s_server->send(200, "application/json", s);
}

// GET /api/system/statistics[?columns=a,b,c] - AxeOS compatible, streamed
enum { C_HR = 0, C_HR1M, C_HR10M, C_HR1H, C_TEMP, C_RSSI, C_HEAP, C_RESP, C_NUM };
static const char *kColNames[C_NUM] = {
    "hashrate", "hashrate_1m", "hashrate_10m", "hashrate_1h",
    "asicTemp", "wifiRssi", "freeHeap", "responseTime"
};

static void handleStatistics() {
    bool sel[C_NUM];
    bool any = false;
    for (int i = 0; i < C_NUM; i++) sel[i] = false;

    if (s_server->hasArg("columns")) {
        String cols = s_server->arg("columns");
        int start = 0;
        while (start <= (int)cols.length()) {
            int comma = cols.indexOf(',', start);
            if (comma < 0) comma = cols.length();
            String c = cols.substring(start, comma);
            c.trim();
            for (int i = 0; i < C_NUM; i++) {
                if (c == kColNames[i]) { sel[i] = true; any = true; }
            }
            start = comma + 1;
        }
    }
    if (!any) for (int i = 0; i < C_NUM; i++) sel[i] = true;

    // Copy ring buffer under lock (~3 KB on heap)
    uint16_t count;
    stat_sample_t *copy = (stat_sample_t *)malloc(sizeof(stat_sample_t) * WEB_API_STATS_SAMPLES);
    if (!copy) {
        s_server->send(503, "application/json", "{\"error\":\"out of memory\"}");
        return;
    }
    portENTER_CRITICAL(&s_mux);
    count = s_statCount;
    uint16_t first = (s_statHead + WEB_API_STATS_SAMPLES - count) % WEB_API_STATS_SAMPLES;
    for (uint16_t i = 0; i < count; i++) {
        copy[i] = s_stats[(first + i) % WEB_API_STATS_SAMPLES];
    }
    portEXIT_CRITICAL(&s_mux);

    sendCors();
    s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server->send(200, "application/json", "");

    char buf[160];
    snprintf(buf, sizeof(buf), "{\"currentTimestamp\":%lu,\"labels\":[", (unsigned long)millis());
    String out = buf;
    for (int i = 0; i < C_NUM; i++) {
        if (!sel[i]) continue;
        out += '"'; out += kColNames[i]; out += "\",";
    }
    out += "\"timestamp\"],\"statistics\":[";
    s_server->sendContent(out);

    for (uint16_t i = 0; i < count; i++) {
        const stat_sample_t &r = copy[i];
        int n = snprintf(buf, sizeof(buf), "%s[", i ? "," : "");
        if (sel[C_HR])    n += snprintf(buf + n, sizeof(buf) - n, "%.9f,", r.hashrate);
        if (sel[C_HR1M])  n += snprintf(buf + n, sizeof(buf) - n, "%.9f,", r.hashrate1m);
        if (sel[C_HR10M]) n += snprintf(buf + n, sizeof(buf) - n, "%.9f,", r.hashrate10m);
        if (sel[C_HR1H])  n += snprintf(buf + n, sizeof(buf) - n, "%.9f,", r.hashrate1h);
        if (sel[C_TEMP])  n += snprintf(buf + n, sizeof(buf) - n, "%.1f,", r.asicTemp);
        if (sel[C_RSSI])  n += snprintf(buf + n, sizeof(buf) - n, "%d,", r.wifiRssi);
        if (sel[C_HEAP])  n += snprintf(buf + n, sizeof(buf) - n, "%lu,", (unsigned long)r.freeHeap);
        if (sel[C_RESP])  n += snprintf(buf + n, sizeof(buf) - n, "%.0f,", r.responseTime);
        snprintf(buf + n, sizeof(buf) - n, "%lu]", (unsigned long)r.timestamp);
        s_server->sendContent(buf);
    }
    s_server->sendContent("]}");
    s_server->sendContent("");  // Terminate chunked response
    free(copy);
}

// GET /api/log - recent pool/stratum events, oldest first, streamed
static void handleLog() {
    sendCors();
    s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server->send(200, "text/plain; charset=utf-8", "");

    char line[WEB_API_LOG_LINE_LEN];
    char out[WEB_API_LOG_LINE_LEN + 24];

    portENTER_CRITICAL(&s_logMux);
    uint16_t count = s_logCount;
    uint16_t first = (s_logHead + WEB_API_LOG_LINES - count) % WEB_API_LOG_LINES;
    uint32_t total = s_logTotal;
    portEXIT_CRITICAL(&s_logMux);

    snprintf(out, sizeof(out), "# uptime %lu s, %lu events logged, showing last %u\n",
             (unsigned long)(millis() / 1000), (unsigned long)total, (unsigned)count);
    s_server->sendContent(out);

    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (first + i) % WEB_API_LOG_LINES;
        portENTER_CRITICAL(&s_logMux);
        memcpy(line, s_log[idx], sizeof(line));
        uint32_t ms = s_logMs[idx];
        portEXIT_CRITICAL(&s_logMux);
        line[sizeof(line) - 1] = '\0';
        snprintf(out, sizeof(out), "[%7lu.%lus] %s\n",
                 (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100), line);
        s_server->sendContent(out);
    }
    s_server->sendContent("");  // Terminate chunked response
}

#include "web_settings.h"

static void handleRestart() {
    sendCors();
    s_server->send(200, "application/json", "{\"message\":\"System will restart shortly.\"}");
    delay(500);
    ESP.restart();
}

static void handleOptions() {
    sendCors();
    s_server->send(204);
}

static void handleRoot() {
    snap_t sn;
    takeSnapshot(&sn);
    const miner_config_t *cfg = nvs_config_get();
    const char *poolName = sn.poolConnected ? stratum_get_pool() : cfg->poolUrl;
    char buf[1700];
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='10'><title>SparkMiner</title>"
        "<style>body{background:#000;color:#fff;font-family:Helvetica,Arial,sans-serif;max-width:480px;margin:20px auto;padding:0 12px}"
        "h1{color:#ff6800}td{padding:4px 12px 4px 0}td:first-child{color:#aaa}a{color:#ffd700}</style></head><body>"
        "<h1>SparkMiner</h1><table>"
        "<tr><td>Hashrate</td><td>%.2f kH/s</td></tr>"
        "<tr><td>Hashrate 1h</td><td>%.2f kH/s</td></tr>"
        "<tr><td>Shares</td><td>%lu accepted / %lu rejected</td></tr>"
        "<tr><td>Best diff</td><td>%.4f (session %.4f)</td></tr>"
        "<tr><td>Pool</td><td>%s (%s)</td></tr>"
        "<tr><td>Wallet</td><td>%s</td></tr>"
        "<tr><td>Uptime</td><td>%lu s</td></tr>"
        "<tr><td>Version</td><td>%s</td></tr></table>"
        "<p><a href='/settings'>Settings</a></p>"
        "<p><a href='/api/system/info'>/api/system/info</a><br>"
        "<a href='/api/system/statistics'>/api/system/statistics</a><br>"
        "<a href='/api/sparkminer'>/api/sparkminer</a><br>"
        "<a href='/api/log'>/api/log</a></p></body></html>",
        sn.hashRateHs / 1e3, sn.hr1h / 1e3,
        (unsigned long)sn.accepted, (unsigned long)sn.rejected,
        sn.bestDiffEver, sn.bestSessionDiff,
        poolName, sn.poolConnected ? "connected" : "disconnected",
        cfg->wallet[0] ? "configured" : "NOT SET - mining is off",
        (unsigned long)sn.uptime, AUTO_VERSION);
    s_server->send(200, "text/html", buf);
}

static void handleNotFound() {
    if (s_server->method() == HTTP_OPTIONS) { handleOptions(); return; }
    sendCors();
    s_server->send(404, "application/json", "{\"error\":\"unknown route\"}");
}

// ============================================================
// mDNS
// ============================================================

static void startMdns() {
    if (s_mdnsStarted) {
        MDNS.end();
        s_mdnsStarted = false;
    }
    if (!MDNS.begin(s_hostname)) {
        Serial.println("[API] mDNS start failed");
        return;
    }
    MDNS.addService("http", "tcp", WEB_API_PORT);
    MDNS.addServiceTxt("http", "tcp", "device", "sparkminer");
    MDNS.addServiceTxt("http", "tcp", "api", "axeos");
    s_mdnsStarted = true;
    Serial.printf("[API] mDNS: http://%s.local/\n", s_hostname);
}

// ============================================================
// Task
// ============================================================

static void web_api_task(void *param) {
    Serial.printf("[API] Task started on core %d\n", xPortGetCoreID());

    s_server = new WebServer(WEB_API_PORT);
    static const char *kHeaders[] = {"Origin"};
    s_server->collectHeaders(kHeaders, 1);
    s_server->on("/settings", HTTP_GET, handleSettingsGet);
    s_server->on("/settings", HTTP_POST, handleSettingsPost);
    s_server->on("/settings/reset", HTTP_POST, handleSettingsReset);
    s_server->on("/", HTTP_GET, handleRoot);
    s_server->on("/api/system/info", HTTP_GET, handleSystemInfo);
    s_server->on("/api/system/statistics", HTTP_GET, handleStatistics);
    s_server->on("/api/system/statistics/dashboard", HTTP_GET, handleStatistics);
    s_server->on("/api/sparkminer", HTTP_GET, handleNative);
    s_server->on("/api/log", HTTP_GET, handleLog);
    s_server->on("/api/system/restart", HTTP_POST, handleRestart);
    s_server->onNotFound(handleNotFound);
    s_server->begin();
    Serial.printf("[API] HTTP server listening on port %d\n", WEB_API_PORT);

    bool wasConnected = false;
    for (;;) {
        sampleTick();
        settingsTick();

        bool connected = (WiFi.status() == WL_CONNECTED);
        if (connected) {
            IPAddress ip = WiFi.localIP();
            // (Re)announce mDNS after (re)connect or IP change
            if (!wasConnected || ip != s_lastIp) {
                s_lastIp = ip;
                startMdns();
                Serial.printf("[API] Stats API: http://%s/api/system/info\n", ip.toString().c_str());
            }
            s_server->handleClient();
        }
        wasConnected = connected;

        // 20 ms poll: responsive enough for HTTP, negligible CPU on core 0
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================================
// Public API
// ============================================================

void web_api_start() {
    buildHostname();
    if (s_running) return;
    if (!nvs_api_enabled()) {
        Serial.println("[API] Local stats API disabled (enable in config portal)");
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        web_api_task, "WebAPI", WEB_API_STACK, NULL,
        WEB_API_PRIORITY, &s_task, WEB_API_CORE);
    if (ok == pdPASS) {
        s_running = true;
        Serial.println("[API] Local stats API enabled");
    } else {
        Serial.println("[API] ERROR: Failed to create WebAPI task");
    }
}

bool web_api_is_running() {
    return s_running;
}

void web_api_log(const char *fmt, ...) {
    if (!s_running) return;

    char line[WEB_API_LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    uint32_t now = millis();
    portENTER_CRITICAL(&s_logMux);
    memcpy(s_log[s_logHead], line, sizeof(line));
    s_logMs[s_logHead] = now;
    s_logHead = (s_logHead + 1) % WEB_API_LOG_LINES;
    if (s_logCount < WEB_API_LOG_LINES) s_logCount++;
    s_logTotal++;
    portEXIT_CRITICAL(&s_logMux);
}

const char* web_api_get_hostname() {
    buildHostname();
    return s_hostname;
}

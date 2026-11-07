/*
 * SparkMiner - Local HTTP Stats API (Issue #12)
 *
 * Optional, opt-in local web server exposing AxeOS / ESP-Miner compatible
 * endpoints so HashWatcher, Home Assistant and other BitAxe tools can poll
 * the miner over the LAN.
 *
 *   GET  /                          Minimal status page
 *   GET  /api/system/info           AxeOS-compatible system info (JSON)
 *   GET  /api/system/statistics     AxeOS-compatible history (JSON, ?columns=)
 *   GET  /api/sparkminer            SparkMiner-native stats (H/s units, JSON)
 *   GET  /api/log                   Recent pool/stratum events (plain text)
 *   GET  /settings                  Settings page (Basic auth, user "admin")
 *   POST /settings                  Save settings and restart
 *   POST /settings/reset            Factory reset
 *   POST /api/system/restart        Restart the miner
 *
 * Uses the synchronous WebServer + ESPmDNS that ship with the Arduino core
 * (already pulled in by WiFiManager) - no new library dependencies.
 * Disabled by default: when apiEnabled is false no task is created and no
 * socket is opened, so there is zero hashrate cost.
 *
 * GPL v3 License
 */

#ifndef WEB_API_H
#define WEB_API_H

#include <Arduino.h>

#ifndef WEB_API_PORT
    #define WEB_API_PORT        80
#endif
#ifndef WEB_API_CORE
    #define WEB_API_CORE        0
#endif
#ifndef WEB_API_PRIORITY
    #define WEB_API_PRIORITY    1       // Same as monitor task, far below Miner1 (19)
#endif
#ifndef WEB_API_STACK
    #define WEB_API_STACK       8192
#endif

// Statistics history: one sample every WEB_API_STATS_INTERVAL_MS,
// ring buffer of WEB_API_STATS_SAMPLES entries (default 120 x 30 s = 1 h)
#ifndef WEB_API_STATS_INTERVAL_MS
    #define WEB_API_STATS_INTERVAL_MS   30000
#endif
#ifndef WEB_API_STATS_SAMPLES
    #define WEB_API_STATS_SAMPLES       120
#endif

// Event log ring buffer (GET /api/log)
#ifndef WEB_API_LOG_LINES
    #define WEB_API_LOG_LINES           64
#endif
#define WEB_API_LOG_LINE_LEN            120

/**
 * Start the web API task if apiEnabled is set in NVS.
 * Safe to call when disabled (no-op).
 * Must be called after WiFi is connected (after wifi_manager_start()).
 */
void web_api_start();

/**
 * True if the web API task is running.
 */
bool web_api_is_running();

/**
 * mDNS hostname (without .local), e.g. "sparkminer-a1b2".
 */
const char* web_api_get_hostname();

/**
 * Append one line to the in-RAM event log served at GET /api/log.
 * Records pool/stratum events so a miner can be diagnosed from a browser
 * when no serial console is attached. No-op while the API is disabled.
 * Never pass wallet addresses or passwords.
 */
void web_api_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif // WEB_API_H

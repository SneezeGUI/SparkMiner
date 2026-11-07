/*
 * SparkMiner - Web settings page
 *
 * GET  /settings          Edit WiFi, pool, wallet, display and stats settings
 * POST /settings          Validate, save to NVS, restart
 * POST /settings/reset    Factory reset (wipes configuration, opens setup portal)
 *
 * Protected with HTTP Basic auth (user "admin"). The password defaults to the
 * setup hotspot password and can be changed on the page. Only reachable while
 * the local API is enabled.
 *
 * Included by web_api.cpp (uses its WebServer instance s_server).
 *
 * GPL v3 License
 */

#ifndef WEB_SETTINGS_H
#define WEB_SETTINGS_H

#define WEB_SETTINGS_USER       "admin"
#define WEB_PASSWORD_MIN_LEN    8

static uint32_t s_restartAtMs = 0;      // 0 = no restart pending
static bool s_wipeOnRestart = false;

// ============================================================
// Helpers
// ============================================================

static bool settingsAuth() {
    char pass[40];
    nvs_web_password_get(pass, sizeof(pass));
    if (s_server->authenticate(WEB_SETTINGS_USER, pass)) return true;
    s_server->requestAuthentication(BASIC_AUTH, "SparkMiner", "Login required (user: admin)");
    return false;
}

// Reject cross-site form posts: a browser always sends Origin on those.
static bool settingsSameOrigin() {
    if (!s_server->hasHeader("Origin")) return true;
    String origin = s_server->header("Origin");
    int p = origin.indexOf("://");
    if (p < 0) return false;
    return origin.substring(p + 3) == s_server->hostHeader();
}

static void htmlEsc(String &s, const char *v) {
    for (const char *p = v ? v : ""; *p; p++) {
        switch (*p) {
            case '&':  s += "&amp;";  break;
            case '<':  s += "&lt;";   break;
            case '>':  s += "&gt;";   break;
            case '"':  s += "&quot;"; break;
            case '\'': s += "&#39;";  break;
            default:   s += *p;       break;
        }
    }
}

static void fInput(String &s, const char *label, const char *name, const char *value,
                   int maxLen, const char *type = "text", const char *hint = "") {
    s += "<label>";
    s += label;
    s += "<input type='";
    s += type;
    s += "' name='";
    s += name;
    s += "' maxlength='";
    s += String(maxLen);
    s += "' value='";
    htmlEsc(s, value);
    s += "' placeholder='";
    htmlEsc(s, hint);
    s += "' autocomplete='off' autocapitalize='off' spellcheck='false'></label>";
}

static void fSelectOpen(String &s, const char *label, const char *name) {
    s += "<label>";
    s += label;
    s += "<select name='";
    s += name;
    s += "'>";
}

static void fOption(String &s, int value, const char *text, bool selected) {
    s += "<option value='";
    s += String(value);
    s += selected ? "' selected>" : "'>";
    s += text;
    s += "</option>";
}

static void fSelectClose(String &s) {
    s += "</select></label>";
}

static const char SETTINGS_HEAD[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>SparkMiner Settings</title><style>"
    "body{background:#000;color:#fff;font-family:Helvetica,Arial,sans-serif;max-width:480px;margin:20px auto;padding:0 12px}"
    "h1{color:#ff6800}h3{color:#ffd700;margin:22px 0 6px}a{color:#ffd700}"
    "label{display:block;margin:8px 0;color:#aaa;font-size:14px}"
    "input,select{display:block;width:100%;box-sizing:border-box;margin-top:3px;padding:8px;border-radius:4px;"
    "background:#181818;color:#fff;border:1px solid #525252;font-size:15px}"
    "button{background:#ff6800;color:#000;border:none;font-weight:bold;cursor:pointer;margin-top:18px;padding:11px;"
    "width:100%;border-radius:4px;font-size:15px}"
    "button.sec{background:#333;color:#fff}button.danger{background:#7a1010;color:#fff}"
    ".note{color:#aaa;font-size:13px}.err{color:#ff5050}"
    "</style></head><body>";

static void settingsMessage(int code, const char *title, const char *text, bool refreshHome) {
    String s;
    s.reserve(900);
    s = SETTINGS_HEAD;
    if (refreshHome) s += "<meta http-equiv='refresh' content='20;url=/'>";
    s += "<h1>";
    s += title;
    s += "</h1><p>";
    htmlEsc(s, text);
    s += "</p><p><a href='/'>Status</a> &middot; <a href='/settings'>Settings</a></p></body></html>";
    s_server->send(code, "text/html", s);
}

// Copy a trimmed form field into a fixed buffer; false if it does not fit.
// A field that was not submitted leaves the buffer untouched.
static bool argTo(const char *name, char *dest, size_t destSize) {
    if (!s_server->hasArg(name)) return true;
    String v = s_server->arg(name);
    v.trim();
    if (v.length() >= destSize) return false;
    strncpy(dest, v.c_str(), destSize - 1);
    dest[destSize - 1] = '\0';
    return true;
}

static bool argPort(const char *name, uint16_t *out) {
    if (!s_server->hasArg(name)) return true;
    String v = s_server->arg(name);
    v.trim();
    long p = v.toInt();
    if (p < 1 || p > 65535) return false;
    *out = (uint16_t)p;
    return true;
}

// ============================================================
// GET /settings
// ============================================================

static void handleSettingsGet() {
    if (!settingsAuth()) return;
    const miner_config_t *c = nvs_config_get();
    char num[12];

    s_server->sendHeader("Cache-Control", "no-store");
    s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server->send(200, "text/html", "");

    String s;
    s.reserve(2600);

    s = SETTINGS_HEAD;
    s += "<h1>SparkMiner Settings</h1><p class='note'><a href='/'>&larr; Status</a> &middot; ";
    htmlEsc(s, s_hostname);
    s += " &middot; " AUTO_VERSION "</p><form method='POST' action='/settings'>";

    s += "<h3>WiFi</h3>";
    fInput(s, "Network name (SSID)", "ssid", c->ssid, 32);
    fInput(s, "WiFi password", "wifi_pass", "", 63, "password", "leave empty to keep current");
    s += "<p class='note'>If the new network cannot be joined, the miner returns to the current one.</p>";
    s_server->sendContent(s);

    s = "<h3>Mining</h3>";
    fInput(s, "BTC wallet address", "wallet", c->wallet, MAX_WALLET_LEN);
    fInput(s, "Worker name", "worker", c->workerName, 31);
    s += "<h3>Primary pool</h3>";
    fInput(s, "Pool URL", "pool_url", c->poolUrl, MAX_POOL_URL_LEN);
    snprintf(num, sizeof(num), "%u", (unsigned)c->poolPort);
    fInput(s, "Pool port", "pool_port", num, 5, "number");
    fInput(s, "Pool password", "pool_pass", c->poolPassword, MAX_PASSWORD_LEN);
    s += "<h3>Backup pool</h3>";
    fInput(s, "Backup pool URL", "bk_url", c->backupPoolUrl, MAX_POOL_URL_LEN);
    snprintf(num, sizeof(num), "%u", (unsigned)c->backupPoolPort);
    fInput(s, "Backup pool port", "bk_port", num, 5, "number");
    fInput(s, "Backup wallet (optional)", "bk_wallet", c->backupWallet, MAX_WALLET_LEN);
    fInput(s, "Backup pool password", "bk_pass", c->backupPoolPassword, MAX_PASSWORD_LEN);
    s_server->sendContent(s);

    s = "<h3>Display</h3>";
    fSelectOpen(s, "Brightness", "bright");
    static const int kBright[] = {10, 25, 50, 75, 100};
    for (int i = 0; i < 5; i++) {
        snprintf(num, sizeof(num), "%d%%", kBright[i]);
        fOption(s, kBright[i], num, c->brightness == kBright[i]);
    }
    fSelectClose(s);
    fSelectOpen(s, "Screen timeout", "scrn_to");
    static const int kTimeout[] = {0, 30, 60, 120, 300};
    static const char *kTimeoutLabel[] = {"Never", "30 seconds", "1 minute", "2 minutes", "5 minutes"};
    for (int i = 0; i < 5; i++) fOption(s, kTimeout[i], kTimeoutLabel[i], c->screenTimeout == kTimeout[i]);
    fSelectClose(s);
    fSelectOpen(s, "Screen rotation", "rotation");
    static const char *kRotLabel[] = {"0 - Portrait", "1 - Landscape", "2 - Portrait (flipped)", "3 - Landscape (flipped)"};
    for (int i = 0; i < 4; i++) fOption(s, i, kRotLabel[i], c->rotation == i);
    fSelectClose(s);
    fSelectOpen(s, "Color theme", "invert");
    fOption(s, 0, "Dark", !c->invertColors);
    fOption(s, 1, "Light", c->invertColors);
    fSelectClose(s);
    s_server->sendContent(s);

    s = "";
    fSelectOpen(s, "Timezone", "tz");
    for (int i = -12; i <= 14; i++) {
        snprintf(num, sizeof(num), "UTC%+d", i);
        fOption(s, i, num, c->timezoneOffset == i);
    }
    fSelectClose(s);
    s_server->sendContent(s);

    s = "<h3>Live stats (price, block height)</h3>";
    fSelectOpen(s, "Live stats", "stats_en");
    fOption(s, 1, "Enabled", c->statsEnabled);
    fOption(s, 0, "Disabled", !c->statsEnabled);
    fSelectClose(s);
    fSelectOpen(s, "Direct HTTPS", "https_stats");
    fOption(s, 0, "Disabled", !c->enableHttpsStats);
    fOption(s, 1, "Enabled (needed without proxy)", c->enableHttpsStats);
    fSelectClose(s);
    fInput(s, "Custom stats API URL (optional)", "stats_api", c->statsApiUrl, 127);
    fInput(s, "HTTP proxy (optional)", "stats_proxy", c->statsProxyUrl, 127);

    s += "<h3>Access</h3>";
    fSelectOpen(s, "Wallet in API responses", "api_wallet");
    fOption(s, 0, "Masked", !nvs_api_expose_wallet());
    fOption(s, 1, "Full address", nvs_api_expose_wallet());
    fSelectClose(s);
    fInput(s, "New settings password", "web_pass", "", 32, "password", "leave empty to keep current");
    s += "<button type='submit'>Save and restart</button></form>";
    s += "<form method='POST' action='/api/system/restart'><button class='sec' type='submit'>Restart only</button></form>";
    s += "<form method='POST' action='/settings/reset' onsubmit=\"return confirm('Erase all settings and open the setup hotspot?')\">"
         "<button class='danger' type='submit'>Factory reset</button></form>";
    s += "<p class='note'>Saving restarts the miner. This page is back after about 20 seconds.</p></body></html>";
    s_server->sendContent(s);
    s_server->sendContent("");  // Terminate chunked response
}

// ============================================================
// POST /settings
// ============================================================

static void handleSettingsPost() {
    if (!settingsAuth()) return;
    if (!settingsSameOrigin()) {
        settingsMessage(403, "Rejected", "Cross-site request blocked.", false);
        return;
    }

    const miner_config_t *cur = nvs_config_get();
    miner_config_t c = *cur;
    const char *err = NULL;

    char ssid[33];
    char wifiPass[64] = "";
    char webPass[33] = "";
    strncpy(ssid, cur->ssid, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';

    if (!argTo("ssid", ssid, sizeof(ssid)) || !ssid[0])                 err = "WiFi network name is missing or too long.";
    else if (!argTo("wifi_pass", wifiPass, sizeof(wifiPass)))           err = "WiFi password is too long.";
    else if (wifiPass[0] && strlen(wifiPass) < 8)                       err = "WiFi password needs at least 8 characters.";
    else if (!argTo("wallet", c.wallet, sizeof(c.wallet)) || !c.wallet[0]) err = "Wallet address is missing or too long.";
    else if (!argTo("worker", c.workerName, sizeof(c.workerName)))      err = "Worker name is too long.";
    else if (!argTo("pool_url", c.poolUrl, sizeof(c.poolUrl)) || !c.poolUrl[0]) err = "Pool URL is missing or too long.";
    else if (!argPort("pool_port", &c.poolPort))                        err = "Pool port must be 1-65535.";
    else if (!argTo("pool_pass", c.poolPassword, sizeof(c.poolPassword))) err = "Pool password is too long.";
    else if (!argTo("bk_url", c.backupPoolUrl, sizeof(c.backupPoolUrl))) err = "Backup pool URL is too long.";
    else if (c.backupPoolUrl[0] && !argPort("bk_port", &c.backupPoolPort)) err = "Backup pool port must be 1-65535.";
    else if (!argTo("bk_wallet", c.backupWallet, sizeof(c.backupWallet))) err = "Backup wallet is too long.";
    else if (!argTo("bk_pass", c.backupPoolPassword, sizeof(c.backupPoolPassword))) err = "Backup pool password is too long.";
    else if (!argTo("stats_api", c.statsApiUrl, sizeof(c.statsApiUrl))) err = "Stats API URL is too long.";
    else if (!argTo("stats_proxy", c.statsProxyUrl, sizeof(c.statsProxyUrl))) err = "Proxy URL is too long.";
    else if (!argTo("web_pass", webPass, sizeof(webPass)))              err = "Settings password is too long (max 32).";
    else if (webPass[0] && strlen(webPass) < WEB_PASSWORD_MIN_LEN)      err = "Settings password needs at least 8 characters.";
    else if (strchr(c.poolUrl, ' ') || strchr(c.wallet, ' '))           err = "Pool URL and wallet must not contain spaces.";
    else if (strpbrk(c.poolUrl, ":/") || strpbrk(c.backupPoolUrl, ":/")) err = "Pool URL: host name only (no stratum+tcp:// and no :port, the port has its own field).";

    if (err) {
        settingsMessage(400, "Not saved", err, false);
        return;
    }

    long v;
    if (s_server->hasArg("bright")) {
        v = s_server->arg("bright").toInt();
        c.brightness = (uint8_t)(v < 10 ? 10 : (v > 100 ? 100 : v));
    }
    if (s_server->hasArg("scrn_to")) {
        v = s_server->arg("scrn_to").toInt();
        c.screenTimeout = (uint16_t)(v < 0 ? 0 : (v > 3600 ? 3600 : v));
    }
    if (s_server->hasArg("rotation")) {
        v = s_server->arg("rotation").toInt();
        c.rotation = (uint8_t)(v < 0 || v > 3 ? 0 : v);
    }
    if (s_server->hasArg("tz")) {
        v = s_server->arg("tz").toInt();
        c.timezoneOffset = (int8_t)(v < -12 ? -12 : (v > 14 ? 14 : v));
    }
    if (s_server->hasArg("invert"))      c.invertColors     = (s_server->arg("invert").toInt() == 1);
    if (s_server->hasArg("stats_en"))    c.statsEnabled     = (s_server->arg("stats_en").toInt() == 1);
    if (s_server->hasArg("https_stats")) c.enableHttpsStats = (s_server->arg("https_stats").toInt() == 1);
    bool exposeWallet = nvs_api_expose_wallet();
    if (s_server->hasArg("api_wallet"))  exposeWallet = (s_server->arg("api_wallet").toInt() == 1);

    // WiFi: keep the current password unless a new one was entered. If anything
    // changes, remember the working credentials so boot can fall back to them.
    bool wifiChanged = (strcmp(ssid, cur->ssid) != 0) ||
                       (wifiPass[0] && strcmp(wifiPass, cur->wifiPassword) != 0);
    if (wifiChanged) {
        nvs_wifi_prev_save(cur->ssid, cur->wifiPassword);
        strncpy(c.ssid, ssid, sizeof(c.ssid) - 1);
        c.ssid[sizeof(c.ssid) - 1] = '\0';
        if (wifiPass[0]) {
            strncpy(c.wifiPassword, wifiPass, sizeof(c.wifiPassword) - 1);
            c.wifiPassword[sizeof(c.wifiPassword) - 1] = '\0';
        }
    }

    if (!nvs_config_save(&c)) {
        settingsMessage(500, "Not saved", "Writing the configuration to flash failed.", false);
        return;
    }
    nvs_api_save(nvs_api_enabled(), exposeWallet);
    if (webPass[0]) nvs_web_password_set(webPass);

    web_api_log("SETTINGS saved via web (wifi %s), restarting", wifiChanged ? "changed" : "unchanged");
    settingsMessage(200, "Saved", wifiChanged
        ? "Settings saved. The miner restarts and joins the new WiFi; its address may change (see display)."
        : "Settings saved. The miner restarts now; this page returns to the status view in 20 seconds.",
        !wifiChanged);
    s_restartAtMs = millis() + 1500;
}

// ============================================================
// POST /settings/reset
// ============================================================

static void handleSettingsReset() {
    if (!settingsAuth()) return;
    if (!settingsSameOrigin()) {
        settingsMessage(403, "Rejected", "Cross-site request blocked.", false);
        return;
    }
    settingsMessage(200, "Factory reset",
        "All settings are erased. Connect to the SparkMiner_XXXX hotspot (password minebitcoin) to set the miner up again.",
        false);
    s_wipeOnRestart = true;
    s_restartAtMs = millis() + 1500;
}

// Called from the API task loop: perform a pending restart once the response is out
static void settingsTick() {
    if (!s_restartAtMs || (int32_t)(millis() - s_restartAtMs) < 0) return;
    if (s_wipeOnRestart) {
        nvs_factory_reset();
        WiFi.disconnect(true, true);
        delay(300);
    }
    ESP.restart();
}

#endif // WEB_SETTINGS_H

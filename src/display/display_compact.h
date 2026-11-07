/*
 * SparkMiner - Compact TFT layout for 170x320 panels (LILYGO T-Display S3)
 *
 * The stock screens are laid out for 240x320. On a 170x320 panel they
 * overflow in landscape and overlap in portrait, so this file provides a
 * dedicated layout for both orientations.
 *
 * Every function draws through a TFT_eSPI reference, so the caller can
 * target the panel directly or a full-frame sprite (flicker-free, used
 * when PSRAM is available).
 *
 * Included by display.cpp after the colour constants, the status colour
 * helpers and the bolt bitmap. No Arduino String usage: all formatting
 * goes through stack buffers.
 *
 * GPL v3 License
 */

#ifndef DISPLAY_COMPACT_H
#define DISPLAY_COMPACT_H

#define CUI_HEADER_H    26      // Header bar height
#define CUI_BAR_H       30      // Portrait bottom status bar height
#define CUI_PAD         2       // Panel inset from the screen edge
#define CUI_TX          8       // Text inset (left edge of panel text)
#define CUI_LINE        12      // Line pitch for size-1 text
#define CUI_CHAR_W      6       // GLCD font cell width at size 1
#define CUI_GAP         3       // Gap between panels

// ============================================================
// Formatting (stack buffers only)
// ============================================================

static void cuiSplitHashrate(double h, char *val, size_t n, const char **unit) {
    if (h >= 1e9)      { snprintf(val, n, "%.2f", h / 1e9); *unit = "GH/s"; }
    else if (h >= 1e6) { snprintf(val, n, "%.2f", h / 1e6); *unit = "MH/s"; }
    else if (h >= 1e3) { snprintf(val, n, "%.2f", h / 1e3); *unit = "KH/s"; }
    else               { snprintf(val, n, "%.1f", h);       *unit = "H/s"; }
}

static void cuiHashrate(double h, char *out, size_t n) {
    char val[16];
    const char *unit;
    cuiSplitHashrate(h, val, sizeof(val), &unit);
    snprintf(out, n, "%s %s", val, unit);
}

static void cuiNumber(uint64_t v, char *out, size_t n) {
    double d = (double)v;
    if (d >= 1e12)     snprintf(out, n, "%.2fT", d / 1e12);
    else if (d >= 1e9) snprintf(out, n, "%.2fG", d / 1e9);
    else if (d >= 1e6) snprintf(out, n, "%.2fM", d / 1e6);
    else if (d >= 1e3) snprintf(out, n, "%.2fK", d / 1e3);
    else               snprintf(out, n, "%lu", (unsigned long)v);
}

static void cuiDiff(double d, char *out, size_t n) {
    if (d >= 1e15)      snprintf(out, n, "%.2fP", d / 1e15);
    else if (d >= 1e12) snprintf(out, n, "%.2fT", d / 1e12);
    else if (d >= 1e9)  snprintf(out, n, "%.2fG", d / 1e9);
    else if (d >= 1e6)  snprintf(out, n, "%.2fM", d / 1e6);
    else if (d >= 1e3)  snprintf(out, n, "%.2fK", d / 1e3);
    else                snprintf(out, n, "%.4f", d);
}

static void cuiUptime(uint32_t s, char *out, size_t n) {
    unsigned days = s / 86400, hours = (s % 86400) / 3600;
    unsigned mins = (s % 3600) / 60, secs = s % 60;
    if (days > 0)       snprintf(out, n, "%ud %uh", days, hours);
    else if (hours > 0) snprintf(out, n, "%uh %um", hours, mins);
    else                snprintf(out, n, "%um %us", mins, secs);
}

static void cuiOrDash(uint32_t v, char *out, size_t n) {
    if (v > 0) snprintf(out, n, "%lu", (unsigned long)v);
    else       snprintf(out, n, "---");
}

// ============================================================
// Drawing primitives
// ============================================================

static int cuiTextW(const char *s, uint8_t size = 1) {
    return (int)strlen(s) * CUI_CHAR_W * size;
}

static void cuiText(TFT_eSPI &g, int x, int y, uint16_t color, const char *s, uint8_t size = 1) {
    g.setTextSize(size);
    g.setTextColor(color);
    g.setCursor(x, y);
    g.print(s);
}

static void cuiTextCentered(TFT_eSPI &g, int y, uint16_t color, const char *s, uint8_t size = 1) {
    cuiText(g, (g.width() - cuiTextW(s, size)) / 2, y, color, s, size);
}

// Dim label followed by a coloured value; the value is shortened with ".."
// so that label + value never exceed maxW pixels.
static void cuiKV(TFT_eSPI &g, int x, int y, const char *label, const char *value,
                  uint16_t color, int maxW) {
    g.setTextSize(1);
    g.setTextColor(COLOR_DIM);
    g.setCursor(x, y);
    g.print(label);

    int avail = (maxW - cuiTextW(label)) / CUI_CHAR_W;
    if (avail < 1) return;

    char buf[40];
    if (avail > (int)sizeof(buf) - 1) avail = sizeof(buf) - 1;
    if ((int)strlen(value) > avail) {
        int keep = (avail > 2) ? avail - 2 : avail;
        memcpy(buf, value, keep);
        if (avail > 2) { buf[keep++] = '.'; buf[keep++] = '.'; }
        buf[keep] = '\0';
        value = buf;
    }
    g.setTextColor(color);
    g.print(value);
}

// Rounded panel; border == COLOR_PANEL draws no visible border
static void cuiPanel(TFT_eSPI &g, int x, int y, int w, int h, uint16_t border) {
    g.fillRoundRect(x, y, w, h, 4, COLOR_PANEL);
    if (border != COLOR_PANEL) g.drawRoundRect(x, y, w, h, 4, border);
}

static void cuiLogo(TFT_eSPI &g, int x, int y, int size) {
    float scale = (float)size / BOLT_H;
    int cell = (int)scale + 1;
    for (int row = 0; row < BOLT_H; row++) {
        uint16_t bits = ((uint16_t)boltBitmap[row * 2] << 8) | boltBitmap[row * 2 + 1];
        for (int col = 0; col < BOLT_W; col++) {
            if (bits & (0x8000 >> col)) {
                g.fillRect(x + (int)(col * scale), y + (int)(row * scale), cell, cell, COLOR_SPARK1);
            }
        }
    }
}

// TEMP / WIFI / POOL indicators: label on the first row, value on the second
static void cuiStatusItems(TFT_eSPI &g, const display_data_t *d,
                           int xTemp, int xWifi, int xPool, int y) {
    char buf[12];

    float temp = temperatureRead();
    snprintf(buf, sizeof(buf), "%dC", (int)temp);
    cuiText(g, xTemp, y, COLOR_DIM, "TEMP");
    cuiText(g, xTemp, y + 11, getTempColor(temp), buf);

    uint16_t wifiColor = d->wifiConnected ? getWifiColor(d->wifiRssi) : COLOR_ERROR;
    cuiText(g, xWifi, y, COLOR_DIM, "WIFI");
    g.fillCircle(xWifi + 3, y + 14, 3, wifiColor);
    if (d->wifiConnected) {
        snprintf(buf, sizeof(buf), "%d", (int)d->wifiRssi);
        cuiText(g, xWifi + 10, y + 11, wifiColor, buf);
    }

    uint16_t pingColor = d->poolConnected ? getPingColor(d->avgLatency) : COLOR_ERROR;
    if (d->poolConnected && d->poolFailovers > 0) pingColor = COLOR_WARNING;
    cuiText(g, xPool, y, COLOR_DIM, "POOL");
    g.fillCircle(xPool + 3, y + 14, 3, pingColor);
    if (d->poolConnected && d->avgLatency > 0) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)(d->avgLatency > 9999 ? 9999 : d->avgLatency));
        cuiText(g, xPool + 10, y + 11, pingColor, buf);
    }
}

// ============================================================
// Header and portrait status bar
// ============================================================

static void cuiDrawHeader(TFT_eSPI &g, const display_data_t *d) {
    const int w = g.width();

    g.fillRect(0, 0, w, CUI_HEADER_H, COLOR_PANEL);
    g.drawFastHLine(0, CUI_HEADER_H - 1, w, COLOR_ACCENT);

    cuiLogo(g, 4, 3, 20);

    g.setTextSize(2);
    g.setCursor(26, 5);
    g.setTextColor(COLOR_ACCENT);
    g.print("Spark");
    g.setTextColor(COLOR_SPARK1);
    g.print("Miner");

    char ver[8];
    snprintf(ver, sizeof(ver), "V%d", getMajorVersion());
    cuiText(g, 150, 12, COLOR_DIM, ver);

    // Landscape has room for the indicators; portrait shows them in the bottom bar
    if (w > g.height()) {
        int xPool = w - 46;
        int xWifi = xPool - 46;
        int xTemp = xWifi - 36;
        cuiStatusItems(g, d, xTemp, xWifi, xPool, 3);
    }
}

static void cuiDrawBottomBar(TFT_eSPI &g, const display_data_t *d) {
    const int w = g.width();
    const int y = g.height() - CUI_BAR_H;
    const int section = w / 3;

    g.fillRect(0, y, w, CUI_BAR_H, COLOR_PANEL);
    g.drawFastHLine(0, y, w, COLOR_SPARK2);
    cuiStatusItems(g, d, CUI_TX, CUI_TX + section, CUI_TX + 2 * section, y + 5);
}

// ============================================================
// Screen 1: Mining
// ============================================================

static void cuiDrawMining(TFT_eSPI &g, const display_data_t *d, const char *host) {
    const int w = g.width();
    const bool portrait = w < g.height();
    const int pw = w - 2 * CUI_PAD;         // Panel width
    const int rx = w - 108;                 // Right text column (landscape)
    char buf[40];
    int y = CUI_HEADER_H + 2;

    // --- Hashrate panel ---
    const int hrH = portrait ? 48 : 32;
    cuiPanel(g, CUI_PAD, y, pw, hrH, COLOR_ACCENT);

    char val[16];
    const char *unit;
    cuiSplitHashrate(d->hashRate, val, sizeof(val), &unit);
    cuiText(g, CUI_TX, y + 4, COLOR_ACCENT, val, 3);
    int ux = CUI_TX + cuiTextW(val, 3) + 4;
    if (portrait) cuiText(g, ux, y + 20, COLOR_SPARK1, unit, 1);
    else          cuiText(g, ux, y + 12, COLOR_SPARK1, unit, 2);

    snprintf(buf, sizeof(buf), "%lu/%lu", (unsigned long)d->sharesAccepted,
             (unsigned long)(d->sharesAccepted + d->sharesRejected));
    if (portrait) {
        cuiKV(g, CUI_TX, y + 35, "Shares: ", buf, COLOR_FG, pw - 12);
    } else {
        cuiText(g, w - 100, y + 5, COLOR_DIM, "Shares");
        cuiText(g, w - 100, y + 17, COLOR_FG, buf);
    }
    y += hrH + CUI_GAP;

    // --- Stats grid: 3x2 in landscape, 2x3 in portrait ---
    struct { const char *label; char value[16]; uint16_t color; } cells[6] = {
        {"Best",     "", COLOR_SPARK1},
        {"Hashes",   "", COLOR_FG},
        {"Uptime",   "", COLOR_FG},
        {"Jobs",     "", COLOR_FG},
        {"Blocks",   "", COLOR_SUCCESS},
        {"Swarm HR", "", COLOR_SPARK1},
    };
    cuiDiff(d->bestDifficulty, cells[0].value, sizeof(cells[0].value));
    cuiNumber(d->totalHashes, cells[1].value, sizeof(cells[1].value));
    cuiUptime(d->uptimeSeconds, cells[2].value, sizeof(cells[2].value));
    snprintf(cells[3].value, sizeof(cells[3].value), "%lu", (unsigned long)d->templates);
    snprintf(cells[4].value, sizeof(cells[4].value), "%lu", (unsigned long)d->blocksFound);
    snprintf(cells[5].value, sizeof(cells[5].value), "%.12s",
             d->workerHashrate[0] ? d->workerHashrate : "---");

    const int cols = portrait ? 2 : 3;
    const int rows = 6 / cols;
    const int boxW = (pw - (cols - 1) * CUI_GAP) / cols;
    const int boxH = 23;
    for (int i = 0; i < 6; i++) {
        int x = CUI_PAD + (i % cols) * (boxW + CUI_GAP);
        int by = y + (i / cols) * (boxH + CUI_GAP);
        g.fillRoundRect(x, by, boxW, boxH, 3, COLOR_PANEL);
        cuiText(g, x + 4, by + 3, COLOR_DIM, cells[i].label);
        cuiText(g, x + 4, by + 13, cells[i].color, cells[i].value);
    }
    y += rows * (boxH + CUI_GAP);

    // --- Pool / network panel ---
    uint16_t poolColor = COLOR_ERROR;
    if (d->poolConnected) poolColor = (d->poolFailovers > 0) ? COLOR_WARNING : COLOR_SUCCESS;
    const char *pool = d->poolName ? d->poolName : "Disconnected";
    const char *ip = d->ipAddress ? d->ipAddress : "---";
    const char *poolHr = d->poolHashrate[0] ? d->poolHashrate : "---";

    char diff[16], fee[16], blk[16], miners[16];
    cuiDiff(d->poolDifficulty, diff, sizeof(diff));
    if (d->halfHourFee > 0) snprintf(fee, sizeof(fee), "%d sat", d->halfHourFee);
    else                    snprintf(fee, sizeof(fee), "---");
    cuiOrDash(d->blockHeight, blk, sizeof(blk));
    cuiOrDash(d->poolWorkersTotal > 0 ? (uint32_t)d->poolWorkersTotal : 0, miners, sizeof(miners));

    if (portrait) {
        const int lines = host ? 8 : 7;
        const int tw = pw - 12;
        cuiPanel(g, CUI_PAD, y, pw, lines * CUI_LINE + 5, COLOR_SPARK2);
        int ly = y + 5;
        cuiKV(g, CUI_TX, ly, "Pool: ", pool, poolColor, tw);            ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Miners: ", miners, COLOR_SPARK1, tw);     ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Diff: ", diff, COLOR_FG, tw);             ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Fee: ", fee, COLOR_SPARK2, tw);           ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Pool HR: ", poolHr, COLOR_SPARK1, tw);    ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Block: ", blk, COLOR_FG, tw);             ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "LAN: ", ip, COLOR_FG, tw);                ly += CUI_LINE;
        if (host) cuiKV(g, CUI_TX, ly, "Host: ", host, COLOR_FG, tw);
    } else {
        const int lw = rx - CUI_TX - 6;     // Left column width
        const int rw = w - rx - 6;          // Right column width
        cuiPanel(g, CUI_PAD, y, pw, 4 * CUI_LINE + 5, COLOR_SPARK2);
        int ly = y + 5;
        cuiKV(g, CUI_TX, ly, "Pool: ", pool, poolColor, lw);
        cuiKV(g, rx, ly, "Miners: ", miners, COLOR_SPARK1, rw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Diff: ", diff, COLOR_FG, lw);
        cuiKV(g, rx, ly, "Fee: ", fee, COLOR_SPARK2, rw);               ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Pool HR: ", poolHr, COLOR_SPARK1, lw);
        cuiKV(g, rx, ly, "Block: ", blk, COLOR_FG, rw);                 ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "LAN: ", ip, COLOR_FG, lw);
        if (host) cuiKV(g, rx, ly, "", host, COLOR_DIM, rw);
    }
}

// ============================================================
// Screen 2: Network stats
// ============================================================

static void cuiDrawStats(TFT_eSPI &g, const display_data_t *d) {
    const int w = g.width();
    const bool portrait = w < g.height();
    const int pw = w - 2 * CUI_PAD;
    const int rx = w - 108;
    const int tw = pw - 12;
    char buf[40];
    int y = CUI_HEADER_H + 2;

    // --- BTC price panel ---
    const int prH = portrait ? 48 : 32;
    cuiPanel(g, CUI_PAD, y, pw, prH, COLOR_SPARK1);
    if (d->btcPrice > 0) {
        snprintf(buf, sizeof(buf), "$%.0f", (double)d->btcPrice);
        cuiText(g, CUI_TX, y + 4, COLOR_SPARK1, buf, 3);
    } else {
        cuiText(g, CUI_TX, y + 8, COLOR_DIM, "Loading...", 2);
    }
    char blk[16];
    cuiOrDash(d->blockHeight, blk, sizeof(blk));
    if (portrait) {
        cuiKV(g, CUI_TX, y + 35, "Block: ", blk, COLOR_FG, tw);
    } else {
        cuiText(g, w - 100, y + 5, COLOR_DIM, "Block");
        cuiText(g, w - 100, y + 17, COLOR_FG, blk);
    }
    y += prH + CUI_GAP;

    // --- Network panel ---
    const char *netHr = d->networkHashrate[0] ? d->networkHashrate : "---";
    const char *netDiff = d->networkDifficulty[0] ? d->networkDifficulty : "---";
    char fee[16];
    if (d->halfHourFee > 0) snprintf(fee, sizeof(fee), "%d sat", d->halfHourFee);
    else                    snprintf(fee, sizeof(fee), "---");

    const int netLines = portrait ? 3 : 2;
    cuiPanel(g, CUI_PAD, y, pw, netLines * CUI_LINE + 5, COLOR_PANEL);
    int ly = y + 5;
    if (portrait) {
        cuiKV(g, CUI_TX, ly, "Network: ", netHr, COLOR_FG, tw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Difficulty: ", netDiff, COLOR_FG, tw);    ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Fee: ", fee, COLOR_SPARK2, tw);
    } else {
        cuiKV(g, CUI_TX, ly, "Network: ", netHr, COLOR_FG, rx - CUI_TX - 6);
        cuiKV(g, rx, ly, "Fee: ", fee, COLOR_SPARK2, w - rx - 6);       ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Difficulty: ", netDiff, COLOR_FG, rx - CUI_TX - 6);
    }
    y += netLines * CUI_LINE + 5 + CUI_GAP;

    // --- Your mining panel ---
    char rate[24], best[16], shares[16], blocks[16], uptime[16], hashes[16], onPool[20];
    cuiHashrate(d->hashRate, rate, sizeof(rate));
    cuiDiff(d->bestDifficulty, best, sizeof(best));
    snprintf(shares, sizeof(shares), "%lu", (unsigned long)d->sharesAccepted);
    snprintf(blocks, sizeof(blocks), "%lu", (unsigned long)d->blocksFound);
    cuiUptime(d->uptimeSeconds, uptime, sizeof(uptime));
    cuiNumber(d->totalHashes, hashes, sizeof(hashes));
    onPool[0] = '\0';
    if (d->poolWorkersTotal > 0) snprintf(onPool, sizeof(onPool), "%d on pool", d->poolWorkersTotal);

    if (portrait) {
        const int lines = onPool[0] ? 8 : 7;
        cuiPanel(g, CUI_PAD, y, pw, lines * CUI_LINE + 5, COLOR_ACCENT);
        ly = y + 5;
        cuiText(g, CUI_TX, ly, COLOR_ACCENT, "Your Mining");            ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Rate: ", rate, COLOR_FG, tw);             ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Best: ", best, COLOR_SPARK1, tw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Shares: ", shares, COLOR_FG, tw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Blocks: ", blocks, COLOR_SUCCESS, tw);    ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Uptime: ", uptime, COLOR_FG, tw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Hashes: ", hashes, COLOR_FG, tw);         ly += CUI_LINE;
        if (onPool[0]) cuiText(g, CUI_TX, ly, COLOR_SPARK1, onPool);
    } else {
        const int lw = rx - CUI_TX - 6;
        const int rw = w - rx - 6;
        cuiPanel(g, CUI_PAD, y, pw, 4 * CUI_LINE + 5, COLOR_ACCENT);
        ly = y + 5;
        cuiText(g, CUI_TX, ly, COLOR_ACCENT, "Your Mining");
        if (onPool[0]) cuiText(g, rx, ly, COLOR_SPARK1, onPool);
        ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Rate: ", rate, COLOR_FG, lw);
        cuiKV(g, rx, ly, "Shares: ", shares, COLOR_FG, rw);             ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Best: ", best, COLOR_SPARK1, lw);
        cuiKV(g, rx, ly, "Blocks: ", blocks, COLOR_SUCCESS, rw);        ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Uptime: ", uptime, COLOR_FG, lw);
        cuiKV(g, rx, ly, "Hashes: ", hashes, COLOR_FG, rw);
    }
}

// ============================================================
// Screen 3: Clock
// ============================================================

static void cuiDrawClock(TFT_eSPI &g, const display_data_t *d) {
    const int w = g.width();
    const int h = g.height();
    const bool portrait = w < h;
    const int pw = w - 2 * CUI_PAD;
    const int rx = w - 108;
    const int tw = pw - 12;
    char buf[40];

    // Short timeout: the default (5 s) would stall the monitor task
    // on every refresh while NTP has not synced yet.
    struct tm t;
    if (!getLocalTime(&t, 10)) {
        cuiTextCentered(g, h / 2 - 8, COLOR_DIM, "No Time", 2);
        return;
    }

    char rate[24], shares[16], best[16], uptime[16], blk[16], price[16];
    cuiHashrate(d->hashRate, rate, sizeof(rate));
    snprintf(shares, sizeof(shares), "%lu", (unsigned long)d->sharesAccepted);
    cuiDiff(d->bestDifficulty, best, sizeof(best));
    cuiUptime(d->uptimeSeconds, uptime, sizeof(uptime));
    cuiOrDash(d->blockHeight, blk, sizeof(blk));
    if (d->btcPrice > 0) snprintf(price, sizeof(price), "$%.0f", (double)d->btcPrice);
    else                 snprintf(price, sizeof(price), "---");

    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    if (portrait) {
        cuiPanel(g, CUI_PAD, 36, pw, 40, COLOR_ACCENT);
        cuiTextCentered(g, 44, COLOR_ACCENT, buf, 3);

        strftime(buf, sizeof(buf), "%a, %b %d", &t);
        cuiTextCentered(g, 88, COLOR_FG, buf, 2);
        strftime(buf, sizeof(buf), "%Y", &t);
        cuiTextCentered(g, 110, COLOR_DIM, buf, 2);

        int y = 150;
        cuiPanel(g, CUI_PAD, y, pw, 6 * CUI_LINE + 5, COLOR_PANEL);
        int ly = y + 5;
        cuiKV(g, CUI_TX, ly, "Hash: ", rate, COLOR_ACCENT, tw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Shares: ", shares, COLOR_FG, tw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Best: ", best, COLOR_SPARK1, tw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Uptime: ", uptime, COLOR_FG, tw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "BTC: ", price, COLOR_SPARK1, tw);         ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Block: ", blk, COLOR_FG, tw);
    } else {
        cuiPanel(g, CUI_PAD, 32, pw, 48, COLOR_ACCENT);
        cuiTextCentered(g, 40, COLOR_ACCENT, buf, 4);

        strftime(buf, sizeof(buf), "%a, %b %d %Y", &t);
        cuiTextCentered(g, 90, COLOR_FG, buf, 2);

        const int lw = rx - CUI_TX - 6;
        const int rw = w - rx - 6;
        int y = 116;
        cuiPanel(g, CUI_PAD, y, pw, 3 * CUI_LINE + 5, COLOR_PANEL);
        int ly = y + 5;
        cuiKV(g, CUI_TX, ly, "Hash: ", rate, COLOR_ACCENT, lw);
        cuiKV(g, rx, ly, "BTC: ", price, COLOR_SPARK1, rw);             ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Shares: ", shares, COLOR_FG, lw);
        cuiKV(g, rx, ly, "Block: ", blk, COLOR_FG, rw);                 ly += CUI_LINE;
        cuiKV(g, CUI_TX, ly, "Best: ", best, COLOR_SPARK1, lw);
        cuiKV(g, rx, ly, "Uptime: ", uptime, COLOR_FG, rw);
    }
}

// ============================================================
// Frame + WiFi setup screen
// ============================================================

// Draws one complete frame. The caller clears the background first.
static void cuiDrawFrame(TFT_eSPI &g, uint8_t screen, const display_data_t *d, const char *host) {
    cuiDrawHeader(g, d);
    switch (screen) {
        case SCREEN_STATS: cuiDrawStats(g, d); break;
        case SCREEN_CLOCK: cuiDrawClock(g, d); break;
        default:           cuiDrawMining(g, d, host); break;
    }
    if (g.width() < g.height()) cuiDrawBottomBar(g, d);
}

static void cuiDrawApConfig(TFT_eSPI &g, const char *ssid, const char *password, const char *ip) {
    const int w = g.width();
    const int fit = w - 2 * CUI_TX;
    char url[40];
    snprintf(url, sizeof(url), "http://%s", ip);

    cuiLogo(g, CUI_TX, 6, 20);
    cuiText(g, CUI_TX + 22, 8, COLOR_ACCENT, "WiFi Setup", 2);
    g.drawFastHLine(0, 30, w, COLOR_ACCENT);

    int y = 40;
    cuiText(g, CUI_TX, y, COLOR_FG, "1. Connect to WiFi:");
    y += CUI_LINE;
    uint8_t size = (cuiTextW(ssid, 2) <= fit) ? 2 : 1;
    cuiText(g, CUI_TX, y, COLOR_ACCENT, ssid, size);
    y += (size == 2) ? 22 : 14;

    cuiKV(g, CUI_TX, y, "Password: ", password, COLOR_SPARK1, fit);
    y += CUI_LINE + 10;

    cuiText(g, CUI_TX, y, COLOR_FG, "2. Open in browser:");
    y += CUI_LINE;
    size = (cuiTextW(url, 2) <= fit) ? 2 : 1;
    cuiText(g, CUI_TX, y, COLOR_ACCENT, url, size);
}

#endif // DISPLAY_COMPACT_H

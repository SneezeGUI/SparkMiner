#include <Arduino.h>
#include <WebServer.h>
#include <esp_timer.h>
#include "advanced_web.h"
#ifdef ADVANCED_MAIL
#include "../mail/mail_service.h"
#endif
#include "dashboard_page.h"
#ifdef ADVANCED_LOCAL_CONTROLS
#include "local_settings.h"
#endif

namespace {
portMUX_TYPE cacheMux = portMUX_INITIALIZER_UNLOCKED;
advanced::Snapshot cached;
bool ready = false;
TaskHandle_t webTask = nullptr;
WebServer server(80);

void headers() {
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
    server.sendHeader("Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
}
void stats() {
    advanced::Snapshot copy;
    portENTER_CRITICAL(&cacheMux);
    bool available = ready;
    if (available) copy = cached;
    portEXIT_CRITICAL(&cacheMux);
    headers();
    if (!available) {
        server.sendHeader("Retry-After", "1");
        server.send(503, "application/json", "{\"error\":\"statistics_not_ready\"}");
        return;
    }
    // Fixed-size serialization; no configuration or credentials enter this payload.
    char json[1024];
    uint64_t now = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    if (!advanced::json(copy, now, json, sizeof(json))) {
        server.send(500, "application/json", "{\"error\":\"statistics_unavailable\"}");
        return;
    }
    server.send(200, "application/json; charset=utf-8", json);
}
void run(void*) {
    server.on("/", HTTP_GET, [] { headers(); server.send_P(200, "text/html; charset=utf-8", ADVANCED_DASHBOARD); });
    server.on("/api/advanced-stats", HTTP_GET, stats);
    #ifdef ADVANCED_LOCAL_CONTROLS
    local_settings_register(server);
    #endif
    server.onNotFound([] { headers(); server.send(404, "text/plain", "Not found"); });
    server.begin();
    Serial.println("[ADVANCED] Read-only statistics dashboard on port 80");
    for (;;) { server.handleClient(); vTaskDelay(pdMS_TO_TICKS(10)); }
}
}
void advanced_stats_publish(const advanced::Snapshot& snapshot) {
#ifdef ADVANCED_MAIL
    mail_publish(snapshot);
#endif
    portENTER_CRITICAL(&cacheMux);
    cached = snapshot;
    ready = true;
    portEXIT_CRITICAL(&cacheMux);
}
bool advanced_stats_web_start() {
    if (webTask) return true;
#ifdef ADVANCED_MAIL
    if(!mail_start())return false;
#endif
    // Separate low-priority task: slow browsers cannot block display or checkpoints.
    return xTaskCreatePinnedToCore(run, "StatsWeb", 6144, nullptr, 1, &webTask, 0) == pdPASS;
}

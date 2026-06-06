/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright Theodore Robert Campbell Jr <trcjr@stupidfoot.com>
 */

#include "logging.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static SemaphoreHandle_t s_logMutex = NULL;
static volatile bool s_startupBarrierOpen = false;

void log_init() {
    if (s_logMutex == NULL) {
        s_logMutex = xSemaphoreCreateMutex();
    }
}

void log_set_startup_barrier(bool open) {
    s_startupBarrierOpen = open;
}

bool log_is_startup_barrier_open() {
    return s_startupBarrierOpen;
}

void log_wait_startup_barrier(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (!s_startupBarrierOpen) {
        if (timeoutMs > 0 && (millis() - start) >= timeoutMs) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void log_emit_atomic(const char *text) {
    if (text == NULL) {
        return;
    }

    if (s_logMutex == NULL) {
        Serial.print(text);
        return;
    }

    if (xSemaphoreTake(s_logMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        Serial.print(text);
        xSemaphoreGive(s_logMutex);
    } else {
        // Fallback to avoid dropping logs if mutex is contended.
        Serial.print(text);
    }
}

void log_line(const char *message) {
    if (message == NULL) {
        return;
    }

    char line[256];
    size_t len = strnlen(message, sizeof(line) - 2);
    memcpy(line, message, len);
    if (len == 0 || message[len - 1] != '\n') {
        line[len++] = '\n';
    }
    line[len] = '\0';

    log_emit_atomic(line);
}

void log_linef(const char *fmt, ...) {
    if (fmt == NULL) {
        return;
    }

    char line[320];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (written < 0) {
        return;
    }

    size_t len = strnlen(line, sizeof(line) - 2);
    if (len == 0 || line[len - 1] != '\n') {
        line[len++] = '\n';
        line[len] = '\0';
    }

    log_emit_atomic(line);
}

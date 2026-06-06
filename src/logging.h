/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright Theodore Robert Campbell Jr <trcjr@stupidfoot.com>
 */

#pragma once

#include <Arduino.h>

void log_init();
void log_set_startup_barrier(bool open);
bool log_is_startup_barrier_open();
void log_wait_startup_barrier(uint32_t timeoutMs = 5000);
void log_line(const char *message);
void log_linef(const char *fmt, ...);

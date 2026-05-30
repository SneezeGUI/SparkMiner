/* 
 * BitsyMiner Open Source
 * Copyright (c) 2025 Justin Williams
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef NVS_HANDLER_H
#define NVS_HANDLER_H

bool loadSettings();
bool saveSettings();

void init_nvs();
bool save_blob(const char* key, const void*value, size_t length);
bool read_blob(const char* key, void* buffer, size_t length);
void delete_key(const char* key);
void delete_all_keys();
bool saveMonitorData();
bool loadMonitorData();

#endif
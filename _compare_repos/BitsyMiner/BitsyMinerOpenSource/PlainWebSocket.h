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
#ifndef PLAINWEBSOCKET_H
#define PLAINWEBSOCKET_H

#include <WiFi.h>

#define MAX_CLIENTS 5


class PlainWebSocket {
public:
    PlainWebSocket(uint16_t port = 81);
    void begin();
    void onMessage(void (*callback)(String));
    void broadcast(const String& message);
    void broadcast(const char* message, size_t length);
    void broadcast(const char* color, const char* message, size_t length);

private:
    WiFiServer server;
    WiFiClient clients[MAX_CLIENTS];
    void (*messageCallback)(String) = nullptr;

    static void websocketTask(void* pvParameters);
    void handleClient(WiFiClient& client);
    static void performHandshake(WiFiClient& client, const String& request);
    void sendMessage(WiFiClient& client, const String& message);
    void sendMessage(WiFiClient& client, const char* message, size_t length);
    void sendMessage(WiFiClient& client, const char* color, const char* message, size_t length);

    static void base64_encode(String& output, const char* input, size_t len) {
      const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      output.reserve(((len + 2) / 3) * 4);

      for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (input[i] << 16) | (i + 1 < len ? input[i + 1] << 8 : 0) | (i + 2 < len ? input[i + 2] : 0);
        output += base64_chars[(n >> 18) & 63];
        output += base64_chars[(n >> 12) & 63];
        output += (i + 1 < len) ? base64_chars[(n >> 6) & 63] : '=';
        output += (i + 2 < len) ? base64_chars[n & 63] : '=';
      }
    }
};

#endif

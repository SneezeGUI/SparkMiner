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
#include "PlainWebSocket.h"
#include "sha1.h"
#include "defines_n_types.h"

extern SetupData settings;

PlainWebSocket::PlainWebSocket(uint16_t port) : server(port) {}

void PlainWebSocket::begin() {
    server.begin();

    // Create a FreeRTOS task for async handling
    xTaskCreatePinnedToCore(
        websocketTask,       // Task function
        "WebSocketTask",      // Task name
        6000,                // Stack size
        this,                 // Task parameters
        1,                    // Priority
        NULL,                  // Task handle
        0                     // Pinned to core 0
    );
}

void PlainWebSocket::onMessage(void (*callback)(String)) {
    messageCallback = callback;
}

void PlainWebSocket::websocketTask(void* pvParameters) {
  PlainWebSocket* ws = reinterpret_cast<PlainWebSocket*>(pvParameters);

  while (true) {
    WiFiClient newClient = ws->server.available();
    if (newClient) {
      
      // If log viewing isn't turned on, then shut down this client
      if( ! settings.enableLogViewer ) {
        newClient.stop();
        vTaskDelay(50 / portTICK_PERIOD_MS); 
        continue;
      }

      // Add new client to the list
      int i = 0;
      for (; i < MAX_CLIENTS; ++i) {
        if (!ws->clients[i] || !ws->clients[i].connected()) {
          ws->clients[i] = newClient;

          // Read header
          int16_t wp = 200;               
          while(! ws->clients[i].available() && wp-- > 0 ) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
          }
          String key;
          bool requestingUpgrade = false;
          bool gotKey = false;
          while( ws->clients[i].available() ) {
            String line = ws->clients[i].readStringUntil('\n');

            // See if this is a websocket client
            if( line.indexOf("Upgrade: websocket") != -1 ) {
              requestingUpgrade = true;                        
            }
            // Look for Websocket key to perform handshake
            if( requestingUpgrade && line.indexOf("Sec-WebSocket-Key") != - 1 ) {
              int start = line.indexOf("Sec-WebSocket-Key: ");
              if (start >= 0) {
                  start += 19;
                  int end = line.indexOf("\r\n", start);
                  key = line.substring(start, end);
                  key.trim();
                  gotKey = true;
              }
            }
            // See if the headers are done
            if( line.length() == 0 || (line.length() == 1 && line.c_str()[0] == '\r') ) {
              if( requestingUpgrade && gotKey ) {
                performHandshake(ws->clients[i], key);
              }
              break;
            }
          }
          break;
        }
      }
      if( i >= MAX_CLIENTS ) {
        newClient.stop();
      }
    }

    // Handle existing clients
    for (int i = 0; i < MAX_CLIENTS; ++i) {
      if (ws->clients[i] && ws->clients[i].connected()) {
        ws->handleClient(ws->clients[i]);
      }
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);  // Yield CPU
  }
}

void PlainWebSocket::handleClient(WiFiClient& client) {
    if (!client.connected()) {
      client.stop();
      return;
    }

    if (client.available()) {

      // Read and process WebSocket data
      uint8_t data[256];
      size_t len = client.read(data, sizeof(data));

      if (len > 0 && len < 256) {
        data[len] = 0;  // Null-terminate the message
        String message = String((char*)data);

        // Trigger callback with received message
        if (messageCallback) {
          messageCallback(message);
        }
      }
    }
}

void PlainWebSocket::performHandshake(WiFiClient& client, const String& request) {
  // Extract WebSocket key from the request
  // Append the GUID and hash it
  String acceptKey = request + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  
  // SHA-1 hash the key
  uint8_t hash[20];

  SHA1_CTX ctx;
  sha1_init(&ctx);
  sha1_update(&ctx, reinterpret_cast<const uint8_t*>(acceptKey.c_str()), acceptKey.length());
  sha1_final(&ctx, hash);

  // Base64 encode the result
  String acceptBase64;
  base64_encode(acceptBase64, reinterpret_cast<char*>(hash), 20);

  // Send the WebSocket handshake response
  client.println("HTTP/1.1 101 Switching Protocols");
  client.println("Upgrade: websocket");
  client.println("Connection: Upgrade");
  client.println("Sec-WebSocket-Accept: " + acceptBase64);
  client.println();
}

void PlainWebSocket::broadcast(const String& message) {
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i] && clients[i].connected()) {
      sendMessage(clients[i], message);
    }
  }
}


void PlainWebSocket::broadcast(const char* message, size_t length) {
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i] && clients[i].connected()) {
      sendMessage(clients[i], message, length);
    }
  }
}

void PlainWebSocket::broadcast(const char* color, const char* message, size_t length) {
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i] && clients[i].connected()) {
      sendMessage(clients[i], color, message, length);
    }
  }
}

void PlainWebSocket::sendMessage(WiFiClient& client, const String& message) {
  sendMessage(client, message.c_str(), message.length());
}

void PlainWebSocket::sendMessage(WiFiClient& client, const char* color, const char* message, size_t length) {

  size_t len = length;
  size_t pos = 0;
  
  const size_t maxFrameSize = 125;  // Max payload size for single-byte length encoding
  
  char colorBuf[30];

  bool isFirstFrame = true;

  while (pos < len) {

    uint8_t header[2];
    if( isFirstFrame && len && color && strlen(color) < sizeof(colorBuf) - 3 ) {
      sprintf(colorBuf, "[%s]", color);
      header[0] = 0x01;
      header[1] = strlen(colorBuf);
      client.write(header, 2);
      client.write(colorBuf, strlen(colorBuf));
      isFirstFrame = false;
    } 

    size_t chunkSize = (len - pos > maxFrameSize) ? maxFrameSize : (len - pos);
  
    header[0] = isFirstFrame ? 0x01 : 0x00;  // 0x01 = text, 0x00 = continuation
    if (pos + chunkSize >= len) {
      header[0] |= 0x80;  // Set FIN bit on the last frame
    }

    header[1] = chunkSize;  // Payload length
    client.write(header, 2);
    client.write((uint8_t*)(message + pos), chunkSize);

    pos += chunkSize;
    isFirstFrame = false;
  }
}

void PlainWebSocket::sendMessage(WiFiClient& client, const char* message, size_t length) {
  sendMessage(client, NULL, message, length);
}
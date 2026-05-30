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
#include "MyWiFi.h"
#include "defines_n_types.h"


#define AP_MODE WIFI_AP_STA
//#define AP_MODE WIFI_AP

char MyWiFi::theSSID[MAX_SSID_LENGTH] = "";
char MyWiFi::theSSIDPassword[MAX_PASSWORD_LENGTH] = "";
char MyWiFi::apSSID[MAX_SSID_LENGTH] = "";
char MyWiFi::apPassword[MAX_PASSWORD_LENGTH] = "";
MyWiFiEventCallback MyWiFi::ipCallback = NULL;



bool MyWiFi::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool MyWiFi::isConnecting() {
  return (WiFi.status() == WL_IDLE_STATUS);
}
void MyWiFi::setIPCallback(MyWiFiEventCallback callback) {
  ipCallback = callback;
}

// Returns the number of clients connected to the access point
int16_t MyWiFi::apClientCount() {
  if( isAccessPoint() ) {
    return WiFi.softAPgetStationNum();
  }
  return 0;
}


void MyWiFi::handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {

  switch (event) {
    case WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      dbg("Event: ARDUINO_EVENT_WIFI_AP_STADISCONNECTED\n");
      break;
    case WiFiEvent_t::ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      dbg("Event: ARDUINO_EVENT_WIFI_AP_STACONNECTED\n");
      break;
    case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED:
      dbg("Event: ARDUINO_EVENT_WIFI_STA_CONNECTED\n");
      break;
    case WiFiEvent_t::ARDUINO_EVENT_WIFI_READY:
      dbg("Event: ARDUINO_EVENT_WIFI_READY\n");
      break;
    case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP:
      if( ipCallback ) {
        ipCallback();
      }
      break;
    case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      dbg("WiFi lost connection.\n");
      dbg("Disconnect reason: %d\n", info.wifi_sta_disconnected.reason);
      break;
  }

  
}

void MyWiFi::setupEvents() {
  WiFi.onEvent(handleWiFiEvent);
}



// Set up the info for our access point
void MyWiFi::setAccessPointInfo(char* apssid, char *password) {
  strncpy(apSSID, apssid, MAX_SSID_LENGTH);
  strncpy(apPassword, password, MAX_PASSWORD_LENGTH);
}

// Set up the SSID we'll connect to 
void MyWiFi::setSSIDInfo(char *ssid, char *password) {
  strncpy(theSSID, ssid, MAX_SSID_LENGTH);
  strncpy(theSSIDPassword, password, MAX_PASSWORD_LENGTH);
}


void MyWiFi::configure(uint32_t ipAddress,
                       uint32_t gatewayAddress,
                       uint32_t subnetAddress,
                       uint32_t primaryDNSAddress,
                       uint32_t secondaryDNSAddress)
{
  if (ipAddress == INADDR_NONE || ipAddress == INADDR_ANY) {
    // Enable DHCP
    WiFi.config(IPAddress(0,0,0,0),
                IPAddress(0,0,0,0),
                IPAddress(0,0,0,0));
    return;
  }

  IPAddress ip(ipAddress);
  IPAddress gateway(gatewayAddress);
  IPAddress subnet(subnetAddress);
  IPAddress primaryDns(primaryDNSAddress);
  IPAddress secondaryDns(secondaryDNSAddress);

  dbg("Static IP: %s\n", ip.toString().c_str());

  if( ! WiFi.config(ip, gateway, subnet, primaryDns, secondaryDns) ) {
    dbg("Failed to configure WiFi.");
  } else {
    dbg("Configured WiFi successfully.");
  }
}



void MyWiFi::enterAccessPointMode() {
  int16_t timeout = 20;
  if( WiFi.getMode() == WIFI_MODE_STA ) {
    dbg("Entering access point mode..");
    WiFi.disconnect(true);
    while (WiFi.status() == WL_CONNECTED && --timeout > 0) {
        delay(100);
    }
  }
  if( ! isAccessPoint() ) {
    WiFi.mode(AP_MODE);
    WiFi.softAP(apSSID, apPassword);  
  }
}


void MyWiFi::enterStationMode() {
  WiFi.persistent(false);
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(false);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(theSSID, theSSIDPassword);

  uint32_t start = millis();
  const uint32_t timeoutMs = 20000; // 20 seconds

  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    dbg("WiFi connect timeout\n");
  }
}


bool MyWiFi::isAccessPoint() {
  wifi_mode_t m = WiFi.getMode();
  return (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA);
}

IPAddress MyWiFi::getIP() {
  IPAddress s;
  if( WiFi.getMode() == WIFI_MODE_APSTA || WiFi.getMode() == WIFI_AP ) {
    s = WiFi.softAPIP();
  } else if( WiFi.getMode() == WIFI_MODE_STA && WiFi.status() == WL_CONNECTED ) {
    s = WiFi.localIP();
  }
  return s;
}


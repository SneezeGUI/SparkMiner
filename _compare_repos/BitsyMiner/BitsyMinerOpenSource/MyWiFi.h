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
#ifndef MY_WIFI_INCLUDED_H
#define MY_WIFI_INCLUDED_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <esp_now.h>
#include "defines_n_types.h"

#define INADDR_ANY ((uint32_t)0x0)

typedef void (*MyWiFiEventCallback)(void);

class MyWiFi {

  public:
    static void enterAccessPointMode();
    static void enterStationMode();
    static void setAccessPointInfo(char* apssid, char *password);
    static void setSSIDInfo(char *ssid, char *password);
    static bool isAccessPoint();
    static bool isConnected();
    static bool isConnecting();
    static IPAddress getIP();
    static void configure(uint32_t ipAddress, uint32_t gatewayAddress, uint32_t subnetAddress, uint32_t primaryDNSAddress, uint32_t secondaryDNSAddress);
    static void setupEvents();
    static void setIPCallback(MyWiFiEventCallback callback);

    static int16_t apClientCount();

  private:
    static MyWiFiEventCallback ipCallback;
    static char theSSID[MAX_SSID_LENGTH];
    static char theSSIDPassword[MAX_PASSWORD_LENGTH];
    static char apSSID[MAX_SSID_LENGTH];
    static char apPassword[MAX_PASSWORD_LENGTH];


    static void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

};

#endif
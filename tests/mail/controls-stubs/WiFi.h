#pragma once
#include <Arduino.h>
struct IPAddress{String toString()const{return "192.0.2.43";}};
struct TestWiFi{IPAddress localIP()const{return {};}void disconnect(bool,bool){}};
extern TestWiFi WiFi;

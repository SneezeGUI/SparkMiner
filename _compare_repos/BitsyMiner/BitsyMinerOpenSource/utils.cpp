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
#include <Arduino.h>
#include "esp_mac.h"
#include "defines_n_types.h"
#include "utils.h"
#include <WiFi.h>
#include "MyWiFi.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_log.h"



//////////////////////////////////////////////////////////////////////////////////////////
// Use a 64-bit millis clock
//////////////////////////////////////////////////////////////////////////////////////////
uint64_t bigMillis() {
    static uint32_t low32 = 0, high32 = 0;
    uint32_t new_low32 = millis();
    if( new_low32 < low32 ) high32++;
    low32 = new_low32;
    return (uint64_t) high32 << 32 | low32;
}


void formatBigNumber(char* dest, double n) {
  int dec = 3;
  char fmtString[7];
  char suffix[2] = "";
  
  const double kilo = 1000;
	const double mega = 1000000;
	const double giga = 1000000000;
	const double tera = 1000000000000;
	const double peta = 1000000000000000;
	const double exa  = 1000000000000000000;
	
    if(n >= exa ) { //E
        n /= exa;
        strcpy(suffix, "E");
    }
    else if( n >= exa ) {// P
        n /= peta;
        strcpy(suffix, "P");
    }   
    else if( n >= tera ) {// T
        n /= tera;
        strcpy(suffix, "T");
    }
    else if(n >= giga) { // G
        n /= giga;
        strcpy(suffix, "G");
    } 
    else if( n>= mega) { // M
        n /= mega;
        strcpy(suffix, "M");
    }
    else if( n >= kilo) { // K
        n /= kilo;
        strcpy(suffix, "k");
    } 

    if( n > 999.0 ) {
      dec = 1;
    } else if( n > 99 ) {
      dec = 2;
    } 
    
    sprintf(dest, "%0.3f", n);
    strcat(dest, suffix);
 }

char *safeStrnCpy(char *dest, const char* src, size_t num) {
  strncpy(dest, src, num);
  dest[num - 1] = '\0';
  return dest;
}

double roundToDecimals(double value, int decimals) {
    double factor = 1;
    for (int i = 0; i < decimals; i++) {
        factor *= 10;
    }    
    long temp = (long)(value * factor + (value >= 0 ? 0.5 : -0.5)); // Apply rounding
    return temp / factor; // Convert back to double
}


unsigned char decodeHexChar(char c) {
    switch(c)
    {
        case 'a'...'f':
            return 0x0a + c - 'a';
        case 'A'...'F':
            return 0x0a + c - 'A';
        case '0'...'9':
            return c - '0';
    } 
    return 0;
}

// Convert a string to lower case in place
char* lCaseInPlace(char* buffer) {
  for(int16_t i = 0; buffer[i]; i++) {
    if( buffer[i] >= 'A' && buffer[i] <= 'Z' ) {
      buffer[i] += 0x20;
    }
  }
  return buffer;
}


// Make sure output buffer is 2 * input bytes + 1
void bin2hex(char *out, unsigned char* in, size_t len) {
    int i, j;
    char *tbl = "0123456789ABCDEF";
    for(i = 0, j = 0; i < len; i++) {
        out[j++] = tbl[(in[i] >> 4) & 0x0f];
        out[j++] = tbl[in[i] & 0x0f];        
    }
    out[j] = '\0';
}

char* colorToCode(char *out, uint32_t color) {
    int i, j;
    color = __builtin_bswap32(color);
    unsigned char* in = (unsigned char*) &color;
    char tbl[] = "0123456789ABCDEF";
    out[0] = '#';
    for(i=1, j=1; i < 4; i++) {
      out[j++] = tbl[(in[i] >> 4) & 0x0f];
      out[j++] = tbl[in[i] & 0x0f];   
    }
    out[j] = '\0';
    return out;
}

uint32_t codeToColor(const char* in) {
  uint32_t color = 0;
  unsigned char* cPtr = (unsigned char*) &color;
  if(*in == '#') {
    in++;
  }
  int i = 0;
  while(i++ < 3 && *in && in[1]) {
    uint8_t c = decodeHexChar(*in++) << 4;
    c |= decodeHexChar(*in++);
    *cPtr++ = c;
  }
  return __builtin_bswap32(color) >> 8;
}

void hex2bin(unsigned char *out, const char *in, size_t len) {
    size_t b = 0;
    for(int i = 0; i < len; i+=2) {
        out[b++] = (unsigned char) (decodeHexChar(in[i]) << 4) + decodeHexChar(in[i+1]);
    }
}



void getMacAddress(char* destBuff) {
  uint8_t baseMac[6];
  //esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
  esp_err_t ret = esp_efuse_mac_get_default(baseMac);
  if (ret == ESP_OK) {
    sprintf(destBuff, "%02X:%02X:%02X:%02X:%02X:%02X",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);

  } else {
    dbg("Couldn't get MAC address.\n");
    destBuff[0] = '\0';
  }
}

// Appends the last three characters of the MAC address to our prefix to get the AP name
void buildAccessPointName(char* destBuff) {
  uint8_t baseMac[6];
  char suffix[7];

  esp_err_t ret = esp_efuse_mac_get_default(baseMac);
  if (ret == ESP_OK) {
    sprintf(suffix, "%02X%02X%02X", baseMac[3], baseMac[4], baseMac[5]);
  } else {
    suffix[0] = '\0';
  }
  strcpy(destBuff, AP_SSID);
  strcat(destBuff, suffix);
}

char* versionToString(char* dest, uint32_t version) {
  sprintf(dest, "v%d.%d.%d", version >> 16, (version >> 8) & 0xff, version & 0xff);
  return dest;
}


bool is_leap_year(int16_t year) {
    if (year % 4 == 0) {
        if (year % 100 == 0) {
            if (year % 400 == 0) {
                return true;
            }
            return false;
        }
        return true;
    }
    return false;
}

int16_t getDayOfWeek(int16_t year, int16_t month, int16_t day) {
    return (day
        + ((153 * (month + 12 * ((14 - month) / 12) - 3) + 2) / 5) 
        + (365 * (year + 4800 - ((14 - month) / 12)))
        + ((year + 4800 - ((14 - month) / 12)) / 4)              
        - ((year + 4800 - ((14 - month) / 12)) / 100)             
        + ((year + 4800 - ((14 - month) / 12)) / 400) 
        - 32045 
      ) % 7; // 0 through 6, Monday through Sunday 
}

void dateFromEpoch(Date *theDate, uint32_t epochTime) {

    // Calculate total days since epoch
    int16_t days = epochTime / 86400L;
    int remaining_seconds = epochTime % 86400L;

    // Initialize starting year
    int16_t year = 1970;

    // Calculate current year
    while (1) {
        int16_t days_in_year = is_leap_year(year) ? 366 : 365;
        if (days >= days_in_year) {
            days -= days_in_year;
            year++;
        } else {
            break;
        }
    }

    // Months and days in a regular year
    int16_t days_in_months[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Adjust for leap year
    if (is_leap_year(year)) {
        days_in_months[1] = 29;
    }

    // Calculate current month and day
    int16_t month = 0;
    while (days >= days_in_months[month]) {
        days -= days_in_months[month];
        month++;
    }
    
    theDate->year = year;
    theDate->month = month+1;  // Adjust month to be 1-based (1-12)
    theDate->day = days + 1;  // Adjust day to be 1-based (1-31)

    // Calculate hours, minutes, and seconds
    theDate->hour = remaining_seconds / 3600;
    remaining_seconds %= 3600;
    theDate->minute = remaining_seconds / 60;
    theDate->second = remaining_seconds % 60;
    theDate->dayOfWeek = getDayOfWeek(year, theDate->month, theDate->day);
    theDate->epoch = epochTime;
}


void sendPostData(const char *host, const char* path, unsigned char* data, size_t len, uint16_t port=80) {
  
  WiFiClient client;
  int16_t waitCycles = 10;
 
  // Don't bother if we're not connected or we're in access point mode
  if( MyWiFi::isAccessPoint() || ! WiFi.isConnected() ) {
    return;
  }

  if( client.connect(host, port) ) {
    client.printf("POST %s HTTP/1.0\n", path);
    client.printf("User-Agent: BitsyMiner/1.0\n");
    client.printf("Host: %s\n", host);
    client.printf("Content-Type: application/x-www-form-urlencoded\n");
    client.printf("Content-length: %d\n\n", len);
        
    client.write(data, len);

    while(! client.available() && waitCycles-- > 0) {
      vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    
    // Read headers
    while( client.available() ) {
      client.read();
    }

    client.stop();
  }
}


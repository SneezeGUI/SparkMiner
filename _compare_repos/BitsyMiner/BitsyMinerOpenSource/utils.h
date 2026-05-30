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
#ifndef UTILS_H
#define UTILS_H


typedef struct {
  uint32_t epoch;
  int16_t year;
  int16_t month;
  int16_t day;
  int16_t hour;
  int16_t minute;
  int16_t second;
  int16_t dayOfWeek;
} Date;

uint64_t bigMillis();


char* lCaseInPlace(char* buffer);

unsigned char decodeHexChar(char c);
void hex2bin(unsigned char *out, const char *in, size_t len);
void bin2hex(char* out, unsigned char* in, size_t len);

void formatBigNumber(char* dest, double n);
void getMacAddress(char* destBuff);
void buildAccessPointName(char* destBuff);
char* colorToCode(char *out, uint32_t color);
uint32_t codeToColor(const char* in);
char* versionToString(char* dest, uint32_t version);

void dateFromEpoch(Date *theDate, uint32_t epochTime);
double roundToDecimals(double value, int decimals);

void sendPostData(const char *host, const char* path, unsigned char* data, size_t len, uint16_t port);
char *safeStrnCpy(char *dest, const char* src, size_t num);
bool validateDevice();

#endif
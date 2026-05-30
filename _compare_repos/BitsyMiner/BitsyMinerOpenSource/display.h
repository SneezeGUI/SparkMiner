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
#ifndef DISPLAY_H
#define DISPLAY_H

#if defined(ESP32_2432S028) || defined(ESP32_2432S024)

#define SCREEN_MINING 1
#define SCREEN_ACCESS_POINT 2
#define SCREEN_ONE 3
#define SCREEN_CLOCK 4
#define SCREEN_PHOTOS 5
#define SCREEN_FIRMWARE 99

void initializeDisplay(uint8_t rotation, uint8_t brightness);
void refreshDisplay();
bool screenTouched();
void setBrightness(unsigned long brightness);
void setCurrentScreen(uint8_t screen);
void setRotation(uint8_t rotation);
void redraw();
void handleScreenTouch();

#endif

#endif

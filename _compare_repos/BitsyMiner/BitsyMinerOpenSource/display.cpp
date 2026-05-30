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
#include "defines_n_types.h"
#include "monitor.h"
#include "utils.h"


// This whole source is wrapped, as it was for supporting other display types
#if defined(ESP32_2432S028) || defined(ESP32_2432S024)

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include <PNGdec.h>
#include "my_fonts.h"
#include "display_graphics.h"
#include "esp32-hal-ledc.h"
#include "qrcode.h"
#include "MyWiFi.h"

//<a href="https://www.flaticon.com/free-icons/wifi" title="wifi icons">Wifi icons created by Uniconlabs - Flaticon</a>
//https://www.flaticon.com/free-icon/settings_675780?term=configure&related_id=675780
//<a href="https://www.flaticon.com/free-icons/offline" title="offline icons">Offline icons created by Pixel perfect - Flaticon</a>
//<a href="https://www.flaticon.com/free-icons/pickaxe" title="pickaxe icons">Pickaxe icons created by smashingstocks - Flaticon</a>
//<a href="https://www.flaticon.com/free-icons/no-internet" title="no-internet icons">No-internet icons created by Paul J. - Flaticon</a>
//<a href="https://www.flaticon.com/free-icons/connected" title="connected icons">Connected icons created by Paul J. - Flaticon</a>

// Backlight control
// https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/Examples/Basics/4-BacklightControlTest/4-BacklightControlTest.ino

#if defined(ESP32_2432S028)
  #define LCD_BACK_LIGHT_PIN 21
  // Touchscreen pins
  #define XPT2046_IRQ 36   // T_IRQ
  #define XPT2046_MOSI 32  // T_DIN
  #define XPT2046_MISO 39  // T_OUT
  #define XPT2046_CLK 25   // T_CLK
  #define XPT2046_CS 33    // T_CS
  #define SPI_TOUCH_FREQUENCY 2500000


#elif defined(ESP32_2432S024)

  #define LCD_BACK_LIGHT_PIN 27
  // Touchscreen pins
  #define XPT2046_IRQ 36   // T_IRQ
  #define XPT2046_MOSI 13  // T_DIN
  #define XPT2046_MISO 12  // T_OUT
  #define XPT2046_CLK 14   // T_CLK
  #define XPT2046_CS 33    // T_CS
  #define SPI_TOUCH_FREQUENCY 2500000


#endif

// use first channel of 16 channels (started from zero)
#define LEDC_CHANNEL_0 0
// use 12 bit precission for LEDC timer
#define LEDC_TIMER_12_BIT 12
// use 5000 Hz as a LEDC base frequency
#define LEDC_BASE_FREQ 5000

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FONT_SIZE 2

#define SEG7_FONT_WIDTH 30
#define SEG7_FONT_HEIGHT 48

#define MAX_IMAGE_WIDTH 240            // Adjust for your images
#define BACKGROUND_COLOR 0x108         //getColor(0x04, 0x20, 0x45)
#define BACKGROUND_COLOR_32B 0x452004  // BGR

#define WHITE_32B 0xFFFFFF


#define LINE_START_Y 70
#define TITLE_X 15
#define DATA_X 210
#define LINE_HEIGHT 19
#define DATA_CHAR_WIDTH 11
#define DATA_CHARS 9
#define DATA_SIG_CHARS 3

#define SCREEN_MINING 1
#define SCREEN_ACCESS_POINT 2

#define PORTRAIT_LINE_GAP 4



extern SetupData settings;
extern MonitorData monitorData;

static const char *dataTitles[] = { "Total Hashes", "Best Difficulty", "Pool Jobs", "Pool Targets", "32-bit Targets", "Blocks Found" };

uint8_t currentScreen = SCREEN_MINING;
uint8_t currentScreenOrientation = 0;

TFT_eSPI tft = TFT_eSPI();

// Sprites
TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite db1 = TFT_eSprite(&tft);

TFT_eSprite portraitText = TFT_eSprite(&tft);
TFT_eSprite landscapeText = TFT_eSprite(&tft);


#if defined(ESP32_2432S028) 
SPIClass touchscreenSPI = SPIClass(VSPI); 
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
#elif defined(ESP32_2432S024)
SPIClass touchscreenSPI = SPIClass(HSPI); //SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
#endif

IPAddress lastIPAddress;

typedef struct {
  int16_t x;
  int16_t y;
  uint32_t bgColor;
  TFT_eSPI *tftPtr;
} PNGDrawInfo;

PNG png;  // PNG decoder instance


typedef struct {
  GFXfont *font;
  TFT_eSprite *sprite;
} MonoFontSprite;

bool spritesCreated = false;

//=========================================v==========================================
//                                      pngDraw
//====================================================================================
// This next function will be called during decoding of the png file to
// render each image line to the TFT.  If you use a different TFT library
// you will need to adapt this function to suit.
// Callback function to draw pixels to the display
int pngDraw(PNGDRAW *pDraw) {
  PNGDrawInfo *p = (PNGDrawInfo *)pDraw->pUser;
  uint16_t lineBuffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, p->bgColor);
  p->tftPtr->pushImage(p->x, p->y + pDraw->y, pDraw->iWidth, 1, lineBuffer);
  return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
uint16_t getColor(uint16_t r, uint16_t g, uint16_t b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
uint16_t getColor16(uint32_t color) {
  return ((color >> 19) << 11) | ((color & 0xfc00) >> 5) | ((color & 0xff) >> 3);
}
///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
//#define BACKGROUND_COLOR_32B 0x452004  // BGR
uint32_t getColor32(uint32_t color) {
  return ((color << 24) >> 8) | (color & 0xff00) | (color >> 16);
}

///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void ledcAnalogWrite(uint8_t channel, uint32_t value, uint32_t valueMax = 255) {
  // calculate duty, 4095 from 2 ^ 12 - 1
  uint32_t duty = (4095 / valueMax) * min(value, valueMax);

  // write duty to LEDC
  ledcWrite(channel, duty);
}

///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void refreshAccessPointPage() {

}




///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void showPageOne() {

  lastIPAddress = MyWiFi::getIP();

  bool isLandscape = (currentScreenOrientation % 2);
  int16_t scrWidth = isLandscape ? SCREEN_WIDTH : SCREEN_HEIGHT;
  int16_t scrHeight = isLandscape ? SCREEN_HEIGHT : SCREEN_WIDTH;
  
  uint16_t bg16 = getColor16(settings.backgroundColor);
  uint32_t bg32 = getColor32(settings.backgroundColor);
  uint16_t fg16 = getColor16(settings.foregroundColor);
  uint32_t fg32 = getColor32(settings.foregroundColor);

  tft.fillScreen(bg16);

  tft.setTextColor(fg16, bg16);
  int32_t centerText = scrWidth / 6;
  uint8_t font = 4;

  tft.setTextColor(fg16, bg16);

  tft.setCursor(5, 0, font);
  tft.print("IP Address");

  tft.setCursor(20, 30, font);
  tft.print(lastIPAddress.toString());

  tft.setCursor(5, 60, font);
  tft.print("Subnet Mask");

  tft.setCursor(20, 90, font);
  tft.print(WiFi.subnetMask().toString().c_str());

  tft.setCursor(5, 120, font);
  tft.print("Gateway IP");

  tft.setCursor(20, 150, font);
  tft.print(WiFi.gatewayIP().toString().c_str());

  tft.setCursor(5, 180, font);
  tft.print("DNS Server 1");

  tft.setCursor(20, 210, font);
  tft.print(WiFi.dnsIP().toString().c_str());

  if( ! isLandscape ) {
    tft.setCursor(5, 240, font);
    tft.print("DNS Server 2");

    tft.setCursor(20, 270, font);
    tft.print(WiFi.dnsIP(1).toString().c_str());
  }
  

}


///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void refreshPageOne() {
  if(MyWiFi::getIP() != lastIPAddress ) {
    showPageOne();
  }
}

///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void showAccessPointPage() {
  char mac[30];
  char version[13];
  char apName[30];
  char qrContents[100];
  bool isLandscape = true;  //(currentScreenOrientation % 2);
  int16_t scrWidth = isLandscape ? SCREEN_WIDTH : SCREEN_HEIGHT;
  int16_t scrHeight = isLandscape ? SCREEN_HEIGHT : SCREEN_WIDTH;

  PNGDrawInfo p;

  tft.setRotation(1);
  setBrightness(200);

  // Grab our access point name
  buildAccessPointName(apName);
  getMacAddress(mac);
  versionToString(version, MINING_HARDWARE_VERSION_HEX);

  tft.fillScreen(BACKGROUND_COLOR);
  int rc = png.openFLASH((unsigned char *)transparent_small_logo, sizeof(transparent_small_logo), pngDraw);
  if (rc == PNG_SUCCESS) {
    tft.startWrite();
    p.tftPtr = &tft;
    p.bgColor = BACKGROUND_COLOR_32B;
    p.x = 0;
    p.y = 10;
    rc = png.decode(&p, 0);
    tft.endWrite();
  }

  tft.setTextColor(TFT_WHITE, BACKGROUND_COLOR);

  if (isLandscape) {
    
    tft.setTextFont(2);
    tft.drawString("Scan the QR code or use", 10, 72);
    tft.drawString("the settings below to", 10, 90);
    tft.drawString("connect to the access", 10, 108);
    tft.drawString("point and configure your miner.", 10, 126); 

    tft.setFreeFont(&FreeMonoBold10pt7b);
    tft.drawString("SSID: ", 10, 153);
    tft.drawString("Password: ", 10, 173);
    tft.drawString("IP: ", 10, 193);
    //tft.setTextFont(4);
    tft.drawString(apName, 145, 150);
    tft.drawString(AP_SSID_PASSWORD, 145, 170);

    tft.setTextColor(TFT_WHITE, BACKGROUND_COLOR);
    tft.setTextFont(2);
    tft.drawString(mac, 5, scrHeight - 18);

    tft.drawString(version, scrWidth - 60, scrHeight - 18);

    String ip = MyWiFi::getIP().toString();
    tft.setTextColor(TFT_WHITE, BACKGROUND_COLOR);
    tft.setFreeFont(&FreeMonoBold10pt7b);
    tft.drawString(ip.c_str(), 145, 190);

  } 


  // Draw QRCode
  QRCode qrcode;
  snprintf(qrContents, sizeof(qrContents), "WIFI:S:%s;T:WPA;P:%s;H:false;;", apName, AP_SSID_PASSWORD);

  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, 0, qrContents);

  int16_t qpx, qpy;
  int16_t moduleSize = 90 / qrcode.size;

  if (isLandscape) {
    qpx = scrWidth - 110;
    qpy = 20;
  } else {
    qpx = (scrWidth / 2) - ((qrcode.size * moduleSize) / 2);
    qpy = scrHeight - 115;
  }

  // Draw a border around the QR Code
  tft.fillRoundRect(qpx - 10, qpy - 10, qrcode.size * moduleSize + 20, qrcode.size * moduleSize + 20, 8, TFT_WHITE);

  for (int8_t y = 0; y < qrcode.size; y++) {
    for (int8_t x = 0; x < qrcode.size; x++) {
      uint16_t color = qrcode_getModule(&qrcode, x, y) ? TFT_BLACK : TFT_WHITE;
      tft.fillRect(qpx + (x * moduleSize), qpy + (y * moduleSize), moduleSize, moduleSize, color);
    }
  }
}
///////////////////////////////////////////////////////////////////////////////////////////
//
//
//

///////////////////////////////////////////////////////////////////////////////////////////
void showFirmwarePage() {

  bool isLandscape = (currentScreenOrientation % 2);
  int16_t scrWidth = (currentScreenOrientation % 2) ? SCREEN_WIDTH : SCREEN_HEIGHT;
  int16_t scrHeight = (currentScreenOrientation % 2) ? SCREEN_HEIGHT : SCREEN_WIDTH;

  uint32_t bgOriginal = settings.backgroundColor;
  uint16_t bg16 = getColor16(bgOriginal);
  uint32_t bg32 = getColor32(bgOriginal);
  uint32_t fgOriginal = settings.foregroundColor;
  uint16_t fg16 = getColor16(fgOriginal);
  uint32_t fg32 = getColor32(fgOriginal);

  // Make sure we are how we need to be, just in case they came from an access point page
  tft.setRotation(currentScreenOrientation);

  tft.fillScreen(bg16);

  tft.setTextColor(fg16, bg16);
  tft.setFreeFont(&FreeMonoBold10pt7b);
  tft.drawString("Updating firmware...", 5, 5);
  
  tft.drawRect(0, 40, scrWidth, 20, fg16);

}




///////////////////////////////////////////////////////////////////////////////////////////
//
//
//

///////////////////////////////////////////////////////////////////////////////////////////
void showMiningPage() {

  bool isLandscape = (currentScreenOrientation % 2);
  int16_t scrWidth = (currentScreenOrientation % 2) ? SCREEN_WIDTH : SCREEN_HEIGHT;
  int16_t scrHeight = (currentScreenOrientation % 2) ? SCREEN_HEIGHT : SCREEN_WIDTH;

  uint32_t bgOriginal = settings.backgroundColor;
  uint16_t bg16 = getColor16(bgOriginal);
  uint32_t bg32 = getColor32(bgOriginal);
  uint32_t fgOriginal = settings.foregroundColor;
  uint16_t fg16 = getColor16(fgOriginal);
  uint32_t fg32 = getColor32(fgOriginal);

  PNGDrawInfo p;

  // Make sure we are how we need to be, just in case they came from an access point page
  tft.setRotation(currentScreenOrientation);

  // Create the sprites we need on the first pass through
  if (!spritesCreated) {
    img.createSprite(tft.textWidth("888.88", 7), SEG7_FONT_HEIGHT);
    db1.createSprite(DATA_CHAR_WIDTH * DATA_CHARS, LINE_HEIGHT);
    spritesCreated = true;
  }
  tft.fillScreen(bg16);
  int rc;

  if (isLandscape) {

    tft.drawFastHLine(0, 72, scrWidth, fg16);
    tft.drawFastHLine(0, 122, scrWidth, fg16);
    tft.drawFastHLine(0, 172, scrWidth, fg16);
    //tft.drawFastHLine(0, 252, scrWidth, TFT_WHITE);
    tft.drawFastVLine(scrWidth/3, 72, 100, fg16);
    tft.drawFastVLine((scrWidth/3) * 2, 72, 100, fg16);

    int32_t centerText = scrWidth / 6;
    uint8_t font = 2;

    tft.setTextColor(fg16, bg16);
    tft.setCursor(centerText - (tft.textWidth(dataTitles[0], font) / 2), 76, font);
    tft.print(dataTitles[0]);
    tft.setCursor(centerText - (tft.textWidth(dataTitles[3], font) / 2), 126, font);
    tft.print(dataTitles[3]);

    tft.setCursor(centerText * 3 - (tft.textWidth(dataTitles[1], font) / 2), 76, font);
    tft.print(dataTitles[1]);
    tft.setCursor(centerText * 3- (tft.textWidth(dataTitles[4], font) / 2), 126, font);
    tft.print(dataTitles[4]);

    tft.setCursor(centerText * 5- (tft.textWidth(dataTitles[2], font) / 2), 76, font);
    tft.print(dataTitles[2]);
    tft.setCursor(centerText * 5 - (tft.textWidth(dataTitles[5], font) / 2), 126, font);
    tft.print(dataTitles[5]);


  } else { 
    
    tft.drawFastHLine(0, 72, scrWidth, fg16);
    tft.drawFastHLine(0, 132, scrWidth, fg16);
    tft.drawFastHLine(0, 192, scrWidth, fg16);
    tft.drawFastHLine(0, 252, scrWidth, fg16);
    tft.drawFastVLine(scrWidth/2, 72, 180, fg16);

    int32_t centerText = scrWidth / 4;
    uint8_t font = 2;

    tft.setTextColor(fg16, bg16);
    tft.setCursor(centerText - (tft.textWidth(dataTitles[0], font) / 2), 76, font);
    tft.print(dataTitles[0]);
    tft.setCursor(centerText - (tft.textWidth(dataTitles[2], font) / 2), 136, font);
    tft.print(dataTitles[2]);
    tft.setCursor(centerText - (tft.textWidth(dataTitles[4], font) / 2), 196, font);
    tft.print(dataTitles[4]);

    centerText *= 3;

    tft.setCursor(centerText - (tft.textWidth(dataTitles[1], font) / 2), 76, font);
    tft.print(dataTitles[1]);
    tft.setCursor(centerText - (tft.textWidth(dataTitles[3], font) / 2), 136, font);
    tft.print(dataTitles[3]);
    tft.setCursor(centerText - (tft.textWidth(dataTitles[5], font) / 2), 196, font);
    tft.print(dataTitles[5]);

  }
 
  char vStr[10];
  versionToString(vStr, MINING_HARDWARE_VERSION_HEX);
  tft.setTextColor(fg16, bg16);
  tft.setTextFont(2);
  tft.drawString(vStr, scrWidth - strlen(vStr) * 8, 50);

  rc = png.openFLASH((unsigned char *)transparent_small_logo, sizeof(transparent_small_logo), pngDraw);
  if (rc == PNG_SUCCESS) {
    tft.startWrite();
    p.tftPtr = &tft;
    p.bgColor = bg32;
    p.x = 0;
    p.y = 5;
    rc = png.decode(&p, 0);
    tft.endWrite();
  }

  if (isLandscape) {
    tft.setTextColor(fg16, bg16);
    tft.setCursor(2, scrHeight - 52, 2);
    tft.print("Current");
    tft.setCursor(2, scrHeight - 35, 2);
    tft.print("Hashrate");
  }

  tft.setCursor(scrWidth - 58, scrHeight - 30, 4);
  tft.setTextColor(fg16, bg16);
  tft.print("kH/s");
}


///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void drawIconSet(bool resetFlags) {
  static bool lastMiningStatus = false;
  static bool lastWifiStatus = false;
  static bool haveDrawnWifiStatus = false;
  static bool lastPoolConnectedStatus = false;
  static bool haveDrawnPoolStatus = false;

  bool isLandscape = (currentScreenOrientation % 2);
  int16_t scrWidth = isLandscape ? SCREEN_WIDTH : SCREEN_HEIGHT;
  int16_t scrHeight = isLandscape ? SCREEN_HEIGHT : SCREEN_WIDTH;

  bool miningStatus = monitorData.isMining;
  bool wifiStatus = monitorData.wifiConnected;
  bool poolStatus = monitorData.poolConnected;

  uint32_t bgOriginal = settings.backgroundColor;
  uint16_t bg16 = getColor16(bgOriginal);
  uint32_t bg32 = getColor32(bgOriginal);
  uint32_t fgOriginal = settings.foregroundColor;
  uint16_t fg16 = getColor16(fgOriginal);
  uint32_t fg32 = getColor32(fgOriginal);

  PNGDrawInfo p;

  int16_t tw = tft.textWidth(monitorData.hashesPerSecondStr, 7);
  img.fillSprite(bg16);
  img.setCursor(img.width() - tw, 0, 7);
  img.setTextColor(fg16, bg16);

  int16_t hashRateX = isLandscape ? 70 : 4;
  img.print(monitorData.hashesPerSecondStr);
  img.pushSprite(hashRateX, scrHeight - 52);

  // Decide whether to draw mining status
  if (resetFlags || miningStatus != lastMiningStatus) {

    if (miningStatus) {
      int rc = png.openFLASH((unsigned char *)pickaxe_one, sizeof(pickaxe_one), pngDraw);
      if (rc == PNG_SUCCESS) {
        p.tftPtr = &tft;
        p.bgColor = bg32;
        p.x = scrWidth - 120;
        p.y = 5;
        rc = png.decode(&p, 0);
      }
    } else {
      tft.fillRect(scrWidth - 120, 5, 32, 32, bg16);
    }
    lastMiningStatus = miningStatus;
  }

  if (resetFlags || !haveDrawnPoolStatus || poolStatus != lastPoolConnectedStatus) {
    unsigned char *poolConnectionImage;
    size_t imgSize;

    if (poolStatus) {
      poolConnectionImage = (byte *)pool_connected;
      imgSize = sizeof(pool_connected);
    } else {
      poolConnectionImage = (byte *)pool_disconnected;
      imgSize = sizeof(pool_disconnected);
    }
    int rc = png.openFLASH(poolConnectionImage, imgSize, pngDraw);
    if (rc == PNG_SUCCESS) {
      p.tftPtr = &tft;
      p.bgColor = bg32;
      p.x = scrWidth - 80;
      p.y = 5;
      rc = png.decode(&p, 0);
    }
    lastPoolConnectedStatus = poolStatus;
    haveDrawnPoolStatus = true;
  }

  if (resetFlags || !haveDrawnWifiStatus || wifiStatus != lastWifiStatus) {
    unsigned char *wifiImage;
    size_t imgSize;

    if (wifiStatus) {
      wifiImage = (byte *)wifi;
      imgSize = sizeof(wifi);
    } else {
      wifiImage = (byte *)wifi_slash;
      imgSize = sizeof(wifi_slash);
    }

    int rc = png.openFLASH(wifiImage, imgSize, pngDraw);
    if (rc == PNG_SUCCESS) {
      p.tftPtr = &tft;
      p.bgColor = bg32;
      p.x = scrWidth - 40;
      p.y = 5;
      tft.startWrite();
      rc = png.decode(&p, 0);
      tft.endWrite();
    }

    lastWifiStatus = wifiStatus;
    haveDrawnWifiStatus = true;
  }  
}


void printSpriteString(const char *text, int16_t x, int16_t y, const GFXfont *theFont = &overpass_mono_bold9pt7b, int16_t fontHeight = 0) {

  static MonoFontSprite fonts[4] = {};

  static int16_t fontCount = 0;

  uint16_t bg16 = getColor16(settings.backgroundColor);
  uint16_t fg16 = getColor16(settings.foregroundColor);

  int16_t i, fontIndex = -1;

  // See if we already have this font
  for(i = 0; i < fontCount; i++) {
    if( fonts[i].font == theFont ) {
      fontIndex = i;
      break;
    }
  }

  // Add the font
  if( fontIndex < 0 && fontCount < 4 ) {
    fonts[fontCount].font = (GFXfont*) theFont;
    fonts[fontCount].sprite = new TFT_eSprite(&tft);
    fonts[fontCount].sprite->setFreeFont(theFont);
    if( fontHeight == 0 ) {
      fontHeight = fonts[fontCount].sprite->fontHeight();
    }
    fonts[fontCount].sprite->createSprite(fonts[fontCount].sprite->textWidth("X"), fontHeight);
    if( fonts[fontCount].sprite->created() ) {
      fontIndex = fontCount++;
    }
  }

  // If we still don't have a font sprite, then get out
  if( fontIndex < 0 ) {
    return;
  }

  char sBuf[] = {'\0', '\0'};
  i = 0;

  while(text[i]) {
    sBuf[0] = text[i++];
    fonts[fontIndex].sprite->setTextColor(fg16, bg16);
    fonts[fontIndex].sprite->fillSprite(bg16);
    fonts[fontIndex].sprite->drawString(sBuf, 0, 0);
    fonts[fontIndex].sprite->pushSprite(x, y);
    x += fonts[fontIndex].sprite->width();
  }


}

///////////////////////////////////////////////////////////////////////////////////////////
//
//
//

///////////////////////////////////////////////////////////////////////////////////////////
void refreshClockPage(bool resetFlags) {

  static uint32_t lastDate = 0;
  static int16_t lastDayOfWeek = -1;
  static bool last24setting = settings.clock24;
  static int16_t lastTime = -1;

  static char *days[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
  static char *months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

  bool isLandscape = (currentScreenOrientation % 2);
  int16_t scrWidth = isLandscape ? SCREEN_WIDTH : SCREEN_HEIGHT;
  int16_t scrHeight = isLandscape ? SCREEN_HEIGHT : SCREEN_WIDTH;
  int16_t x;

  uint32_t bgOriginal = settings.backgroundColor;
  uint16_t bg16 = getColor16(bgOriginal);
  uint32_t bg32 = getColor32(bgOriginal);
  uint32_t fgOriginal = settings.foregroundColor;
  uint16_t fg16 = getColor16(fgOriginal);
  uint32_t fg32 = getColor32(fgOriginal);

  // static TFT_eSprite landscapeClockDigit = TFT_eSprite(&tft);
  // static TFT_eSprite portraitClockDigit = TFT_eSprite(&tft);

  const GFXfont *textFont = &overpass_mono_bold9pt7b;
  const int16_t textFontHeight = 20;

  drawIconSet(resetFlags);

  Date d;
  dateFromEpoch(&d, monitorData.currentTime + settings.utcOffset);

  const GFXfont *theFont;
  char tBuf[12];
  char dBuf[32];

  if( settings.clock24 ) {
    sprintf(tBuf, "%02d:%02d", d.hour, d.minute);
  } else {
    uint16_t dHours = d.hour > 12 ? d.hour - 12 : d.hour;
    dHours = dHours ? dHours : 12;
    sprintf(tBuf, "%02d:%02d %s", dHours, d.minute, (d.hour >= 12 ? "PM" : "AM"));
  }

  // Force a refresh if the clock format has changed
  if( last24setting != settings.clock24 ) {
    resetFlags = true;
  }

  if( resetFlags || (d.hour * 60 + d.minute) != lastTime ) {
    
    if (isLandscape) {

      theFont = &overpass_mono_bold32pt7b;

      tft.setFreeFont(theFont);
      x = (scrWidth / 2) - (tft.textWidth(tBuf) / 2);
      
      if(! settings.clock24 && tBuf[0] == '0' ) {
          tBuf[0] = ' ';
      }

      if( last24setting != settings.clock24 ) {
        tft.fillRect(0, 110, scrWidth, 55, bg16);
      }
      printSpriteString(tBuf, x, 110, theFont, 55); // Adjust font height because it's crazy big

        
      if( d.month >= 1 && d.month <= 12 && d.day >=1 && d.day <= 31 ) {        
        uint32_t dateCode = (d.year - 1970) << 9 | (d.month << 5) | d.day; 

        if( resetFlags || dateCode != lastDate ) {
          tft.setFreeFont(&overpass_mono_bold9pt7b);
          tft.fillRect(0, 80, scrWidth, tft.fontHeight(), bg16);

          sprintf(dBuf, "%s, %s %d, %4d", days[d.dayOfWeek], months[d.month-1], d.day, d.year);
          x = (scrWidth / 2) - (tft.textWidth(dBuf) / 2);
          printSpriteString(dBuf, x, 80, textFont, textFontHeight);
        }
        lastDate = dateCode;
      }

      
    } else {
     
      theFont = &overpass_mono_bold24pt7b;

      tft.setFreeFont(theFont);
      x = (scrWidth / 2) - (tft.textWidth(tBuf) / 2);
      if( ! settings.clock24 && tBuf[0] == '0' ) {
          tBuf[0] = ' ';
      }

      if( last24setting != settings.clock24 ) {
        tft.fillRect(0, 150, scrWidth, 40, bg16);
      }

      printSpriteString(tBuf, x, 150, theFont, 40); // Adjust font size from default

        
      if( d.month >= 1 && d.month <= 12 && d.day >=1 && d.day <= 31 ) {        
        uint32_t dateCode = (d.year - 1970) << 9 | (d.month << 5) | d.day; 

        if( resetFlags || dateCode != lastDate ) {
          tft.setFreeFont(&overpass_mono_bold9pt7b);
          tft.fillRect(0, 85, scrWidth, (110 + tft.fontHeight()) - 85, bg16);

          sprintf(dBuf, "%s", days[d.dayOfWeek]);
          x = (scrWidth / 2) - (tft.textWidth(dBuf) / 2);
          printSpriteString(dBuf, x, 85, textFont, textFontHeight);

          sprintf(dBuf, "%s %d, %4d", months[d.month-1], d.day, d.year);
          x = (scrWidth / 2) - (tft.textWidth(dBuf) / 2);
          printSpriteString(dBuf, x, 110, textFont, textFontHeight);
        }
        lastDate = dateCode;
      }
              

    }
     

  }

  lastTime = (d.hour * 60 + d.minute);
  last24setting = settings.clock24;
  //Serial.println("leaving screen refresh");
}


///////////////////////////////////////////////////////////////////////////////////////////
//
//
//

///////////////////////////////////////////////////////////////////////////////////////////
void showClockPage() {

  bool isLandscape = (currentScreenOrientation % 2);
  int16_t scrWidth = (currentScreenOrientation % 2) ? SCREEN_WIDTH : SCREEN_HEIGHT;
  int16_t scrHeight = (currentScreenOrientation % 2) ? SCREEN_HEIGHT : SCREEN_WIDTH;

  uint32_t bgOriginal = settings.backgroundColor;
  uint16_t bg16 = getColor16(bgOriginal);
  uint32_t bg32 = getColor32(bgOriginal);
  uint32_t fgOriginal = settings.foregroundColor;
  uint16_t fg16 = getColor16(fgOriginal);
  uint32_t fg32 = getColor32(fgOriginal);

  PNGDrawInfo p;

  // Make sure we are how we need to be, just in case they came from an access point page
  tft.setRotation(currentScreenOrientation);

  // Create the sprites we need on the first pass through
  if (!spritesCreated) {
    img.createSprite(tft.textWidth("888.88", 7), SEG7_FONT_HEIGHT);
    db1.createSprite(DATA_CHAR_WIDTH * DATA_CHARS, LINE_HEIGHT);
    spritesCreated = true;
  }

  tft.fillScreen(bg16);
  int rc;

  if (isLandscape) {
    tft.drawFastHLine(0, 172, scrWidth, fg16);
  } else { 
    tft.drawFastHLine(0, 252, scrWidth, fg16);
  }
 
  char vStr[10];
  versionToString(vStr, MINING_HARDWARE_VERSION_HEX);
  tft.setTextColor(fg16, bg16);
  tft.setTextFont(2);
  tft.drawString(vStr, scrWidth - strlen(vStr) * 8, 50);

  rc = png.openFLASH((unsigned char *)transparent_small_logo, sizeof(transparent_small_logo), pngDraw);
  if (rc == PNG_SUCCESS) {
    tft.startWrite();
    p.tftPtr = &tft;
    p.bgColor = bg32;
    p.x = 0;
    p.y = 5;
    rc = png.decode(&p, 0);
    tft.endWrite();
  }

  if (isLandscape) {
    tft.setTextColor(fg16, bg16);
    tft.setCursor(2, scrHeight - 52, 2);
    tft.print("Current");
    tft.setCursor(2, scrHeight - 35, 2);
    tft.print("Hashrate");
  }

  tft.setCursor(scrWidth - 62, scrHeight - 30, 4);
  tft.setTextColor(fg16, bg16);
  tft.print("kH/s");
}


///////////////////////////////////////////////////////////////////////////////////////////
//
//
//

///////////////////////////////////////////////////////////////////////////////////////////
// Returns the location of the period in a string
int16_t periodLoc(char *str) {
  int16_t i;
  for (i = 0; str[i] && str[i] != '.'; i++)
    ;
  return i;
}

///////////////////////////////////////////////////////////////////////////////////////////
//
//
//

///////////////////////////////////////////////////////////////////////////////////////////
void refreshMiningScreen(bool resetFlags) {

  static uint32_t lastTotalJobs = 0xffffffff;
  static uint32_t lastPoolSubs = 0xffffffff;
  static uint32_t last32 = 0xffffffff;
  static uint32_t lastvb = 0xffffffff;
  static double lastbd = -2.0;

  bool isLandscape = (currentScreenOrientation % 2);
  int16_t scrWidth = isLandscape ? SCREEN_WIDTH : SCREEN_HEIGHT;
  int16_t scrHeight = isLandscape ? SCREEN_HEIGHT : SCREEN_WIDTH;

  static int32_t lColPos[6][2]= {{53 - ((DATA_CHAR_WIDTH * DATA_CHARS) / 2), 102}, {159 - ((DATA_CHAR_WIDTH * DATA_CHARS) / 2), 102}, {265 - ((DATA_CHAR_WIDTH * DATA_CHARS) / 2), 102}, 
    {53 - ((DATA_CHAR_WIDTH * DATA_CHARS) / 2), 152}, {159 - ((DATA_CHAR_WIDTH * DATA_CHARS) / 2), 152}, {265 - ((DATA_CHAR_WIDTH * DATA_CHARS) / 2), 152} };
  static int32_t pColPos[6][2]= {{10, 105}, {130, 105}, {10, 165}, {130, 165}, {10, 225}, {130, 225} };

  uint32_t bgOriginal = settings.backgroundColor;
  uint16_t bg16 = getColor16(bgOriginal);
  uint32_t bg32 = getColor32(bgOriginal);
  uint32_t fgOriginal = settings.foregroundColor;
  uint16_t fg16 = getColor16(fgOriginal);
  uint32_t fg32 = getColor32(fgOriginal);

  drawIconSet(resetFlags);

  const GFXfont *theFont = &WhiteRabbit_Regular10pt7b;
  db1.setFreeFont(theFont);
  db1.setTextColor(fg16, bg16);

  // Put sprites on the screen
  int16_t dataX = isLandscape ? scrWidth - 110 : scrWidth - 95;
  int16_t gap = isLandscape ? 0 : PORTRAIT_LINE_GAP;
  int16_t ls = isLandscape ? LINE_START_Y : LINE_START_Y + 15;

  int16_t hw = (db1.width() / 2);
  

  int32_t (*colPos)[6][2] = isLandscape ? &lColPos : &pColPos;
  db1.fillSprite(bg16);
  db1.drawString(monitorData.totalHashesStr, (DATA_CHAR_WIDTH * DATA_SIG_CHARS) - (periodLoc(monitorData.totalHashesStr) * DATA_CHAR_WIDTH), 0);
  db1.pushSprite((*colPos)[0][0], (*colPos)[0][1]);
  

  if( resetFlags || (monitorData.bestDifficultyStr[0] && lastbd != monitorData.bestDifficulty) ) {
    db1.fillSprite(bg16);
    db1.drawString(monitorData.bestDifficultyStr, (DATA_CHAR_WIDTH * DATA_SIG_CHARS) - (periodLoc(monitorData.bestDifficultyStr) * DATA_CHAR_WIDTH), 0);
    db1.pushSprite((*colPos)[1][0], (*colPos)[1][1]);
    if( monitorData.bestDifficultyStr[0] ) {
      lastbd = monitorData.bestDifficulty;
    }
  }
  if( resetFlags || (monitorData.poolSubmissionsStr[0] && lastPoolSubs != monitorData.poolSubmissions) ) {
    db1.fillSprite(bg16);
    db1.drawString(monitorData.poolSubmissionsStr, (DATA_CHAR_WIDTH * DATA_SIG_CHARS) - (periodLoc(monitorData.poolSubmissionsStr) * DATA_CHAR_WIDTH), 0);
    db1.pushSprite((*colPos)[3][0], (*colPos)[3][1]);
    if(monitorData.poolSubmissionsStr[0]) {
      lastPoolSubs = monitorData.poolSubmissions;
    }
  }
      
  if( resetFlags || (monitorData.totalJobsStr[0] && lastTotalJobs != monitorData.totalJobs) ) {
    db1.fillSprite(bg16);
    db1.drawString(monitorData.totalJobsStr, (DATA_CHAR_WIDTH * DATA_SIG_CHARS) - (periodLoc(monitorData.totalJobsStr) * DATA_CHAR_WIDTH), 0);
    db1.pushSprite((*colPos)[2][0], (*colPos)[2][1]);
    if( monitorData.totalJobsStr[0] ) {
      lastTotalJobs = monitorData.totalJobs;
    }
  }

  if( resetFlags || (monitorData.blocks32FoundStr[0] && last32 != monitorData.blocks32Found) ) {
    db1.fillSprite(bg16);
    db1.drawString(monitorData.blocks32FoundStr, (DATA_CHAR_WIDTH * DATA_SIG_CHARS) - (periodLoc(monitorData.blocks32FoundStr) * DATA_CHAR_WIDTH), 0);
    db1.pushSprite((*colPos)[4][0], (*colPos)[4][1]);
    if(monitorData.blocks32FoundStr[0]) {
      last32 = monitorData.blocks32Found;
    }
  }
  if( resetFlags || (monitorData.validBlocksFoundStr[0] && lastvb != monitorData.validBlocksFound) ) {
    db1.fillSprite(bg16);
    db1.drawString(monitorData.validBlocksFoundStr, (DATA_CHAR_WIDTH * DATA_SIG_CHARS) - (periodLoc(monitorData.validBlocksFoundStr) * DATA_CHAR_WIDTH), 0);
    db1.pushSprite((*colPos)[5][0], (*colPos)[5][1]); 
    if( monitorData.validBlocksFoundStr[0] ) {
      lastvb = monitorData.validBlocksFound;   
    }    
  }


}

///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void setBrightness(unsigned long brightness) {

  #if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAnalogWrite(LEDC_CHANNEL_0, brightness);
  #else
    ledcWrite(LCD_BACK_LIGHT_PIN, brightness * 16);
  #endif
}


///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
// Called by Main to see if the screen is currently
// being touched
bool screenTouched() {
  #if defined(ESP32_2432S028) || defined(ESP32_2432S024)
    return touchscreen.touched();
  #else
    Point touch = touchscreen.getTouch();
    if( touch.x != 0 || touch.y != 0 ) {
      return true;
    }
    return false;
  #endif

}


///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
// Redraws whatever the current screen is
void refreshDisplay() {
  switch (currentScreen) {
    case SCREEN_MINING:
      refreshMiningScreen(false);
      break;
    case SCREEN_ACCESS_POINT:
      // No need to refresh that page currently
      refreshAccessPointPage();
      break;
    case SCREEN_ONE:
      refreshPageOne();
      break;
    case SCREEN_CLOCK:
      refreshClockPage(false);
      break;

  }

}

///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void redraw() {

  // Start fresh with inversion or not
  tft.invertDisplay(settings.invertColors);

  switch (currentScreen) {
    case SCREEN_MINING:
      showMiningPage();
      refreshMiningScreen(true);
      break;
    case SCREEN_ACCESS_POINT:
      showAccessPointPage();
      refreshAccessPointPage();
      break;
    case SCREEN_ONE:
      showPageOne();
      refreshPageOne();
      break;
    case SCREEN_FIRMWARE:
      showFirmwarePage();
      break;
    case SCREEN_CLOCK:
      showClockPage();
      refreshClockPage(true);
      break;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
// Set the current screen and switch
void setCurrentScreen(uint8_t screen) {
  currentScreen = screen;
  redraw();
}
///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void setRotation(uint8_t rotation) {
  currentScreenOrientation = rotation;
  tft.setRotation(rotation);
  //touchscreen.setRotation(rotation);  
}

// Called if main decides we need to do
// something with a screen touch
void handleScreenTouch() {
  switch( currentScreen ) {
    case SCREEN_MINING:
      setCurrentScreen(SCREEN_ONE);
      break;
    case SCREEN_ONE:
      setCurrentScreen(SCREEN_CLOCK);
      break;
    case SCREEN_CLOCK:
      setCurrentScreen(SCREEN_MINING);      
      break;
  }

}


///////////////////////////////////////////////////////////////////////////////////////////
//
//
//
///////////////////////////////////////////////////////////////////////////////////////////
void initializeDisplay(uint8_t rotation, uint8_t brightness) {

  currentScreenOrientation = rotation;

  #if defined(ESP32_2432S028) || defined(ESP32_2432S024)
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchscreenSPI);
    touchscreen.setRotation(rotation);
  #else
    touchscreen.begin();
  #endif

  //ts.begin();

  // Set the Touchscreen rotation in landscape mode
  //Note: in some displays, the touchscreen might be upside down, so you might need to set the rotation to 3: touchscreen.setRotation(3);
  

  // Start the tft display
  tft.init();
  
  tft.setRotation(rotation);

  // Invert colors for some reason
  tft.invertDisplay(settings.invertColors);

  // Recommended setting on CYD - https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/cyd.md
  // tft.writecommand(ILI9341_GAMMASET); //Gamma curve selected
  // tft.writedata(2);
  // delay(120);
  // tft.writecommand(ILI9341_GAMMASET); //Gamma curve selected
  // tft.writedata(1);

  // Setting up the LEDC and configuring the Back light pin
  // NOTE: this needs to be done after tft.init()
// ledcAttachChannel(LCD_BACK_LIGHT_PIN, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT, LEDC_CHANNEL_0);


#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
  ledcAttachPin(LCD_BACK_LIGHT_PIN, LEDC_CHANNEL_0);
#else
  ledcAttach(LCD_BACK_LIGHT_PIN, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
#endif

  setBrightness(brightness);

}
#endif
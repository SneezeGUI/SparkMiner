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
#include <WiFi.h>
#include <WebServer.h>
#include <CustomJWT.h>

#include "miner.h"
#include "monitor.h"
#include "defines_n_types.h"
#include "MyWebServer.h"
#include "stratum.h"
#include "nvs_handler.h"
#include "MyWiFi.h"
#include "utils.h"


#include "PlainWebSocket.h"


// Function prototypes as necessary
void handleStatus();


extern MonitorData monitorData;
extern SetupData settings;
extern QueueHandle_t stratumMessageQueueHandle;
extern QueueHandle_t appMessageQueueHandle;

#define TEMP_BUFFER_SIZE 2048

WebServer server(80);
PlainWebSocket webSocket(81);

static char temp[TEMP_BUFFER_SIZE];

static const char trueString[] = "true";
static const char falseString[] = "false";

#define SEND_PAGE_HEADER server.sendContent_P(pageHeader);sprintf(temp, PSTR(pageHeader1), (settings.webTheme == 1 ? "dark-mode" : ""), MINING_HARDWARE_VERSION_HEX, (settings.enableLogViewer ? "" : " noshow"));server.sendContent(temp);
#define SEND_PAGE_FOOTER server.sendContent_P(pageFooter);


char jwtHeader[50];
char jwtPayload[400];
char jwtSignature[50];
char jwtOut[404] = "sid=";
char jwtKey[32];

CustomJWT jwt(jwtKey, jwtHeader, sizeof(jwtHeader), jwtPayload, sizeof(jwtPayload), jwtSignature, sizeof(jwtSignature), jwtOut, sizeof(jwtOut));

const char* headersToCollect[] = { "Cookie" };

void addToWebLog(const char* logMessage, size_t length) {
  webSocket.broadcast(logMessage, length);
}

void addToWebLog(const char* color, const char* logMessage) {
  webSocket.broadcast(color, logMessage, strlen(logMessage));
}

void addToWebLog(const char* logMessage) {
  webSocket.broadcast(logMessage, strlen(logMessage));
}

void addToWebLog(String& logMessage) {
  webSocket.broadcast(logMessage);
}

//////////////////////////////////////////////////////////////////////////////////////////
// Clear out miner statistics
//////////////////////////////////////////////////////////////////////////////////////////
void clearMinerStats() {

  monitorData.totalHashes = 0;
  monitorData.bestDifficulty = 0;
  monitorData.totalJobs = 0;
  monitorData.poolSubmissions = 0;
  monitorData.blocks32Found = 0;
  monitorData.blocks16Found = 0;
  monitorData.validBlocksFound = 0;
  
  saveMonitorData();

}


void submitJob(const char* jobId, uint32_t timestamp, uint32_t nonce, const char* extraNonce2) {
  
  //submit(jobId, extraNonce2, hb->timestamp, hb->nonce);
  jobSubmitQueueEntry qe;

  strncpy(qe.jobId, jobId, MAX_JOB_ID_LENGTH);
  strncpy(qe.extraNonce2, extraNonce2, 20);
  qe.timestamp = timestamp;
  qe.nonce = nonce;
  //stratumSubmitMessages.addMessage(&qe, 150);
  xQueueSend(stratumMessageQueueHandle, &qe, pdMS_TO_TICKS(100));

}

// We'll just generate a new key all the time to keep anyone from reusing an old one
void generateRandomJWTKey() {
  const char keyChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$-=%!@#^&*()";
  for (int i = 0; i < sizeof(jwtKey) - 1; i++) {
    jwtKey[i] = keyChars[random(sizeof(keyChars))];
  }
  jwtKey[sizeof(jwtKey) - 1] = '\0';
}


char* escape_html(const char* input) {
  // Calculate the length of the output string
  size_t length = 0;
  const char* ptr = input;
  while (*ptr) {
    switch (*ptr) {
      case '&':
        length += 5;  // &amp;
        break;
      case '<':
        length += 4;  // &lt;
        break;
      case '>':
        length += 4;  // &gt;
        break;
      case '"':
        length += 6;  // &quot;
        break;
      case '\'':
        length += 5;  // &#39;
        break;
      default:
        length += 1;
        break;
    }
    ptr++;
  }

  // Allocate memory for the escaped string
  char* escaped = (char*)malloc(length + 1);
  if (escaped == NULL) {
    return NULL;  // Memory allocation failed
  }

  // Fill the escaped string
  char* dest = escaped;
  ptr = input;
  while (*ptr) {
    switch (*ptr) {
      case '&':
        strcpy(dest, "&amp;");
        dest += 5;
        break;
      case '<':
        strcpy(dest, "&lt;");
        dest += 4;
        break;
      case '>':
        strcpy(dest, "&gt;");
        dest += 4;
        break;
      case '"':
        strcpy(dest, "&quot;");
        dest += 6;
        break;
      case '\'':
        strcpy(dest, "&#39;");
        dest += 5;
        break;
      default:
        *dest = *ptr;
        dest += 1;
        break;
    }
    ptr++;
  }

  // Null-terminate the escaped string
  *dest = '\0';

  return escaped;
}

String getCookie(const char* cookieName) {
  String cookieValue = "";
  if (server.hasHeader("Cookie")) {  // Check if the "Cookie" header is present in the request
    String cookies = server.header("Cookie");
    int start = cookies.indexOf(String(cookieName) + "=");  // Find the position of the cookie name in the header
    if (start != -1) {
      start += strlen(cookieName) + 1;        // Move the start position to the beginning of the value
      int end = cookies.indexOf(';', start);  // Find the position of the semicolon or the end of the header
      if (end == -1) {
        end = cookies.length();
      }
      cookieValue = cookies.substring(start, end);  // Extract the cookie value
    }
  }
  return cookieValue;
}

void redirect(String location) {
  String header = "Location";
  server.sendHeader(header, location);
  server.send(302, "Found", "");
}

void configRedirect() {
  redirect("http://192.168.4.1/config");
}


// Validate that the user is logged in
bool validateJWT() {
  // Get cookie
  String cval = getCookie("sid");
  if (cval.length() && jwt.decodeJWT((char*)cval.c_str()) == 0) {
    return true;
  }
  return false;
}

void refreshLoginCookie() {

  // Always generate a new key
  generateRandomJWTKey();

  char buffer[30];
  snprintf(jwtPayload, sizeof(buffer), "{\"time\": %ul}", millis());
  jwt.encodeJWT(buffer);

  String header = "Set-Cookie";
  String cookie = "sid=";
  cookie.concat(jwtOut);
  cookie.concat("; HttpOnly; Max-Age=3600");
  server.sendHeader(header, cookie, false);
}

void handleAccessPointMode() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  SEND_PAGE_HEADER

  server.sendContent_P(accessPointHomepage);

  // Send footer
  SEND_PAGE_FOOTER;
}

void handleRoot() {


  if (MyWiFi::isAccessPoint()) {
    handleAccessPointMode();
  } else {
    handleStatus();
  }
}

void showConfigScreen(const char* messages) {

  char versionString[20];
  char fgColor[9];
  char bgColor[9];

  char* sOption = " SELECTED";  // Point option here for selected option
  char* nsOption = "";          // and here for not selected

  char* selOptionPtrs[8];  // and use these pointers in sprintf

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  char* username = escape_html(settings.htaccessUser);
  char* ssid = escape_html(settings.ssid);
  char* wallet = escape_html(settings.wallet);
  char* poolUrl = escape_html(settings.poolUrl);
  char* poolPassword = escape_html(settings.poolPassword);
  char* ntpServer = escape_html(settings.ntpServer);
  char* backupWallet = escape_html(settings.backupWallet);
  char* backupPoolUrl = escape_html(settings.backupPoolUrl);
  char* backupPoolPassword = escape_html(settings.backupPoolPassword);

  // Send header
  SEND_PAGE_HEADER;
  server.sendContent_P(configPageTitle);

  // Display any messages
  if (messages) {
    snprintf(temp, TEMP_BUFFER_SIZE,
             "<div class=\"c\"><div class=\"row message-area\">%s</div></div>", messages);
    server.sendContent(temp);
  }

    // Show User Settings
  snprintf(temp, TEMP_BUFFER_SIZE,
           "<div class=\"c nbpad\">\
  <div class=\"row\">\
  <button class=\"accordion\">User Settings</button>\
  <div class=\"panel\">\
    <form id=\"frmUser\">\
    <div class=\"row\">\
      Username\
      <input class=\"card w-100 lower\" id=\"user\" type=\"text\" name=\"username\" value=\"%s\" autocorrect=\"off\" autocapitalize=\"none\">\
    </div>\
    <div class=\"row\" style=\"position:relative\">\
      Password\
      <input class=\"card w-100 capsWatch\" id=\"password\" type=\"password\" name=\"password\" placeholder=\"Never shown\" data-caps-indicator=\"capsInd1\">\
      <div id=\"capsInd1\" style=\"position:absolute;right:30px;top:-6px;color:red;display:none\">Caps Lock</div>\
    </div>\
    <div class=\"row hint\">\
      Password must be between eight and 32 characters long and contain at least one number, one lowercase character, one uppercase character, and one special character.\
    </div>\
    <div class=\"row\" style=\"position:relative\">\
      Confirm Password\
      <input class=\"card w-100 capsWatch\" id=\"confirmPassword\" type=\"password\" name=\"confirmPassword\" data-caps-indicator=\"capsInd3\">\
      <div id=\"capsInd3\" style=\"position:absolute;right:30px;top:-6px;color:red;display:none\">Caps Lock</div>\
    </div>\
    <div class=\"row\">\
      <input id=\"btnUserUpdate\" class=\"btn\" type=\"button\" value=\"Update User\">\
    </div>\
    </form>\
  </div><!--End panel//-->\
  </div><!--End row//-->\
  </div><!--End c//-->",
           username);
  server.sendContent(temp);

  if( strlen(settings.htaccessPassword) >= MIN_PASSWORD_LENGTH ) {
    // Network settings
    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"c nbpad\">\
    <div class=\"row\">\
    <button class=\"accordion\">Network Settings</button>\
    <div class=\"panel\">\
    <form id=\"frmNetwork\">\
    <div class=\"row\">\
      WiFi SSID\ 
      <div class=\"rel\">\   
        <input class=\"card w-100\" id=\"ssid\" type=\"text\" name=\"ssid\" value=\"%s\" autocorrect=\"off\" autocapitalize=\"none\">\
        <input type=\"button\" id=\"btnScanNetworks\" class=\"btn inside-button\" value=\"Scan\">\
      </div>\
    </div>\
    <div class=\"row\" style=\"position:relative\">\
      WiFi Password\    
      <input class=\"card w-100 capsWatch\" id=\"ssidPassword\" type=\"password\" name=\"ssidPassword\" autocomplete=\"off\" placeholder=\"Never shown\" data-caps-indicator=\"capsInd2\">\
      <div id=\"capsInd2\" style=\"position:absolute;right:30px;top:-6px;color:red;display:none\">Caps Lock</div>\
      <input type=\"checkbox\" id=\"showWifiPassword\">&nbsp;<span style=\"font-size:.8em;margin-left:5px;\">Show Password</span>\
    </div>", ssid);
    server.sendContent(temp);

    if (settings.staticIp) {
      selOptionPtrs[0] = nsOption;
      selOptionPtrs[1] = sOption;
    } else {
      selOptionPtrs[0] = sOption;
      selOptionPtrs[1] = nsOption;
    }

    // Use Static IP
    snprintf(temp, TEMP_BUFFER_SIZE,
      "<div class=\"row\">\
      Static IP Address\    
      <select class=\"card w-100\" name=\"staticIp\" id=\"staticIp\">\
      <option value=\"false\"%s>No</value>\
      <option value=\"true\"%s>Yes</value>\
      </select>\
    </div>", selOptionPtrs[0], selOptionPtrs[1]);
    server.sendContent(temp);
    
    IPAddress ip1(settings.ipAddress), ip2(settings.gateway);

    snprintf(temp, TEMP_BUFFER_SIZE,
    "<div id=\"staticNetworkInfo\"><div class=\"row\">\
      IP Address\    
      <input class=\"card w-100\" id=\"ipAddress\" type=\"text\" name=\"ipAddress\" value=\"%s\">\
    </div>\
    <div class=\"row\">\
      Gateway\    
      <input class=\"card w-100\" id=\"gateway\" type=\"text\" name=\"gateway\" value=\"%s\">\
    </div>", ip1.toString().c_str(), ip2.toString().c_str());
    server.sendContent(temp);

    
    IPAddress ip3(settings.subnet), ip4(settings.primaryDNS), ip5(settings.secondaryDNS);
    snprintf(temp, TEMP_BUFFER_SIZE,
    "<div class=\"row\">\
      Subnet\    
      <input class=\"card w-100\" id=\"subnet\" type=\"text\" name=\"subnet\" value=\"%s\">\
    </div>\
    <div class=\"row\">\
      Primary DNS\    
      <input class=\"card w-100\" id=\"primaryDNS\" type=\"text\" name=\"primaryDNS\" value=\"%s\">\
    </div>\   
    <div class=\"row\">\
      Secondary DNS\    
      <input class=\"card w-100\" id=\"secondaryDNS\" type=\"text\" name=\"secondaryDNS\" value=\"%s\">\
    </div>\ 
    </div>\<!--End static network section-->\
    <div class=\"row\">\
      <input class=\"btn\" id=\"btnNetworkUpdate\" type=\"button\" value=\"Update Network Settings\">\
    </div>\  
    </form>\
    </div><!--End panel//-->\
    </div><!--End row//-->\
    </div><!--End c//-->", ip3.toString().c_str(), ip4.toString().c_str(), ip5.toString().c_str());
    
    server.sendContent(temp);




    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"c nbpad\">\
    <div class=\"row\">\
    <button class=\"accordion\">Mining Settings</button>\
    <div class=\"panel\">\
    <form id=\"frmMining\">\
    <div class=\"tabs\">\
    <input type=\"radio\" id=\"tab1\" name=\"tabs\" checked>\
    <label for=\"tab1\">Primary</label>\
    <input type=\"radio\" id=\"tab2\" name=\"tabs\">\
    <label for=\"tab2\">Backup</label>\    
    <div class=\"tab-content content1\">\
    <div class=\"row tab-header\">Primary Pool Settings</div>\
    <div class=\"row\">\
      Mining Pool Server\    
      <input class=\"card w-100\" id=\"poolUrl\" type=\"text\" name=\"poolUrl\" value=\"%s\"> \
    </div>\
    <div class=\"row\">\
      Mining Pool Port\    
      <input class=\"card w-100\" id=\"poolPort\" type=\"text\" name=\"poolPort\" value=\"%d\">\
    </div>\
    <div class=\"row\">\
      Mining Pool Password\    
      <input class=\"card w-100\" id=\"poolPassword\" type=\"text\" name=\"poolPassword\" value=\"%s\">\
    </div>\ 
    <div class=\"row\">\
      Wallet Address\ 
      <div class=\"rel\">\ 
        <input class=\"card w-100\" id=\"wallet\" type=\"text\" name=\"wallet\" value=\"%s\">\
        <input type=\"button\" id=\"btnValidateWallet\" class=\"btn inside-button\" value=\"Validate\">\
      </div>\
    </div>\
    </div><!--End tab 1//-->",
            poolUrl, settings.poolPort, poolPassword, wallet);
    server.sendContent(temp);

    snprintf(temp, TEMP_BUFFER_SIZE,
    "<div class=\"tab-content content2\">\
    <div class=\"row tab-header\">Backup Pool Settings</div>\
    <div class=\"row\">\
      Mining Pool Server\    
      <input class=\"card w-100\" id=\"backupPoolUrl\" type=\"text\" name=\"backupPoolUrl\" value=\"%s\"> \
    </div>\
    <div class=\"row\">\
      Mining Pool Port\    
      <input class=\"card w-100\" id=\"backupPoolPort\" type=\"text\" name=\"backupPoolPort\" value=\"%d\">\
    </div>\
    <div class=\"row\">\
      Mining Pool Password\    
      <input class=\"card w-100\" id=\"backupPoolPassword\" type=\"text\" name=\"backupPoolPassword\" value=\"%s\">\
    </div>\ 
    <div class=\"row\">\
      Wallet Address\ 
      <div class=\"rel\">\ 
        <input class=\"card w-100\" id=\"backupWallet\" type=\"text\" name=\"backupWallet\" value=\"%s\">\
        <input type=\"button\" id=\"btnValidateBackupWallet\" class=\"btn inside-button\" value=\"Validate\">\
      </div>\
    </div>\
    </div><!--End tab 2//-->\
    </div><!--End tabs//-->",
            backupPoolUrl, settings.backupPoolPort, backupPoolPassword, backupWallet);
    server.sendContent(temp);    

    if (settings.randomizeTimestamp) {
      selOptionPtrs[0] = nsOption;
      selOptionPtrs[1] = sOption;
    } else {
      selOptionPtrs[0] = sOption;
      selOptionPtrs[1] = nsOption;
    }

    // Randomize Timestamps
    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"row\">\
      Randomize Block Timestamps\    
      <select class=\"card w-100\" name=\"randomizeTimestamp\" id=\"randomizeTimestamp\">\
      <option value=\"false\"%s>No</value>\
      <option value=\"true\"%s>Yes</value>\
      </select>\
    </div>",
            selOptionPtrs[0], selOptionPtrs[1]);
    server.sendContent(temp);

    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"row\">\
      <input class=\"btn\" id=\"btnMiningUpdate\" type=\"button\" value=\"Update Mining Settings\">\
    </div>\ 
    </form>\
    </div><!--End panel//-->\
    </div><!--End row//-->\
    </div><!--End c//-->");
    server.sendContent(temp);



    // Items below may depend on device

  #if defined(ESP32_2432S028) || defined(ESP32_2432S024)

    uint8_t srArray[] = { 0, 1, 2, 3 };
    for (int8_t i = 0; i < 4; i++) {
      if (settings.screenRotation == srArray[i]) {
        selOptionPtrs[i] = sOption;
      } else {
        selOptionPtrs[i] = nsOption;
      }
    }

    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"c nbpad\">\
    <div class=\"row\">\
    <button class=\"accordion\">Display Settings</button>\
    <div class=\"panel\">\
    <form id=\"frmDisplay\" onsubmit=\"return false;\">");

    server.sendContent(temp);


    snprintf(temp, TEMP_BUFFER_SIZE,
    "<div class=\"row\">\
      Screen Rotation\    
      <select class=\"card w-100\" name=\"screenRotation\" id=\"screenRotation\">\
      <option value=\"0\"%s>Portrait: Cable at bottom</value>\
      <option value=\"1\"%s>Landscape: Cable at right</value>\
      <option value=\"2\"%s>Portrait: Cable at top</value>\
      <option value=\"3\"%s>Landscape: Cable at left</value>\
      </select>\
    </div>",
            selOptionPtrs[0], selOptionPtrs[1], selOptionPtrs[2], selOptionPtrs[3]);
    server.sendContent(temp);

    // Then set the selected option
    uint8_t optArray[] = { 0, 24, 64, 128, 192, 255 };
    for (int8_t i = 1; i < 6; i++) {
      if (settings.screenBrightness == optArray[i]) {
        selOptionPtrs[i] = sOption;
      } else {
        selOptionPtrs[i] = nsOption;
      }
    }

    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"row\">\
      Screen Brightness\    
      <select class=\"card w-100\" name=\"screenBrightness\" id=\"screenBrightness\">\
      <option value=\"24\"%s>10%%</option>\
      <option value=\"64\"%s>25%%</option>\
      <option value=\"128\"%s>50%%</option>\
      <option value=\"192\"%s>75%%</option>\
      <option value=\"255\"%s>100%</option>\
      </select>\    
    </div>",
            selOptionPtrs[1], selOptionPtrs[2], selOptionPtrs[3], selOptionPtrs[4], selOptionPtrs[5]);
    server.sendContent(temp);

    uint32_t optArray1[] = { 0, 30000, 60000, 120000, 300000, 600000, 1800000 };
    for (int8_t i = 0; i < 7; i++) {
      if (settings.inactivityTimer == optArray1[i]) {
        selOptionPtrs[i] = sOption;
      } else {
        selOptionPtrs[i] = nsOption;
      }
    }
    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"row\">\
      Screen Inactivity Timer\    
      <select id=\"inactivityTimer\" class=\"card w-100\" name=\"inactivityTimer\">\
      <option value=\"0\"%s>No timer</option>\
      <option value=\"30000\"%s>30 seconds</option>\
      <option value=\"60000\"%s>1 minute</option>\
      <option value=\"120000\"%s>2 minutes</option>\
      <option value=\"300000\"%s>5 minutes</option>\
      <option value=\"600000\"%s>10 minutes</option>\
      <option value=\"1800000\"%s>30 minutes</option>\
      </select>\    
    </div>",
            selOptionPtrs[0], selOptionPtrs[1], selOptionPtrs[2], selOptionPtrs[3], selOptionPtrs[4], selOptionPtrs[5], selOptionPtrs[6]);
    server.sendContent(temp);

    for (int8_t i = 0; i < 6; i++) {
      if (settings.inactivityBrightness == optArray[i]) {
        selOptionPtrs[i] = sOption;
      } else {
        selOptionPtrs[i] = nsOption;
      }
    }
    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"row\">\
      Screen Inactivity Brightness\    
      <select class=\"card w-100\" name=\"inactivityBrightness\" id=\"inactivityBrightness\">\
      <option value=\"0\"%s>Screen off</option>\
      <option value=\"24\"%s>10%%</option>\
      <option value=\"64\"%s>25%%</option>\
      <option value=\"128\"%s>50%%</option>\
      <option value=\"192\"%s>75%%</option>\
      <option value=\"255\"%s>100%</option>\
      </select>\    
    </div>",
            selOptionPtrs[0], selOptionPtrs[1], selOptionPtrs[2], selOptionPtrs[3], selOptionPtrs[4], selOptionPtrs[5]);
    server.sendContent(temp);


    snprintf(temp, TEMP_BUFFER_SIZE,
      "<div class=\"row\">\
      Foreground Color\
      <input class=\"w-100 colorbox\" id=\"foregroundColor\" type=\"color\" name=\"foregroundColor\" value=\"%s\">\
      <a href=\"#\" onclick=\"setDefaultForegroundColor();return false;\">Set default</a>\
      </div>\
      <div class=\"row\">\
      Background Color\
      <input class=\"w-100 colorbox\" id=\"backgroundColor\" type=\"color\" name=\"backgroundColor\" value=\"%s\">\
      <a href=\"#\" onclick=\"setDefaultBackgroundColor();return false;\">Set default</a>\
      </div>",    
        colorToCode(fgColor, settings.foregroundColor), colorToCode(bgColor, settings.backgroundColor));
    server.sendContent(temp);      

    if (settings.invertColors) {
      selOptionPtrs[0] = nsOption;
      selOptionPtrs[1] = sOption;
    } else {
      selOptionPtrs[0] = sOption;
      selOptionPtrs[1] = nsOption;
    }

    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"row\">\
      Invert Colors\    
      <select class=\"card w-100\" name=\"invertColors\" id=\"invertColors\">\
      <option value=\"false\"%s>No</option>\
      <option value=\"true\"%s>Yes</option>\
      </select>\    
    </div>",
            selOptionPtrs[0], selOptionPtrs[1]);
    server.sendContent(temp);


    snprintf(temp, TEMP_BUFFER_SIZE,
      "<div class=\"row\">\
      <input class=\"btn\" id=\"btnDisplayUpdate\" type=\"button\" value=\"Update Display Settings\">\
    </div>\  
    </form>\
    </div><!--End panel//-->\
    </div><!--End row//-->\
    </div><!--End c//-->");
    server.sendContent(temp);


    snprintf(temp, TEMP_BUFFER_SIZE,
    "<div class=\"c nbpad\">\
    <div class=\"row\">\
    <button class=\"accordion\">LED Settings</button>\
    <div class=\"panel\">\
    <form id=\"frmLED\" onsubmit=\"return false;\">");
    server.sendContent(temp);

    snprintf(temp, TEMP_BUFFER_SIZE, 
      "<div class=\"row\">\
      LED Red Level\
      <input class=\"w-100\" id=\"led1red\" type=\"range\" name=\"led1red\" min=\"0\" max=\"255\" value=\"%d\">\
      </div>\
      <div class=\"row\">\
      LED Green Level\
      <input class=\"w-100\" id=\"led1green\" type=\"range\" name=\"led1green\" min=\"0\" max=\"255\" value=\"%d\">\
      </div>\
      <div class=\"row\">\
      LED Blue Level\
      <input class=\"w-100\" id=\"led1blue\" type=\"range\" name=\"led1blue\" min=\"0\" max=\"255\" value=\"%d\">\
      </div>"      
    , settings.led1red, settings.led1green, settings.led1blue);

    server.sendContent(temp);

    snprintf(temp, TEMP_BUFFER_SIZE,
      "<div class=\"row\">\
      <input class=\"btn\" id=\"btnLEDUpdate\" type=\"button\" value=\"Update LED Settings\">\
    </div>\  
    </form>\
    </div><!--End panel//-->\
    </div><!--End row//-->\
    </div><!--End c//-->");
    server.sendContent(temp);

  #endif // End of ESP28 settings




    snprintf(temp, TEMP_BUFFER_SIZE,
    "<div class=\"c nbpad\">\
    <div class=\"row\">\
    <button class=\"accordion\">General Settings</button>\
    <div class=\"panel\">\
    <form id=\"frmGeneral\" onsubmit=\"return false;\">");
    server.sendContent(temp);

    snprintf(temp, TEMP_BUFFER_SIZE,
    "<div class=\"row\">Web Theme\
    <select class=\"card w-100\" name=\"webTheme\" id=\"webTheme\">\
    <option value=\"0\"%s>Standard</value>\
    <option value=\"1\"%s>Dark</value>\
    </select>\
    </div>", (settings.webTheme == 0 ? sOption : nsOption), (settings.webTheme == 1 ? sOption : nsOption));
    server.sendContent(temp);


    snprintf(temp, TEMP_BUFFER_SIZE,
      "<div class=\"row\">\
      Time Server (NTP)\
      <input class=\"card w-100\" id=\"ntpServer\" type=\"text\" name=\"ntpServer\" value=\"%s\" placeholder=\"Time server\">\
      </div>", ntpServer
    );
    server.sendContent(temp);

    int32_t offsetHours = settings.utcOffset / 3600;
    int32_t offsetMinutes = (settings.utcOffset % 3600) / 60;
    if( offsetMinutes < 0 ) {
      offsetMinutes *= -1;
    }

    //Serial.printf("%d %d\n", offsetHours, offsetMinutes);

    snprintf(temp, TEMP_BUFFER_SIZE,
      "<div class=\"row\">\
      <div class=\"w-100\">Timezone Offset</div>\
      <select class=\"card\" name=\"utcOffsetHours\" id=\"utcOffsetHours\" style=\"margin-right:10px\">");
    server.sendContent(temp);
    
    for(int16_t i = -11; i < 15; i++) {
      sprintf(temp, "<option value=\"%d\"%s>%d hours</option>", i, (i == offsetHours ? sOption :nsOption), i);
      server.sendContent(temp);
    }

    sprintf(temp, "</select><select class=\"card\" name=\"utcOffsetMinutes\" id=\"utcOffsetMinutes\">\
      <option value=\"0\"%s>0 minutes</option>\
      <option value=\"30\"%s>30 minutes</option>\
      <option value=\"45\"%s>45 minutes</option>\
      </select></div>", (0 == offsetMinutes ? sOption :nsOption), (30 == offsetMinutes ? sOption :nsOption), (45 == offsetMinutes ? sOption :nsOption));

    server.sendContent(temp);

    snprintf(temp, TEMP_BUFFER_SIZE,
      "<div class=\"row\">\
      Clock Format\
      <select class=\"card w-100\" id=\"clock24\" name=\"clock24\">\
      <option value=\"false\"%s>12-hour</option>\
      <option value=\"true\"%s>24-hour</option>\
      </select>\
      </div>", (settings.clock24 ? nsOption : sOption), (settings.clock24 ? sOption : nsOption)
    );
    server.sendContent(temp);

    sprintf(temp, 
      "<div class=\"row\">\
      <input class=\"btn\" id=\"btnGeneralUpdate\" type=\"button\" value=\"Update General Settings\">\
      </div>\  
      </form>\
      </div><!--End panel//-->\
      </div><!--End row//-->\
      </div><!--End c//-->"
    );
    server.sendContent(temp);

    // Advanced settings
    snprintf(temp, TEMP_BUFFER_SIZE,
    "<div class=\"c nbpad\">\
    <div class=\"row\">\
    <button class=\"accordion\">Advanced Settings</button>\
    <div class=\"panel\">\
    <form id=\"frmAdvanced\" onsubmit=\"return false;\">");
    server.sendContent(temp);


    // Disable Core 0
  #ifndef SINGLE_CORE
   if (settings.coreZeroDisabled) {
      selOptionPtrs[0] = nsOption;
      selOptionPtrs[1] = sOption;
    } else {
      selOptionPtrs[0] = sOption;
      selOptionPtrs[1] = nsOption;
    }

    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"row\">\
      Reduce Mining CPU Load\    
      <select class=\"card w-100\" name=\"coreZeroDisabled\" id=\"coreZeroDisabled\">\
      <option value=\"false\"%s>No</value>\
      <option value=\"true\"%s>Yes</value>\
      </select>\
    </div>\
    <div class=\"row hint\">\
     Reduces mining impact on the CPU, enabling better performance for non-mining tasks at the expense of hash rate.\
    </div>",  selOptionPtrs[0], selOptionPtrs[1]);
    server.sendContent(temp);
  #endif

    snprintf(temp, TEMP_BUFFER_SIZE,
      "<div class=\"row\">\
      Clear Statistics\    
      <input type=\"text\" maxlength=\"4\" class=\"card w-100\" name=\"clearStats\" id=\"clearStats\">\
    </div>\
    <div class=\"row hint\">\
     To clear miner statistics, type \"YES\" in the box above and update the advanced settings.\
    </div>",  selOptionPtrs[0], selOptionPtrs[1]);
    server.sendContent(temp);

   if (settings.enableLogViewer) {
      selOptionPtrs[0] = nsOption;
      selOptionPtrs[1] = sOption;
    } else {
      selOptionPtrs[0] = sOption;
      selOptionPtrs[1] = nsOption;
    }

    snprintf(temp, TEMP_BUFFER_SIZE,
            "<div class=\"row\">\
      Enable Log Viewer\    
      <select class=\"card w-100\" name=\"enableLogViewer\" id=\"enableLogViewer\">\
      <option value=\"false\"%s>No</value>\
      <option value=\"true\"%s>Yes</value>\
      </select>\
    </div>\
    <div class=\"row hint\">\
     Allows you to see communication logs in in real time. Logs may contain wallets and pool passwords. \
    </div>",  selOptionPtrs[0], selOptionPtrs[1]);
    server.sendContent(temp);



    sprintf(temp, 
      "<div class=\"row\">\
      <input class=\"btn\" id=\"btnAdvancedUpdate\" type=\"button\" value=\"Update Advanced Settings\">\
      </div>\  
      </form>\
      </div><!--End panel//-->\
      </div><!--End row//-->\
      </div><!--End c//-->"
    );
    server.sendContent(temp);



  } // end if good password set


  server.sendContent_P(sha256Javascript);
  server.sendContent_P(configPageJavascript);
  SEND_PAGE_FOOTER;


  if (username) {
    free(username);
  }
  if (ssid) {
    free(ssid);
  }
  if (poolUrl) {
    free(poolUrl);
  }
  if (poolPassword) {
    free(poolPassword);
  }
  if (wallet) {
    free(wallet);
  }
  if( backupPoolUrl ) {
    free(backupPoolUrl);
  }
  if( backupPoolPassword )  {
    free(backupPoolPassword);
  }
  if( backupWallet ) {
    free(backupWallet);
  }
  if(ntpServer) {
    free(ntpServer);
  }
}

void handleConfigGet() {

  // Make sure the user is logged in
  if (!validateJWT()) {
    redirect("/login");
    return;
  }

  // Keep them guessing and keep them logged in
  refreshLoginCookie();

  showConfigScreen(NULL);
}

void sendAjaxResponse(bool successful, bool changesApplied, bool loggedIn, const char* message) {
  char es = '\0';
  const char* error = successful ? falseString : trueString;
  const char* li = loggedIn ? trueString : falseString;
  const char* changes = changesApplied ? trueString : falseString;
  const char* msg = message ? message : (const char*)&es;
  const char* lrm = strlen(settings.htaccessPassword) >= MIN_PASSWORD_LENGTH ? trueString : falseString;
  snprintf(temp, TEMP_BUFFER_SIZE,
           "{\"error\": %s, \"changesApplied\": %s, \"loggedIn\": %s, \"lrm\": %s, \"message\": \"%s\"}", error, changes, li, lrm, msg);

  server.send(200, "application/json", temp);
}

void handleConfigPost() {

  // Make sure the user is logged in
  if (!validateJWT()) {
    sendAjaxResponse(false, false, false, "User is not logged in.");
    return;
  }

  // Used to send messages back to main application
  ApplicationMessage am;
  uint16_t mainAction = 0;

  // Keep them guessing and keep them logged in
  refreshLoginCookie();

  // Make a copy of the current settings
  SetupData newSettings;
  memcpy(&newSettings, &settings, sizeof(SetupData));

  bool error = false;
  //bool rebootRequired = false;
  //bool networkReconnectRequired = false;
  //bool redrawRequired = false;
  bool stratumReconnectRequired = false;
  bool changesMade = false;

  char messages[200] = "";

  String section = server.arg("section");

  // Make sure that a section name is given
  if (!section || !section.length()) {
    sendAjaxResponse(false, false, true, "Section not defined");
    return;
  }

  if( strcmp(section.c_str(), "advanced") == 0 ) {


    String coreZeroDisabled = server.arg("coreZeroDisabled");
    String clStats = server.arg("clearStats");
    String enableLogViewer = server.arg("enableLogViewer");

    bool bv = false;
    
    bv = (strcmp(enableLogViewer.c_str(), "true") == 0) ? true : false;
    if( settings.enableLogViewer != bv ) {
      newSettings.enableLogViewer = bv;
      changesMade = true;
    }

    if( clStats.equalsIgnoreCase("YES") ) {
      clearMinerStats();
      changesMade = true;
    }
    

  #ifndef SINGLE_CORE
    bool discz = false;
    if(strcmp(coreZeroDisabled.c_str(), "true") == 0) {
      discz = true;
    }
    if(settings.coreZeroDisabled != discz) {
      newSettings.coreZeroDisabled = discz;
      changesMade = true;
    }
  #endif
    
  } else if (strcmp(section.c_str(), "user") == 0) {

    String username = server.arg("username");
    String password = server.arg("password");
    String confirmPassword = server.arg("confirmPassword");

    if (username.length() == 0 || password.length() == 0 || confirmPassword.length() == 0) {
      strcpy(messages, "All information is required.");
      error = true;
    } else if (password != confirmPassword) {
      strcpy(messages, "Passwords do not match.");
      error = true;
    } else if (username.length() > MAX_USERNAME_LENGTH) {
      strcpy(messages, "Username too long.");
      error = true;
    } else if (password.length() > MAX_PASSWORD_LENGTH) {
      strcpy(messages, "Password too long.");
      error = true;
    } else if(password.length() < MIN_PASSWORD_LENGTH) {
      strcpy(messages, "Password is too short.");
      error = true;
    } else {
      strncpy(newSettings.htaccessUser, username.c_str(), MAX_USERNAME_LENGTH);
      strncpy(newSettings.htaccessPassword, password.c_str(), MAX_PASSWORD_LENGTH);
      changesMade = true;
    }
  } else if (strcmp(section.c_str(), "mining") == 0) {


    String poolUrl = server.arg("poolUrl");
    String poolPort = server.arg("poolPort");
    String wallet = server.arg("wallet");
    String randomizeTimestamp = server.arg("randomizeTimestamp");
    String poolPassword = server.arg("poolPassword");
    String backupPoolUrl = server.arg("backupPoolUrl");
    String backupPoolPort = server.arg("backupPoolPort");
    String backupWallet = server.arg("backupWallet");
    String backupPoolPassword = server.arg("backupPoolPassword");

    if (!poolUrl || poolUrl.length() == 0) {
      strcpy(messages, "A valid pool server is required.");
      error = true;
    } else if (strcmp(settings.poolUrl, poolUrl.c_str())) {
      strncpy(newSettings.poolUrl, poolUrl.c_str(), MAX_POOL_URL_LENGTH);
      stratumReconnectRequired = true;
      changesMade = true;
    }


    if (!poolPort || poolPort.length() == 0) {
      strcpy(messages, "A valid mining pool port is required.");
      error = true;
    } else {
      int port = poolPort.toInt();
      if (port < 0 || port > 65535) {
        strcpy(messages, "Mining pool port out of range.");
        error = true;
      } else if (settings.poolPort != port) {
        newSettings.poolPort = port;
        stratumReconnectRequired = true;
        changesMade = true;
      }
    }

    if (!error) {
      if (!wallet || wallet.length() == 0) {
        strcpy(messages, "A valid bitcoin wallet is required.");
        error = true;
      } else if (strcmp(settings.wallet, wallet.c_str()) ) {
        strncpy(newSettings.wallet, wallet.c_str(), MAX_WALLET_LENGTH);
        stratumReconnectRequired = true;
        changesMade = true;
      }
    }

    if( ! error ) {

      if( poolPassword.length() == 0 ) {
        strcpy(messages, "A pool password is required.");
        error = true;
      } else if( strcmp(settings.poolPassword, poolPassword.c_str()) ) {
        strncpy(newSettings.poolPassword, poolPassword.c_str(), MAX_POOL_PASSWORD_LENGTH);
        stratumReconnectRequired = true;
        changesMade = true;
      }
    }

    if( ! error ) {
      if( strcmp(settings.backupPoolUrl, backupPoolUrl.c_str()) ) {
        safeStrnCpy(newSettings.backupPoolUrl, backupPoolUrl.c_str(), MAX_POOL_URL_LENGTH);
        changesMade = true;
      }
    }  

    if( ! error ) {
      if( backupPoolPort.length() ) {
        int backupPort = backupPoolPort.toInt();
        if( backupPort < 0 || backupPort > 65535 ) {
          strcpy(messages, "Backup mining pool port out of range.");
          error = true;
        } else {
          newSettings.backupPoolPort = backupPort;
          changesMade = true;
        }
      }
    }

    if( ! error ) {
       if ( strcmp(settings.backupWallet, backupWallet.c_str()) ) {
        safeStrnCpy(newSettings.backupWallet, backupWallet.c_str(), MAX_WALLET_LENGTH);
        changesMade = true;
      }     
    }

    if( ! error ) {
      if( strcmp(settings.backupPoolPassword, backupPoolPassword.c_str()) ) {
        safeStrnCpy(newSettings.backupPoolPassword, backupPoolPassword.c_str(), MAX_POOL_PASSWORD_LENGTH);
        changesMade = true;
      }
    }    

    if (!error) {
      if (!randomizeTimestamp || randomizeTimestamp.length() == 0) {
        strcpy(messages, "Randomize timestamps setting is missing.");
        error = true;
      } else {
        bool rts = false;
        if (strcmp(randomizeTimestamp.c_str(), "true") == 0) {
          rts = true;
        }
        if (settings.randomizeTimestamp != rts) {
          newSettings.randomizeTimestamp = rts;
          changesMade = true;
        }
      }
    }

  } else if (strcmp(section.c_str(), "network") == 0) {

    String ssid = server.arg("ssid");
    String ssidPassword = server.arg("ssidPassword");
    String gateway = server.arg("gateway");
    String ipAddress = server.arg("ipAddress");
    String subnet = server.arg("subnet");
    String primaryDNS = server.arg("primaryDNS");
    String secondaryDNS = server.arg("secondaryDNS");
    String staticIp = server.arg("staticIp");

    bool isStaticIP = false;

    if( staticIp.length() ) {
      if( strcmp(staticIp.c_str(), "true") == 0 ) {
        isStaticIP = true;
      }
      if( settings.staticIp != isStaticIP ) {
        newSettings.staticIp = isStaticIP;
        changesMade = true;
      }
    }

    // Deal with all of the fields that come with a static IP
    if( !error && isStaticIP ) {

      IPAddress ip;
      if( ! error ) {
        if( ipAddress.length() && ip.fromString(ipAddress.c_str()) ) {
          if( settings.ipAddress != (uint32_t) ip ) {
            newSettings.ipAddress = (uint32_t) ip;
            changesMade = true;
          }
        } else {
          strcpy(messages, "Invalid IP address.");
          error = true;
        }        
      }
      
      if( ! error ) {
        if( gateway.length() > 0 ) {
          if( ip.fromString(gateway.c_str()) ) {
            if( settings.gateway != (uint32_t) ip ) {
              newSettings.gateway = (uint32_t) ip;
              changesMade = true;
            }
          } else {
            strcpy(messages, "Invalid gateway address.");
            error = true;
          }
        } else {
          ip = INADDR_NONE;
          if( settings.gateway != (uint32_t) ip ) {
            newSettings.gateway = (uint32_t) ip;
            changesMade = true;
          }
        }
      }

      if( ! error ) {
        if( subnet.length() > 0 ) {
          if( ip.fromString(subnet.c_str()) ) {            
            if( settings.subnet != (uint32_t) ip ) {
              newSettings.subnet = (uint32_t) ip;
              changesMade = true;
            }
          } else {
            strcpy(messages, "Invalid subnet address.");
            error = true;
          }
        } else {
          ip = INADDR_NONE;
          if( settings.subnet != (uint32_t) ip ) {
            newSettings.subnet = (uint32_t) ip;
            changesMade = true;
          }
        }
      }

      if( ! error ) {
        if( primaryDNS.length() > 0 ) {
          if( ip.fromString(primaryDNS.c_str()) ) {
            if( settings.primaryDNS != (uint32_t) ip ) {
              newSettings.primaryDNS = (uint32_t) ip;
              changesMade = true;
            }
          } else {
            strcpy(messages, "Invalid primary DNS address.");
            error = true;
          }
        } else {
          ip = INADDR_NONE;
          if( settings.primaryDNS != (uint32_t) ip ) {
            newSettings.primaryDNS = (uint32_t) ip;
            changesMade = true;
          }
        }
      }

      if( ! error ) {
        if( secondaryDNS.length() > 0 ) {
          if( ip.fromString(secondaryDNS.c_str()) ) {
            if( settings.secondaryDNS != (uint32_t) ip ) {
              newSettings.secondaryDNS = (uint32_t) ip;
              changesMade = true;
            }
          } else {
            strcpy(messages, "Invalid secondary DNS address.");
            error = true;
          }        
        } else {
          ip = INADDR_NONE;
          if( settings.secondaryDNS != (uint32_t) ip ) {
            newSettings.secondaryDNS = (uint32_t) ip;
            changesMade = true;
          }
        }
      }
    }

    if (!error && ssid && ssid.length() > 0) {
      if (strcmp(ssid.c_str(), settings.ssid) != 0) {
        strncpy(newSettings.ssid, ssid.c_str(), MAX_SSID_LENGTH);
        changesMade = true;
      }
    }
    
    if (!error && ssidPassword && ssidPassword.length() > 0) {
      strncpy(newSettings.ssidPassword, ssidPassword.c_str(), MAX_PASSWORD_LENGTH);
      changesMade = true;
    }

    if(! error && changesMade ) {
      mainAction |= MAIN_ACTION_NETWORK_CONNECT;
      mainAction |= MAIN_ACTION_GOTO_MAIN_SCREEN;
    }

  } else if (strcmp(section.c_str(), "display") == 0) {


    String screenRotation = server.arg("screenRotation");
    String screenBrightness = server.arg("screenBrightness");
    String inactivityBrightness = server.arg("inactivityBrightness");
    String inactivityTimer = server.arg("inactivityTimer");
    String foregroundColor = server.arg("foregroundColor");
    String backgroundColor = server.arg("backgroundColor");
    String invertColors = server.arg("invertColors");

    if (!error) {
      if (!screenRotation || screenRotation.length() == 0) {
        strcpy(messages, "Screen rotation setting is missing.");
        error = true;
      } else {
        uint8_t sr = screenRotation.toInt();
        if (sr > 3) {
          sr = 3;
        }
        if (settings.screenRotation != sr) {
          newSettings.screenRotation = sr;
          mainAction |= MAIN_ACTION_REDRAW;
          mainAction |= MAIN_ACTION_SET_ROTATION;
          //redrawRequired = true;
          changesMade = true;
        }
      }
    }


    if (!error) {
      if (!screenBrightness || screenBrightness.length() == 0) {
        strcpy(messages, "Screen brightness setting is missing.");
        error = true;
      } else {
        uint32_t sb = screenBrightness.toInt();
        if (sb != settings.screenBrightness) {
          newSettings.screenBrightness = sb;
          changesMade = true;
          mainAction |= MAIN_ACTION_SET_BRIGHTNESS;
        }
      }
    }



    if (!error) {
      if (!inactivityBrightness || inactivityBrightness.length() == 0) {
        strcpy(messages, "Inactivity brightness setting is missing.");
        error = true;
      } else {
        uint32_t ib = inactivityBrightness.toInt();
        if (ib != settings.inactivityBrightness) {
          newSettings.inactivityBrightness = ib;
          changesMade = true;
        }
      }
    }


    if (!error) {
      if (!inactivityTimer || inactivityTimer.length() == 0) {
        strcpy(messages, "Inactivity timer setting is missing.");
        error = true;
      } else {
        uint32_t it = inactivityTimer.toInt();
        if (it != settings.inactivityTimer) {
          newSettings.inactivityTimer = it;
          changesMade = true;
        }
      }
    }

    if(!error) {
      if(foregroundColor.length() && backgroundColor.length()) {
        uint32_t fc = codeToColor(foregroundColor.c_str());
        uint32_t bc = codeToColor(backgroundColor.c_str());
        if( fc != settings.foregroundColor || bc != settings.backgroundColor ) {
          newSettings.foregroundColor = fc;
          newSettings.backgroundColor = bc;
          mainAction |= MAIN_ACTION_REDRAW;
          changesMade = true;
        }
      }
    }

    if(! error ) {
      if( invertColors.length() ) {
        bool ivc = (bool) (strcmp(invertColors.c_str(), "true") == 0);
        if( ivc != settings.invertColors ) {
          newSettings.invertColors = ivc;
          mainAction |= MAIN_ACTION_REDRAW;
          changesMade = true;
        }
      }
    }

  } else if (strcmp(section.c_str(), "general") == 0) {

    String ntpServer = server.arg("ntpServer");
    String utcOffsetHours = server.arg("utcOffsetHours");
    String utcOffsetMinutes = server.arg("utcOffsetMinutes");
    String clock24 = server.arg("clock24");
    String webTheme = server.arg("webTheme");

    if( webTheme.length() ) {
      uint8_t wt = webTheme.toInt();
      if( wt <= 1 && wt != settings.webTheme ) {
        newSettings.webTheme = wt;
        changesMade = true;
      }
    }
    

    if( strcmp(settings.ntpServer, ntpServer.c_str()) ) {
        strncpy(newSettings.ntpServer, ntpServer.c_str(), MAX_POOL_URL_LENGTH);
        changesMade = true;
    }

    newSettings.utcOffset = (utcOffsetHours.toInt() * 3600);
    if( newSettings.utcOffset < 0) {
      newSettings.utcOffset -= (utcOffsetMinutes.toInt() * 60);
    } else {
      newSettings.utcOffset += (utcOffsetMinutes.toInt() * 60);
    }

    if( settings.utcOffset != newSettings.utcOffset ) {
      changesMade = true;
    }

    if (strcmp(clock24.c_str(), "true") == 0) {
      newSettings.clock24 = true;
    } else {
      newSettings.clock24 = false;
    }

    if( newSettings.clock24 != settings.clock24 ) {
      changesMade = true;
    }

  } else if (strcmp(section.c_str(), "lighting") == 0) {

    String led1red = server.arg("led1red");
    String led1green = server.arg("led1green");
    String led1blue = server.arg("led1blue");

    if( ! error ) {
      if( led1red.length() && led1green.length() && led1blue.length() ) {
        newSettings.led1red = led1red.toInt();
        newSettings.led1green = led1green.toInt();
        newSettings.led1blue = led1blue.toInt();
        if( newSettings.led1red != settings.led1red || newSettings.led1blue != settings.led1blue || newSettings.led1green != settings.led1green ) {
          changesMade = true;
          mainAction |= MAIN_ACTION_LED1_SET;
        }
      }
    }

  }



  if (!error) {

    if (changesMade) {
      // Tell stratum to stop its activities
      if (stratumReconnectRequired) {
        requestStratumReconnect();
      }
      // Copy over the new settings
      memcpy(&settings, &newSettings, sizeof(SetupData));

      // Ane make sure they get written to EEPROM or NVS
      settings.currentMode = MODE_INSTALL_COMPLETE;
      saveSettings();

      // If we updated the network settings, then reconnect and go to the main screen
      if (mainAction) {
        am.action = mainAction;
        xQueueSend(appMessageQueueHandle, &am, pdMS_TO_TICKS(10));
      }

      // Send message back to user
      strcpy(messages, "Changes were applied.");

    } else {
      strcpy(messages, "No changes were detected.");
    }
  }

  sendAjaxResponse(!error, changesMade, true, messages);
}

void handleStatusJson() {

  char* poolUrl = escape_html(monitorData.currentPool);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  snprintf(temp, TEMP_BUFFER_SIZE, "{\"mining\": %s, \"hps\": \"%s\", \"poolSubmissions\": \"%s\", \"validBlocks\": \"%s\","
                                   " \"poolConnected\": %s, \"blocks32\": \"%s\", \"blocks16\": \"%s\", \"poolHost\": \"%s\", \"poolPort\": %d,"
                                   " \"uptime\": %llu, \"bestDifficulty\": \"%s\", \"totalHashes\": \"%s\", \"totalJobs\": \"%s\", \"blockHeight\": %lu, \"mac\": \"%s\", \"poolDifficulty\": %f}",
           (monitorData.isMining ? trueString : falseString),
           monitorData.hashesPerSecondStr,
           monitorData.poolSubmissionsStr,
           monitorData.validBlocksFoundStr,
           (monitorData.poolConnected ? trueString : falseString),
           monitorData.blocks32FoundStr,
           monitorData.blocks16FoundStr,
           poolUrl,
           settings.poolPort,
           monitorData.uptime,
           monitorData.bestDifficultyStr,
           monitorData.totalHashesStr,
           monitorData.totalJobsStr,
           monitorData.blockHeight,
           monitorData.macAddress,
           monitorData.poolDifficulty);

  server.sendContent(temp);

  if (poolUrl) {
    free(poolUrl);
  }
}

void handleStatus() {

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  SEND_PAGE_HEADER
  server.sendContent_P(statusPage);
  server.sendContent_P(statusPageJavascript);
  SEND_PAGE_FOOTER;
}

void showLoginPage(bool showError) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  SEND_PAGE_HEADER

  if( showError ) {
    server.sendContent_P(loginError);
  }

  server.sendContent_P(loginPage);
  SEND_PAGE_FOOTER;

}

void handleLoginPage() {
  showLoginPage(false);
}

void handleLoginSubmission() {
  String username = server.arg("username");
  String password = server.arg("password");
  if (username.length() && password.length()) {
    if (!strcasecmp(username.c_str(), settings.htaccessUser) && !strcmp(password.c_str(), settings.htaccessPassword)) {
      refreshLoginCookie();
      redirect("config");
      return;
    }
  }
  // Loop back to login page if we can't get in
  showLoginPage(true);
}

void handleGears() {
  String header = "Cache-Control";
  String value = "max-age=86400";
  server.sendHeader(header, value);
  server.send_P(200, (PGM_P)pngContentType, (PGM_P)gears, (size_t)sizeof(gears));
}

void handleLogIcon() {
  String header = "Cache-Control";
  String value = "max-age=86400";
  server.sendHeader(header, value);
  server.send_P(200, (PGM_P)pngContentType, (PGM_P)log_icon, (size_t)sizeof(log_icon));
}

void handleLogo() {
  String header = "Cache-Control";
  String value = "max-age=86400";
  server.sendHeader(header, value);
  server.send_P(200, (PGM_P)pngContentType, (PGM_P)small_logo, (size_t)sizeof(small_logo));
}

void handleFavicon() {
  String header = "Cache-Control";
  String value = "max-age=86400";
  server.sendHeader(header, value);
  server.send_P(200, (PGM_P)pngContentType, (PGM_P)favicon_32x32, (size_t)sizeof(favicon_32x32));
}



//////////////////////////////////////////////////////////////////////////////////////////
// Scan for WiFi networks and send back results as JSON array
//////////////////////////////////////////////////////////////////////////////////////////
void ajaxWiFiScan() {

  // Make sure the user is logged in
  if (!validateJWT()) {
    sendAjaxResponse(false, false, false, "User is not logged in.");
    return;
  }

  String s;
  s.reserve(2000);
  s = "{\"networks\": [";

  int n = WiFi.scanNetworks();
  if (n > 0) {
    for (int i = 0; i < n && s.length() < 1900; ++i) {
      if (i > 0) {
        s += ",";
      }
      // Print SSID and RSSI for each network found
      s += "{\"ssid\": \"";
      s += WiFi.SSID(i);
      s += "\", \"str\":";
      s += WiFi.RSSI(i);
      s += ", \"enc\": ";
      s += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "\"N\"" : "\"Y\"";
      s += "}";
    }
  }

  s += "]}";
  server.send(200, "application/json", s);
  WiFi.scanDelete();
}



void handlePing() {
  const char* tfPtr = validateJWT() ? trueString : falseString;
  snprintf(temp, TEMP_BUFFER_SIZE,
           "{\"ping\": %s, \"version\": %lu}", tfPtr, MINING_HARDWARE_VERSION_HEX);

  server.send(200, "application/json", temp);
}

void handleLog() {
  SEND_PAGE_HEADER
  server.sendContent_P(logPage);
  SEND_PAGE_FOOTER
}


void onWebSocketMessage(String message) {
   // Serial.println("[WS] Received: " + message);
}


void webTask(void* task_id) {

  // Make sure we have a key to start with, just for validations
  generateRandomJWTKey();

  size_t headerkeyssize = sizeof(headersToCollect) / sizeof(char*);
  server.collectHeaders(headersToCollect, headerkeyssize);

  server.on("/wifiScan", ajaxWiFiScan);
  server.on("/logo.png", handleLogo);
  server.on("/favicon.png", handleFavicon);
  server.on("/gears.png", handleGears);
  server.on("/log_icon.png", handleLogIcon);
  server.on("/login", HTTPMethod::HTTP_GET, handleLoginPage);
  server.on("/login", HTTPMethod::HTTP_POST, handleLoginSubmission);
  server.on("/config", HTTPMethod::HTTP_GET, handleConfigGet);
  server.on("/config", HTTPMethod::HTTP_POST, handleConfigPost);
  server.on("/status", handleStatus);
  server.on("/statusJson", handleStatusJson);
  server.on("/ping", handlePing);
  server.on("/logviewer", handleLog);
  server.on("/", handleRoot);
  
  server.begin();  
  webSocket.begin();
  webSocket.onMessage(onWebSocketMessage);

  while (1) {
    server.handleClient();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

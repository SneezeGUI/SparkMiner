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
#include "nvs_flash.h"
#include "nvs.h"
#include "defines_n_types.h"
#include "monitor.h"

#define NVS_TYPE_16BIT 0
#define NVS_TYPE_CHAR 1
#define NVS_TYPE_BOOL 2
#define NVS_TYPE_8BIT 3
#define NVS_TYPE_32BIT 4
#define NVS_TYPE_64BIT 5


typedef struct {
  char keyName[32];
  uint8_t type;  
  const void* defaultValue;
  size_t maxLength; // characters
  void *settingsPtr;
} SetupDataTypes;

extern MonitorData monitorData;
extern SetupData settings;

const uint16_t defaults16[] = {MODE_FACTORY_FRESH, 21496, 0};
const char* defaultChar[] = {"bitsy", "miner", "public-pool.io", "", "x", "north-america.pool.ntp.org"};
const uint8_t defaults8[] = {128, 0};
const uint32_t defaults32[] = {0, 0x042045, 0xffffff };
const bool defaultsBool[] = {true, false};
const uint64_t defaults64[] = {0};

SetupDataTypes setupData[] = 
  { 
    {"currentMode", NVS_TYPE_16BIT, &defaults16[0], 0, &settings.currentMode},
    {"ssid", NVS_TYPE_CHAR, defaultChar[3], MAX_SSID_LENGTH, settings.ssid},
    {"ssidPassword", NVS_TYPE_CHAR, defaultChar[3], MAX_PASSWORD_LENGTH, settings.ssidPassword},    
    {"htUser", NVS_TYPE_CHAR, defaultChar[0], MAX_USERNAME_LENGTH, settings.htaccessUser},
    {"htPassword", NVS_TYPE_CHAR, defaultChar[1], MAX_PASSWORD_LENGTH, settings.htaccessPassword},
    {"poolUrl", NVS_TYPE_CHAR, defaultChar[2], MAX_POOL_URL_LENGTH, settings.poolUrl},
    {"poolPort", NVS_TYPE_16BIT, &defaults16[1], 0, &settings.poolPort},
    {"poolPass", NVS_TYPE_CHAR, defaultChar[4], MAX_POOL_PASSWORD_LENGTH, settings.poolPassword},
    {"wallet", NVS_TYPE_CHAR, defaultChar[3], MAX_WALLET_LENGTH, settings.wallet},
    {"bupPoolUrl", NVS_TYPE_CHAR, defaultChar[3], MAX_POOL_URL_LENGTH, settings.backupPoolUrl},
    {"bupPoolPort", NVS_TYPE_16BIT, &defaults16[1], 0, &settings.backupPoolPort},
    {"bupPoolPass", NVS_TYPE_CHAR, defaultChar[3], MAX_POOL_PASSWORD_LENGTH, settings.backupPoolPassword},
    {"bupWallet", NVS_TYPE_CHAR, defaultChar[3], MAX_WALLET_LENGTH, settings.backupWallet},    
    {"screenRot", NVS_TYPE_8BIT, &defaults8[1], 0, &settings.screenRotation},
    {"randoTS", NVS_TYPE_BOOL, &defaultsBool[1], 0, &settings.randomizeTimestamp},
    {"screenBrt", NVS_TYPE_8BIT, &defaults8[0], 0, &settings.screenBrightness},
    {"inactivTmr", NVS_TYPE_32BIT, &defaults32[0], 0, &settings.inactivityTimer},
    {"inactivBrt", NVS_TYPE_8BIT, &defaults8[1], 0, &settings.inactivityBrightness},
    {"foreColor", NVS_TYPE_32BIT, &defaults32[2], 0, &settings.foregroundColor},
    {"backColor", NVS_TYPE_32BIT, &defaults32[1], 0, &settings.backgroundColor},
    {"staticIp", NVS_TYPE_BOOL, &defaultsBool[1], 0, &settings.staticIp},
    {"ipAddress", NVS_TYPE_32BIT, &defaults32[0], 0, &settings.ipAddress},
    {"subnet", NVS_TYPE_32BIT, &defaults32[0], 0, &settings.subnet},
    {"gateway", NVS_TYPE_32BIT, &defaults32[0], 0, &settings.gateway},
    {"priDNS", NVS_TYPE_32BIT, &defaults32[0], 0, &settings.primaryDNS},
    {"secDNS", NVS_TYPE_32BIT, &defaults32[0], 0, &settings.secondaryDNS},
    {"saveMonData", NVS_TYPE_BOOL, &defaultsBool[0], 0, &settings.saveMonitorData},
    {"ntpServer", NVS_TYPE_CHAR, defaultChar[5], MAX_POOL_URL_LENGTH, settings.ntpServer},
    {"utcOffset", NVS_TYPE_32BIT, &defaults32[0], 0, &settings.utcOffset},
    {"clock24", NVS_TYPE_BOOL, &defaultsBool[1], 0, &settings.clock24},
    {"stratumRptr", NVS_TYPE_BOOL, &defaultsBool[1], 0, &settings.stratumRepeater},
    {"led1red", NVS_TYPE_8BIT, &defaults8[1], 0, &settings.led1red},
    {"led1green", NVS_TYPE_8BIT, &defaults8[1], 0, &settings.led1green},
    {"led1blue", NVS_TYPE_8BIT, &defaults8[1], 0, &settings.led1blue},
    {"coreZMining", NVS_TYPE_BOOL, &defaultsBool[1], 0, &settings.coreZeroDisabled},  // default to false
    {"webTheme", NVS_TYPE_8BIT, &defaults8[1], 0, &settings.webTheme},
    {"asicBoost", NVS_TYPE_BOOL, &defaultsBool[1], 0, &settings.supportAsicBoost},
    {"invertCol", NVS_TYPE_BOOL, &defaultsBool[0], 0, &settings.invertColors},
    {"logViewer", NVS_TYPE_BOOL, &defaultsBool[1], 0, &settings.enableLogViewer}
  }; 


SetupDataTypes monitorSavedValues[] = 
{ 
  {"mTotalHashes", NVS_TYPE_64BIT, &defaults64[0], 0, &monitorData.totalHashes},
  {"mValidBlocks", NVS_TYPE_32BIT, &defaults32[0], 0, &monitorData.validBlocksFound},
  {"mBlocks16", NVS_TYPE_32BIT, &defaults32[0], 0, &monitorData.blocks16Found},
  {"mBlocks32", NVS_TYPE_32BIT, &defaults32[0], 0, &monitorData.blocks32Found},
  {"mPoolSub", NVS_TYPE_32BIT, &defaults32[0], 0, &monitorData.poolSubmissions},
  {"mTotalJobs", NVS_TYPE_32BIT, &defaults32[0], 0, &monitorData.totalJobs},
  {"mBestDiff", NVS_TYPE_64BIT, &defaults64[0], 0, &monitorData.bestDifficulty}
};

void init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        // Retry nvs_flash_init
        err = nvs_flash_init();
    }
}



// Loads up settings or a default if one is not found
bool loadData(SetupDataTypes dt[], size_t items) {
  bool rv = true;
  nvs_handle_t nvs_handle;
  esp_err_t err;

  // Open NVS handle
  err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
      return false;
  }

  for(int16_t i = 0; i < items; i++) {
    switch( dt[i].type ) {
      case NVS_TYPE_BOOL:
      case NVS_TYPE_8BIT:
        err = nvs_get_u8(nvs_handle, dt[i].keyName, (uint8_t*)dt[i].settingsPtr);
        if( err == ESP_ERR_NVS_NOT_FOUND ) {
          *(uint8_t *) dt[i].settingsPtr = *(uint8_t*) dt[i].defaultValue;
          err = ESP_OK;
        }
        break;
      case NVS_TYPE_16BIT:
        err = nvs_get_u16(nvs_handle, dt[i].keyName, (uint16_t*) dt[i].settingsPtr);
        if( err == ESP_ERR_NVS_NOT_FOUND ) {
          *(uint16_t *) dt[i].settingsPtr = *(uint16_t*) dt[i].defaultValue;
          err = ESP_OK;
        }        
        break;
      case NVS_TYPE_32BIT:
        err = nvs_get_u32(nvs_handle, dt[i].keyName, (uint32_t*)dt[i].settingsPtr);
        if( err == ESP_ERR_NVS_NOT_FOUND ) {
          *(uint32_t *) dt[i].settingsPtr = *(uint32_t*) dt[i].defaultValue;
          err = ESP_OK;
        }
        break;
      case NVS_TYPE_64BIT:
        err = nvs_get_u64(nvs_handle, dt[i].keyName, (uint64_t*)dt[i].settingsPtr);
        if( err == ESP_ERR_NVS_NOT_FOUND ) {
          *(uint64_t *) dt[i].settingsPtr = *(uint64_t*) dt[i].defaultValue;
          err = ESP_OK;
        }
        break;        
      case NVS_TYPE_CHAR:
        size_t s = dt[i].maxLength;
        err = nvs_get_str(nvs_handle, dt[i].keyName, (char*)dt[i].settingsPtr, &s);
        if( err == ESP_ERR_NVS_NOT_FOUND ) {
          strncpy((char*) dt[i].settingsPtr, (char*)dt[i].defaultValue, dt[i].maxLength);
          err = ESP_OK;
        }
        break;
    }
    if( err != ESP_OK ) {
      rv = false;
    }
  }

  nvs_close(nvs_handle);
  return rv;
}

bool loadSettings() {
  return loadData(setupData, sizeof(setupData) / sizeof(SetupDataTypes));
}

bool loadMonitorData() {
  return loadData(monitorSavedValues, sizeof(monitorSavedValues) / sizeof(SetupDataTypes));
}


bool saveData(SetupDataTypes dt[], size_t items) {
  bool rv = true;
  nvs_handle_t nvs_handle;
  esp_err_t err;

  // Open NVS handle
  err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
      return false;
  }

  for(int16_t i = 0; i < items; i++) {
    switch( dt[i].type ) {
      case NVS_TYPE_BOOL:
      case NVS_TYPE_8BIT:
        err = nvs_set_u8(nvs_handle, dt[i].keyName, *((uint8_t*)dt[i].settingsPtr));
        break;
      case NVS_TYPE_16BIT:
        err = nvs_set_u16(nvs_handle, dt[i].keyName, *((uint16_t*)dt[i].settingsPtr));
        if(err != ESP_OK ) dbg("Error saving 16\n");
        break;
      case NVS_TYPE_32BIT:
        err = nvs_set_u32(nvs_handle, dt[i].keyName, *((uint32_t*)dt[i].settingsPtr));
        if(err != ESP_OK ) dbg("Error saving 32\n");
        break;
      case NVS_TYPE_64BIT:
        err = nvs_set_u64(nvs_handle, dt[i].keyName, *((uint64_t*)dt[i].settingsPtr));
        if(err != ESP_OK ) dbg("Error saving 64\n");
        break;        
      case NVS_TYPE_CHAR:        
        err = nvs_set_str(nvs_handle, dt[i].keyName, (char*)dt[i].settingsPtr);
        if(err != ESP_OK ) dbg("Error saving char\n");
        break;
    }
    if( err != ESP_OK ) {
      dbg("Error saving %d\n", err);
      rv = false;
    } 
  }

  if( rv ) {
    nvs_commit(nvs_handle);
  }
  nvs_close(nvs_handle);

  return rv;
}


bool saveSettings() { 
  return saveData(setupData, sizeof(setupData) / sizeof(SetupDataTypes) );
}

bool saveMonitorData() {
  return saveData(monitorSavedValues, sizeof(monitorSavedValues) / sizeof(SetupDataTypes));
}


bool saveSettingsOld() {
  bool rv = true;
  nvs_handle_t nvs_handle;
  esp_err_t err;

  // Open NVS handle
  err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
      return false;
  }

  for(int16_t i = 0; i < sizeof(setupData) / sizeof(SetupDataTypes); i++) {
    switch( setupData[i].type ) {
      case NVS_TYPE_BOOL:
      case NVS_TYPE_8BIT:
        err = nvs_set_u8(nvs_handle, setupData[i].keyName, *((uint8_t*)setupData[i].settingsPtr));
        break;
      case NVS_TYPE_16BIT:
        err = nvs_set_u16(nvs_handle, setupData[i].keyName, *((uint16_t*)setupData[i].settingsPtr));
        if(err != ESP_OK ) dbg("Error saving 16\n");
        break;
      case NVS_TYPE_32BIT:
        err = nvs_set_u32(nvs_handle, setupData[i].keyName, *((uint32_t*)setupData[i].settingsPtr));
        if(err != ESP_OK ) dbg("Error saving 32\n");
        break;
      case NVS_TYPE_64BIT:
        err = nvs_set_u64(nvs_handle, setupData[i].keyName, *((uint64_t*)setupData[i].settingsPtr));
        if(err != ESP_OK ) dbg("Error saving 64\n");
        break;        
      case NVS_TYPE_CHAR:        
        err = nvs_set_str(nvs_handle, setupData[i].keyName, (char*)setupData[i].settingsPtr);
        if(err != ESP_OK ) dbg("Error saving char\n");
        break;
    }
    if( err != ESP_OK ) {
      dbg("Error saving %d\n", err);
      rv = false;
    } 
  }

  if( rv ) {
    nvs_commit(nvs_handle);
  }
  nvs_close(nvs_handle);

  return rv;
}

bool save_blob(const char* key, const void*value, size_t length) {
    bool rv = false;
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS handle
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return rv;
    }

    // Write value to NVS
    err = nvs_set_blob(nvs_handle, key, value, length);
    if (err == ESP_OK) {
      err = nvs_commit(nvs_handle);
      if( err == ESP_OK ) {
        rv = true;
      }
    }

    // Close NVS handle
    nvs_close(nvs_handle);

    return rv;
}


bool read_blob(const char* key, void* buffer, size_t length) {
    bool rv = false;
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t actualLength = length;

    // Open NVS handle
    err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {        
        return rv;
    }

    // Read value from NVS
    err = nvs_get_blob(nvs_handle, key, buffer, &actualLength);
    if( err == ESP_OK ) {
      rv = true;
    }
        // Close NVS handle
    nvs_close(nvs_handle);

    return rv;
}

void delete_all_keys() {

    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {        
        return;
    }

    err = nvs_erase_all(nvs_handle);
    if( err == ESP_OK ) {
      nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
}

void delete_key(const char* key) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS handle
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {        
        return;
    }

    // Erase the key
    err = nvs_erase_key(nvs_handle, key);
    if (err == ESP_OK) {
        // Commit the changes
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
        }
    }

    // Close NVS handle
    nvs_close(nvs_handle);
}

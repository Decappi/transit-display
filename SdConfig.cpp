// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#include "SdConfig.h"

#include <SD_MMC.h>
#include <ArduinoJson.h>
#include <Wire.h>

// TCA9554 I2C GPIO expander.
// EXIO7 controls TF card: either power MOSFET (LOW=on) or CS/DAT3.
// TCA defaults: all pins input (high-Z). We set EXIO7 output LOW.
// If TCA not at 0x20, try 0x21. Silent skip if neither responds.
static void sdInitTCA9554() {
  constexpr uint8_t REG_CONFIG = 0x03;
  constexpr uint8_t REG_OUTPUT = 0x01;
  uint8_t addr;
  bool found = false;

  for (const uint8_t a : {0x20, 0x21}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      addr = a;
      found = true;
      break;
    }
  }
  if (!found) {
    Serial.println("tca9554=no_dev");
    return;
  }

  // Set EXIO7 output (bit7=0), rest input (bits6-0=1)
  Wire.beginTransmission(addr);
  Wire.write(REG_CONFIG);
  Wire.write(0x7F);
  if (Wire.endTransmission() != 0) { Serial.println("tca9554=config_fail"); return; }

  // EXIO7=HIGH → DAT3 pulled up → card enters SDMMC mode (not SPI)
  Wire.beginTransmission(addr);
  Wire.write(REG_OUTPUT);
  Wire.write(0x80);  // bits: EXIO7=1 (HIGH), rest=0
  if (Wire.endTransmission() != 0) { Serial.println("tca9554=out_fail"); return; }

  Serial.printf("tca9554=ok addr=0x%02x exio7=HIGH\n", addr);
  delay(50);  // settle
}

bool initSD() {
  sdInitTCA9554();
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("sd=no_card");
    return false;
  }
  Serial.println("sd=mounted");
  return true;
}

void loadWiFiFromSD(char* outSsid, size_t ssidSize,
                    char* outPass,  size_t passSize) {
  // outSsid/outPass already carry compile-time defaults from caller.
  // We only override when SD + config file succeed.

  // SD must be mounted already (call initSD() first)
  if (!SD_MMC.cardSize()) {
    return;
  }

  // Try to open config file
  File configFile = SD_MMC.open(SD_CONFIG_PATH_DOT, FILE_READ);
  if (!configFile) {
    configFile = SD_MMC.open(SD_CONFIG_PATH_FLAT, FILE_READ);
  }
  if (!configFile) {
    Serial.println("wifi_sd=no_config_file");
    return;
  }

  // Parse JSON
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, configFile);
  configFile.close();

  if (err) {
    Serial.printf("wifi_sd=json_parse_error code=%d\n", err.code());
    return;
  }

  // Locate wifi key — try common case variations
  JsonVariant wifiVariant = doc["wifi"];
  if (wifiVariant.isNull()) wifiVariant = doc["WiFi"];
  if (wifiVariant.isNull()) wifiVariant = doc["WIFI"];
  if (wifiVariant.isNull()) {
    Serial.println("wifi_sd=missing_wifi_key");
    return;
  }

  // Support both formats:
  //   array: {"wifi": [{"ssid": "x", "password": "y"}, ...]}
  //   object: {"wifi": {"ssid": "x", "password": "y"}}
  JsonObject wifi;
  if (wifiVariant.is<JsonArray>()) {
    // Take first entry with a valid SSID
    for (JsonVariant entry : wifiVariant.as<JsonArray>()) {
      if (entry["ssid"] || entry["SSID"] || entry["Ssid"]) {
        wifi = entry.as<JsonObject>();
        break;
      }
    }
  } else if (wifiVariant.is<JsonObject>()) {
    wifi = wifiVariant.as<JsonObject>();
  }
  if (wifi.isNull()) {
    Serial.println("wifi_sd=missing_wifi_entry");
    return;
  }

  const char* ssid = wifi["ssid"];
  if (!ssid) ssid = wifi["SSID"];
  if (!ssid) ssid = wifi["Ssid"];

  const char* pass = wifi["password"];
  if (!pass) pass = wifi["Password"];
  if (!pass) pass = wifi["PASSWORD"];
  if (!pass) pass = wifi["pass"];
  if (!pass) pass = wifi["Pass"];

  if (!ssid || strlen(ssid) == 0) {
    Serial.println("wifi_sd=missing_ssid");
    return;
  }

  strlcpy(outSsid, ssid, ssidSize);
  if (pass) {
    strlcpy(outPass, pass, passSize);
  } else {
    outPass[0] = '\0';
  }

  Serial.println("wifi_sd=ok");
}

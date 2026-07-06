// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#include "TransitConfig.h"
#include <SD_MMC.h>
#include <ArduinoJson.h>

static const char* CONFIG_PATH = "/.config/settings.json";

bool TransitConfig_load(TransitConfig& cfg) {
  File f = SD_MMC.open(CONFIG_PATH, FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  JsonObject root = doc.as<JsonObject>();

  JsonObject wifi = root["wifi"];
  if (wifi) {
    strlcpy(cfg.wifiSsid, wifi["ssid"] | "", sizeof(cfg.wifiSsid));
    strlcpy(cfg.wifiPass, wifi["password"] | "", sizeof(cfg.wifiPass));
  }

  JsonObject transit = root["transit_display"];
  if (!transit) return false;

  strlcpy(cfg.profile, transit["profile"] | "home", sizeof(cfg.profile));
  cfg.refreshSec = transit["refresh_sec"] | 30;
  cfg.staleAfterSec = transit["stale_after_sec"] | 120;
  strlcpy(cfg.bridgeUrl, transit["bridge_url"] | "", sizeof(cfg.bridgeUrl));
  strlcpy(cfg.bridgeToken, transit["bridge_token"] | "", sizeof(cfg.bridgeToken));
  cfg.brightness = transit["brightness"] | 100;
  if (cfg.brightness > 100) cfg.brightness = 100;
  cfg.sleepTimeout = transit["sleep_timeout"] | 0;
  return true;
}

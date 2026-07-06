// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#pragma once

#include <Arduino.h>

struct TransitConfig {
  char wifiSsid[64];
  char wifiPass[64];
  char profile[32];
  uint16_t refreshSec;
  uint16_t staleAfterSec;
  uint16_t sleepTimeout;
  char bridgeUrl[128];
  char bridgeToken[64];
  uint8_t brightness;
};

bool TransitConfig_load(TransitConfig& cfg);

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#pragma once

#include <Arduino.h>

// SD card TF slot pins (SDMMC 1-bit mode):
//   CLK = GPIO2  (schematic: SDIO_SCK)
//   CMD = GPIO1  (schematic: SDIO_CMD)
//   D0  = GPIO3  (schematic: SDIO_D0)
//
// CS not needed in SDMMC mode (EXIO7 on TCA9554 is for SPI mode only)
#define SD_MMC_CLK  2
#define SD_MMC_CMD  1
#define SD_MMC_D0   3

// Paths checked (in order) on SD card for config
#define SD_CONFIG_PATH_DOT   "/.config/settings.json"
#define SD_CONFIG_PATH_FLAT  "/settings.json"

// Max sizes
#define SD_WIFI_SSID_MAX     64
#define SD_WIFI_PASS_MAX     64

// Try to load WiFi credentials from SD card.
// Falls back to compile-time WIFI_SSID / WIFI_PASSWORD if SD missing or no config.
// outSsid/password filled with null-terminated strings.
void loadWiFiFromSD(char* outSsid, size_t ssidSize,
                    char* outPass,  size_t passSize);

// Init TCA9554 + mount SD card. Returns true if card mounted.
// SD stays mounted for subsequent state save/load operations.
bool initSD();

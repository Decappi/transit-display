// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#pragma once

#ifndef ESP32QSPI_MAX_PIXELS_AT_ONCE
#define ESP32QSPI_MAX_PIXELS_AT_ONCE 16000
#endif

#include <Arduino_GFX_Library.h>
#include "Matrix.h"
#include "GFX_Layer.hpp"
#include "AmoledSettings.h"
#include "pin_config.h"
#include <XPowersLib.h>

class AmoledMatrix : public Matrix {
private:
  Arduino_ESP32QSPI *bus;
  Arduino_CO5300 *gfx;
  uint16_t *frameBuffer;
  uint16_t *bgFrameBuffer = nullptr;
  uint8_t backlightPercent;
  bool ready;

  XPowersPMU pmu;

  uint16_t rgb888to565(uint8_t r, uint8_t g, uint8_t b);
  void writeScaledPixel(uint16_t x, uint16_t y, uint16_t color565);
  void flushFramebuffer();
  void initPMU();

public:
  AmoledMatrix();
  ~AmoledMatrix();

  int getBatteryPercent();
  bool isCharging();

  void init() override;
  bool isReady() const;
  void drawPixelRGB888(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) override;
  void setBrightness(uint8_t newBrightness) override;
  uint8_t getBrightness() const override;
  uint8_t getXResolution() override;
  uint8_t getYResolution() override;
  void setRotation(uint8_t newRotation) override;
  void rotate90() override;
  void clearScreen() override;
  void update() override;
  void sendScreenshot();
  void precompositeBackground();
  void copyBackground();
  void presentFast();
  void compositeLayers() override;
  void displayOff();
  void displayOn();
};

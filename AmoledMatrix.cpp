// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#include "AmoledMatrix.h"

#include <esp_heap_caps.h>
#include <new>

AmoledMatrix::AmoledMatrix()
    : bus(nullptr), gfx(nullptr), frameBuffer(nullptr), bgFrameBuffer(nullptr),
      backlightPercent(100), ready(false) {
  fontSize = 2;
  rotation = 0;
}

AmoledMatrix::~AmoledMatrix() {
  delete background;
  background = nullptr;
  delete foreground;
  foreground = nullptr;
  delete gfx_compositor;
  gfx_compositor = nullptr;
  if (frameBuffer) {
    heap_caps_free(frameBuffer);
    frameBuffer = nullptr;
  }
  if (bgFrameBuffer) {
    heap_caps_free(bgFrameBuffer);
    bgFrameBuffer = nullptr;
  }
  delete gfx;
  gfx = nullptr;
  delete bus;
  bus = nullptr;
}

void AmoledMatrix::initPMU() {
  if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    Serial.println("AXP2101 not found");
    return;
  }

  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.clearIrqStatus();
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);

  // Cycle ALDO1/ALDO2 to force clean power-on-reset on CO5300 display
  // Required on warm boot (USB flash, AXP2101 stays alive), display would
  // otherwise stay black from stale state
  pmu.disableALDO1();
  pmu.disableALDO2();
  delay(150);

  pmu.setALDO1Voltage(3300);
  pmu.enableALDO1();
  pmu.setALDO2Voltage(3300);
  pmu.enableALDO2();
  delay(150);

  Serial.println("PMU ALDOs cycled");
}

int AmoledMatrix::getBatteryPercent() {
  return pmu.getBatteryPercent();
}

bool AmoledMatrix::isCharging() {
  return pmu.isCharging();
}

void AmoledMatrix::init() {
  ready = false;
  initPMU();

  bus = new (std::nothrow) Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0,
                                              LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
  if (bus == nullptr) {
    Serial.println("Amoled bus allocation failed");
    return;
  }

  gfx = new (std::nothrow) Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, LCD_WIDTH,
                                           LCD_HEIGHT, 16, 0, 0, 0);
  if (gfx == nullptr) {
    Serial.println("Amoled gfx allocation failed");
    return;
  }

  if (!gfx->begin(60000000)) {
    Serial.println("AmOLED begin failed");
    return;
  }

  gfx->setBrightness(200);
  gfx->setRotation(AmoledSettings::DISPLAY_ROTATION);
  brightness = 200;

  const size_t fbBytes = static_cast<size_t>(AmoledSettings::PHYSICAL_WIDTH) *
                         AmoledSettings::PHYSICAL_HEIGHT * sizeof(uint16_t);
  frameBuffer = static_cast<uint16_t*>(
      heap_caps_malloc(fbBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  if (frameBuffer == nullptr) {
    Serial.printf("Amoled framebuffer allocation failed bytes=%lu\n",
                  static_cast<unsigned long>(fbBytes));
    return;
  }

  memset(frameBuffer, 0, fbBytes);

  bgFrameBuffer = static_cast<uint16_t*>(
      heap_caps_malloc(fbBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (bgFrameBuffer == nullptr) {
    Serial.printf("Amoled background framebuffer allocation failed bytes=%lu\n",
                  static_cast<unsigned long>(fbBytes));
    heap_caps_free(frameBuffer);
    frameBuffer = nullptr;
    return;
  }

  memset(bgFrameBuffer, 0, fbBytes);

  Serial.printf("Amoled framebuffer %ux%u (%lu bytes) in PSRAM\n",
                AmoledSettings::PHYSICAL_WIDTH,
                AmoledSettings::PHYSICAL_HEIGHT,
                static_cast<unsigned long>(fbBytes));

  background = new (std::nothrow) GFX_Layer(
      AmoledSettings::LOGICAL_WIDTH, AmoledSettings::LOGICAL_HEIGHT,
      [this](int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
        drawPixelRGB888(x, y, r, g, b);
      });

  foreground = new (std::nothrow) GFX_Layer(
      AmoledSettings::LOGICAL_WIDTH, AmoledSettings::LOGICAL_HEIGHT,
      [this](int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
        drawPixelRGB888(x, y, r, g, b);
      });

  gfx_compositor = new (std::nothrow) GFX_LayerCompositor(
      [this](int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
        drawPixelRGB888(x, y, r, g, b);
      });

  if (background == nullptr || foreground == nullptr || gfx_compositor == nullptr ||
      !background->isReady() || !foreground->isReady()) {
    Serial.println("Amoled layer allocation failed");
    delete gfx_compositor;
    gfx_compositor = nullptr;
    delete background;
    background = nullptr;
    delete foreground;
    foreground = nullptr;
    return;
  }

  ready = true;

  Serial.printf("AmoledMatrix initialized: logical=%ux%u viewport=%ux%u scale=%u\n",
                AmoledSettings::LOGICAL_WIDTH, AmoledSettings::LOGICAL_HEIGHT,
                AmoledSettings::VIEWPORT_WIDTH, AmoledSettings::VIEWPORT_HEIGHT,
                AmoledSettings::SCALE);
}

bool AmoledMatrix::isReady() const {
  return ready;
}

uint16_t AmoledMatrix::rgb888to565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void AmoledMatrix::writeScaledPixel(uint16_t x, uint16_t y, uint16_t color565) {
  if (x >= AmoledSettings::LOGICAL_WIDTH ||
      y >= AmoledSettings::LOGICAL_HEIGHT || frameBuffer == nullptr) {
    return;
  }

  // Software rotation 90° CW + horizontal mirror: logical (x, y) → physical (H-1-y, x)
  // Fills full 368×448 screen at SCALE=4
  const uint16_t fbX = AmoledSettings::VIEWPORT_X +
      (AmoledSettings::LOGICAL_HEIGHT - 1 - y) * AmoledSettings::SCALE;
  const uint16_t fbY = AmoledSettings::VIEWPORT_Y + x * AmoledSettings::SCALE;
  for (uint16_t py = 0; py < AmoledSettings::SCALE; ++py) {
    uint32_t row =
        static_cast<uint32_t>(fbY + py) * AmoledSettings::PHYSICAL_WIDTH;
    for (uint16_t px = 0; px < AmoledSettings::SCALE; ++px) {
      frameBuffer[row + fbX + px] = color565;
    }
  }
}

void AmoledMatrix::drawPixelRGB888(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                                    uint8_t b) {
  writeScaledPixel(x, y, rgb888to565(r, g, b));
}

void AmoledMatrix::precompositeBackground() {
  if (!bgFrameBuffer || !background || !background->isReady()) return;

  const size_t fbBytes = static_cast<size_t>(AmoledSettings::PHYSICAL_WIDTH) *
                         AmoledSettings::PHYSICAL_HEIGHT * sizeof(uint16_t);
  memset(bgFrameBuffer, 0, fbBytes);

  for (uint16_t y = 0; y < AmoledSettings::LOGICAL_HEIGHT; ++y) {
    for (uint16_t x = 0; x < AmoledSettings::LOGICAL_WIDTH; ++x) {
      const CRGB src = background->pixels->data[y][x];
      const uint16_t fbX = (AmoledSettings::LOGICAL_HEIGHT - 1 - y);
      const uint16_t fbY = x;
      bgFrameBuffer[static_cast<uint32_t>(fbY) * AmoledSettings::PHYSICAL_WIDTH + fbX] =
          rgb888to565(src.r, src.g, src.b);
    }
  }
}

void AmoledMatrix::presentFast() {
  if (!bgFrameBuffer || !frameBuffer || !gfx) return;

  const size_t fbBytes = static_cast<size_t>(AmoledSettings::PHYSICAL_WIDTH) *
                         AmoledSettings::PHYSICAL_HEIGHT * sizeof(uint16_t);
  memcpy(frameBuffer, bgFrameBuffer, fbBytes);
  update();
}

void AmoledMatrix::copyBackground() {
  if (!bgFrameBuffer || !frameBuffer) return;

  const size_t fbBytes = static_cast<size_t>(AmoledSettings::PHYSICAL_WIDTH) *
                         AmoledSettings::PHYSICAL_HEIGHT * sizeof(uint16_t);
  memcpy(frameBuffer, bgFrameBuffer, fbBytes);
}

void AmoledMatrix::compositeLayers() {
  if (frameBuffer == nullptr || background == nullptr || foreground == nullptr ||
      !background->isReady() || !foreground->isReady()) {
    return;
  }

  // Clear full framebuffer each frame to purge any stray data
  const size_t fbBytes = static_cast<size_t>(AmoledSettings::PHYSICAL_WIDTH) *
                         AmoledSettings::PHYSICAL_HEIGHT * sizeof(uint16_t);
  memset(frameBuffer, 0, fbBytes);

  for (uint16_t y = 0; y < AmoledSettings::LOGICAL_HEIGHT; ++y) {
    for (uint16_t x = 0; x < AmoledSettings::LOGICAL_WIDTH; ++x) {
      const CRGB fgPixel = foreground->pixels->data[y][x];
      const bool hasForeground = fgPixel != foreground->transparency_colour;
      const CRGB src =
          hasForeground ? fgPixel : background->pixels->data[y][x];

      writeScaledPixel(x, y, rgb888to565(src.r, src.g, src.b));
    }
  }

  foreground->clear();
}

void AmoledMatrix::update() {
  if (frameBuffer == nullptr || gfx == nullptr) return;
  gfx->draw16bitRGBBitmap(
      0, 0,
      frameBuffer,
      AmoledSettings::PHYSICAL_WIDTH, AmoledSettings::PHYSICAL_HEIGHT);
}

void AmoledMatrix::setBrightness(uint8_t newBrightness) {
  brightness = newBrightness;
  if (gfx == nullptr) return;
  gfx->setBrightness(newBrightness);
}

uint8_t AmoledMatrix::getBrightness() const {
  return brightness;
}

uint8_t AmoledMatrix::getXResolution() {
  return AmoledSettings::LOGICAL_WIDTH;
}

uint8_t AmoledMatrix::getYResolution() {
  return AmoledSettings::LOGICAL_HEIGHT;
}

void AmoledMatrix::setRotation(uint8_t newRotation) {
  if (newRotation < 4 && newRotation != rotation) {
    rotation = newRotation;
    if (gfx == nullptr) return;
    gfx->setRotation(rotation);
  }
}

void AmoledMatrix::rotate90() {
  rotation = (rotation + 1) % 4;
  if (gfx == nullptr) return;
  gfx->setRotation(rotation);
}

void AmoledMatrix::sendScreenshot() {
  if (frameBuffer == nullptr) return;
  const uint32_t totalPixels = static_cast<uint32_t>(AmoledSettings::PHYSICAL_WIDTH) *
                                AmoledSettings::PHYSICAL_HEIGHT;
  Serial.printf("SCREENSHOT:%ux%u:", AmoledSettings::PHYSICAL_WIDTH, AmoledSettings::PHYSICAL_HEIGHT);
  Serial.write(reinterpret_cast<const uint8_t*>(frameBuffer), totalPixels * sizeof(uint16_t));
  Serial.println();
}

void AmoledMatrix::clearScreen() {
  if (frameBuffer != nullptr) {
    const size_t fbBytes = static_cast<size_t>(AmoledSettings::PHYSICAL_WIDTH) *
                           AmoledSettings::PHYSICAL_HEIGHT * sizeof(uint16_t);
    memset(frameBuffer, 0, fbBytes);
  }
  if (gfx == nullptr) return;
  gfx->fillScreen(0);
}

void AmoledMatrix::flushFramebuffer() {
  update();
}

void AmoledMatrix::displayOff() {
  if (gfx == nullptr) return;
  gfx->displayOff();
}

void AmoledMatrix::displayOn() {
  if (gfx == nullptr) return;
  gfx->displayOn();
}

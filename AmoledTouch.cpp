// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#include <Wire.h>
#include "AmoledTouch.h"
#include "pin_config.h"

static constexpr uint8_t CST816X_ADDR    = 0x15;
static constexpr uint8_t REG_GESTURE_ID  = 0x01;
static constexpr uint8_t REG_FINGER_NUM  = 0x02;
static constexpr uint8_t REG_XPOS_H      = 0x03;
static constexpr uint8_t REG_XPOS_L      = 0x04;
static constexpr uint8_t REG_YPOS_H      = 0x05;
static constexpr uint8_t REG_YPOS_L      = 0x06;
static constexpr uint8_t REG_CHIP_ID     = 0xA7;
static constexpr uint8_t REG_IRQ_CTL     = 0xFA;
static constexpr uint8_t IRQ_MODE_PERIODIC = 0x40;

// Cubic edge correction from Chromaripple: pushes raw coords toward display edges
// to compensate for touch sensor dead zone on small displays.
static int16_t correct_edge(int16_t coord, int16_t maxVal) {
    float t = (float)coord / (float)maxVal * 2.0f - 1.0f;
    const int16_t shortSide = 368;  // PHYSICAL_WIDTH (smaller of 368x448)
    float strength = 0.20f * (float)maxVal / (float)shortSide;
    t = t * (1.0f - strength) + t * t * t * strength;
    return (int16_t)((t + 1.0f) * maxVal / 2.0f);
}

static volatile bool s_touchIrqPending = false;

static void IRAM_ATTR onTouchIrq() {
  s_touchIrqPending = true;
}

static bool readReg(uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(CST816X_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(CST816X_ADDR, (uint8_t)1);
  if (Wire.available()) {
    val = Wire.read();
    return true;
  }
  return false;
}

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(CST816X_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission(true) == 0;
}

bool AmoledTouch::init() {
  Wire.setTimeOut(50);  // default 2000ms → 50ms; prevents 1.5s stall on hold

  // Reset pin
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), onTouchIrq, FALLING);

  uint8_t id;
  if (readReg(REG_CHIP_ID, id)) {
    Serial.printf("CST816x chip ID: 0x%02X\n", id);
  } else {
    Serial.println("CST816x not found");
  }

  writeReg(REG_IRQ_CTL, IRQ_MODE_PERIODIC);

  _initialized = true;
  return true;
}

const AmoledTouchPoint& AmoledTouch::update() {
  _pressStarted = false;
  _pressReleased = false;

  const bool pinLow = digitalRead(TP_INT) == LOW;

  // No IRQ, pin HIGH, and no known press → nothing to do
  if (!_wasPressed && !s_touchIrqPending && !pinLow) {
    return _point;
  }

  // Was pressed, but now pin HIGH and no IRQ → release detected
  if (_wasPressed && !pinLow && !s_touchIrqPending) {
    _point.pressed = false;
    _pressReleased = true;
    _wasPressed = false;
    return _point;
  }

  s_touchIrqPending = false;

  Wire.beginTransmission(CST816X_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.endTransmission(false);
  Wire.requestFrom(CST816X_ADDR, (uint8_t)13);

  uint8_t regs[13] = {0};
  uint8_t n = 0;
  while (Wire.available() && n < 13) {
    regs[n++] = Wire.read();
  }

  // Always parse XY from regs[3..6] (offset 0x03-0x06)
  if (n >= 7) {
    uint8_t touchCnt = regs[0x02];
    uint8_t xHigh    = regs[0x03];
    uint8_t xLow     = regs[0x04];
    uint8_t yHigh    = regs[0x05];
    uint8_t yLow     = regs[0x06];

    _point.fingerNum = touchCnt & 0x0F;
    if (n >= 13 && regs[9] != 0xFF && regs[12] != 0xFF) {
      uint16_t x2 = ((uint16_t)(regs[9]  & 0x0F) << 8) | regs[10];
      uint16_t y2 = ((uint16_t)(regs[11] & 0x0F) << 8) | regs[12];
      uint16_t x1 = ((uint16_t)(xHigh & 0x0F) << 8) | xLow;
      uint16_t y1 = ((uint16_t)(yHigh & 0x0F) << 8) | yLow;
      int16_t dx = abs((int16_t)x2 - (int16_t)x1);
      int16_t dy = abs((int16_t)y2 - (int16_t)y1);
      if (x2 <= 368 && y2 <= 448 && !(dx < 5 && dy < 5)) {
        _point.fingerNum = 2;
      }
    }
    bool touched = _point.fingerNum > 0;
    if (touched) {
      _point.rawX = (((uint16_t)(xHigh & 0x0F)) << 8) | xLow;
      _point.rawY = (((uint16_t)(yHigh & 0x0F)) << 8) | yLow;

      // Edge correction before rotation mapping
      int16_t cx = correct_edge(_point.rawX, 368);
      int16_t cy = correct_edge(_point.rawY, 448);
      _point.logicalX = AmoledSettings::logicalXFromRaw(cx, cy);
      _point.logicalY = AmoledSettings::logicalYFromRaw(cx, cy);

      if (!_wasPressed) {
        Serial.printf("touch: raw(%u,%u) logical(%u,%u) regs[0..%u]=",
                      _point.rawX, _point.rawY,
                      _point.logicalX, _point.logicalY, n - 1);
        for (uint8_t i = 0; i < n; ++i) {
          Serial.printf("%02X ", regs[i]);
        }
        Serial.println();

        // Push press-start event to event queue
        if (_eventQ) xQueueSend(_eventQ, &_point, 0);
      }

      // Always overwrite state queue with latest touch data
      if (_stateQ) xQueueOverwrite(_stateQ, &_point);
    }
    _pressStarted = touched && !_wasPressed;
    _pressReleased = !touched && _wasPressed;
    _wasPressed = touched;
  } else if (_wasPressed) {
    if (_stateQ) {
      AmoledTouchPoint releasePt;
      releasePt.pressed = false;
      xQueueOverwrite(_stateQ, &releasePt);
    }
    _pressReleased = true;
    _wasPressed = false;
  }

  return _point;
}

static void touchTaskFunc(void* arg) {
  AmoledTouch* self = static_cast<AmoledTouch*>(arg);
  TickType_t lastWake = xTaskGetTickCount();
  while (true) {
    self->update();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
  }
}

void AmoledTouch::startBackgroundTask() {
  _eventQ = xQueueCreate(4, sizeof(AmoledTouchPoint));
  _stateQ = xQueueCreate(1, sizeof(AmoledTouchPoint));
  {
    AmoledTouchPoint initPt;
    initPt.pressed = false;
    xQueueOverwrite(_stateQ, &initPt);
  }
  xTaskCreatePinnedToCore(touchTaskFunc, "touch", 4096, this, 2, NULL, 0);
  Serial.println("touch: bg task on core 0");
}

const AmoledTouchPoint& AmoledTouch::take() {
  // Check for pending press-start event first
  AmoledTouchPoint event;
  if (_eventQ && xQueueReceive(_eventQ, &event, 0) == pdTRUE) {
    _point = event;
    _point.pressed = true;
    return _point;
  }

  // Read latest state (held touch position updates or released=false)
  if (_stateQ && xQueueReceive(_stateQ, &_point, 0) == pdTRUE) {
    // _point now has the latest from bg task
  }

  return _point;
}

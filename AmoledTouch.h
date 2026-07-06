// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "AmoledSettings.h"

struct AmoledTouchPoint {
  bool pressed = false;
  uint8_t fingerNum = 0;
  uint16_t rawX = 0;
  uint16_t rawY = 0;
  uint16_t logicalX = 0;
  uint16_t logicalY = 0;
};

class AmoledTouch {
private:
  bool _initialized = false;
  bool _wasPressed = false;
  bool _pressStarted = false;
  bool _pressReleased = false;
  AmoledTouchPoint _point;

  QueueHandle_t _eventQ;  // press-start events, xQueueSend
  QueueHandle_t _stateQ;  // latest state, xQueueOverwrite

public:
  bool init();
  const AmoledTouchPoint& update();
  const AmoledTouchPoint& take();
  void startBackgroundTask();
  bool isPressed() const { return _point.pressed; }
  bool pressedStarted() const { return _pressStarted; }
  bool pressedReleased() const { return _pressReleased; }
};

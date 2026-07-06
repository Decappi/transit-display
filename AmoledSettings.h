// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#pragma once

#include <stdint.h>

namespace AmoledSettings {

inline constexpr uint8_t DISPLAY_ROTATION = 0;

inline constexpr uint16_t LOGICAL_WIDTH = 448;
inline constexpr uint16_t LOGICAL_HEIGHT = 368;

inline constexpr uint16_t PHYSICAL_WIDTH = 368;
inline constexpr uint16_t PHYSICAL_HEIGHT = 448;

inline constexpr uint8_t SCALE = 1;

// After software rotation: logical (x, y) → physical (y*SCALE, x*SCALE)
// Fills full screen exactly at SCALE=4: 92*4=368 × 112*4=448
inline constexpr uint16_t VIEWPORT_X = 0;
inline constexpr uint16_t VIEWPORT_Y = 0;
inline constexpr uint16_t VIEWPORT_WIDTH = PHYSICAL_WIDTH;
inline constexpr uint16_t VIEWPORT_HEIGHT = PHYSICAL_HEIGHT;

// Rotated + mirrored touch mapping:
//   physical Y = logicalX * SCALE                     → rawY → logicalX
//   physical X = (LOGICAL_H-1 - logicalY) * SCALE     → rawX → logicalY
inline constexpr uint16_t logicalXFromRaw(uint16_t rawX, uint16_t rawY) {
  (void)rawX;
  int32_t rel = static_cast<int32_t>(rawY) - static_cast<int32_t>(VIEWPORT_Y);
  if (rel <= 0) return 0;
  uint16_t lx = static_cast<uint16_t>(rel / SCALE);
  if (lx >= LOGICAL_WIDTH) return LOGICAL_WIDTH - 1;
  return lx;
}
inline constexpr uint16_t logicalYFromRaw(uint16_t rawX, uint16_t rawY) {
  (void)rawY;
  int32_t rel = static_cast<int32_t>(rawX) - static_cast<int32_t>(VIEWPORT_X);
  if (rel <= 0) return (LOGICAL_HEIGHT - 1);
  uint16_t ly = static_cast<uint16_t>(rel / SCALE);
  if (ly >= LOGICAL_HEIGHT) return 0;
  return (LOGICAL_HEIGHT - 1 - ly);
}

}  // namespace AmoledSettings

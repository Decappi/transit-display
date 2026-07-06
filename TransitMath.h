// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#pragma once

#include <stdint.h>

// Map coordinate system: [0..255] logical, [64..191] zoomed → full screen
// AmoledSettings not needed here — widths are compile-time constants

inline float scaleXf(int16_t v, bool zoomed) {
    if (zoomed) {
        return ((float)v - 64.0f) * 447.0f / 127.0f;
    }
    return (float)v * 447.0f / 255.0f;
}

inline float scaleYf(int16_t v, bool zoomed) {
    if (zoomed) {
        return ((float)v - 64.0f) * 367.0f / 127.0f;
    }
    return (float)v * 367.0f / 255.0f;
}

// Integer versions for hit-testing (avoids float→int32_t duplication)
inline int32_t scaleX(int16_t v, bool zoomed) {
    if (zoomed) {
        return ((int32_t)v - 64) * 447 / 127;
    }
    return (int32_t)v * 447 / 255;
}

inline int32_t scaleY(int16_t v, bool zoomed) {
    if (zoomed) {
        return ((int32_t)v - 64) * 367 / 127;
    }
    return (int32_t)v * 367 / 255;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#include "TransitRenderer.h"
#include "TransitMath.h"
#include "TransportIcons.h"
#include <math.h>

static void stamp(GFX_Layer* fg, float x, float y, int16_t r, uint16_t color);

// Quadratic Bezier: point at t (0..1) given control point C
static void bezierPt(float ax, float ay, float cx, float cy, float bx, float by, float t,
                     float* outX, float* outY) {
  float u = 1.0f - t;
  *outX = u*u*ax + 2*u*t*cx + t*t*bx;
  *outY = u*u*ay + 2*u*t*cy + t*t*by;
}

// Draw a thick quadratic Bezier curve on a GFX_Layer (for static rendering)
static void drawBezier(GFX_Layer* fg, float ax, float ay, float cx, float cy,
                       float bx, float by, uint16_t color, int16_t r) {
  float dx = bx - ax, dy = by - ay;
  float len = sqrtf(dx*dx + dy*dy);
  int steps = (int)(len * 1.5f);
  if (steps < 4) steps = 4;
  for (int s = 0; s <= steps; ++s) {
    float t = (float)s / steps;
    float px, py;
    bezierPt(ax, ay, cx, cy, bx, by, t, &px, &py);
    stamp(fg, px, py, r, color);
  }
}

// Compute control point for a quadratic Bezier between A and B
// Uses perpendicular offset from midpoint for smooth curve
static void controlPt(float ax, float ay, float bx, float by, int edgeIdx,
                      float* cx, float* cy) {
  float mx = (ax + bx) * 0.5f;
  float my = (ay + by) * 0.5f;
  float dx = bx - ax, dy = by - ay;
  float len = sqrtf(dx*dx + dy*dy);
  if (len < 1.0f) { *cx = mx; *cy = my; return; }
  // Very subtle perpendicular offset
  float sign = (edgeIdx & 1) ? 1.0f : -1.0f;
  float offset = len * 0.06f * sign;
  *cx = mx + (-dy / len) * offset;
  *cy = my + (dx / len) * offset;
}

static void stamp(GFX_Layer* fg, float x, float y, int16_t r, uint16_t color) {
  const int16_t ix = (int16_t)lroundf(x);
  const int16_t iy = (int16_t)lroundf(y);
  fg->fillRect(ix - r, iy - r, 2 * r, 2 * r, color);
}

static void stampIcon(AmoledMatrix& matrix, float x, float y, const char* product, uint16_t color) {
  (void)color;
  const int16_t cx = (int16_t)lroundf(x);
  const int16_t cy = (int16_t)lroundf(y);
  const uint16_t* bitmap = icon_for_product(product);

  for (uint16_t row = 0; row < ICON_H; ++row) {
    for (uint16_t col = 0; col < ICON_W; ++col) {
      uint16_t px = bitmap[row * ICON_W + col];
      if (px == 0) continue;
      uint8_t r = (((px >> 11) & 0x1F) * 527 + 23) >> 6;
      uint8_t g = (((px >> 5) & 0x3F) * 259 + 33) >> 6;
      uint8_t b = ((px & 0x1F) * 527 + 23) >> 6;
      // Scale 2x: draw each icon pixel as 2x2 block
      int16_t bx = cx - ICON_W + col * 2;
      int16_t by = cy - ICON_H + row * 2;
      matrix.drawPixelRGB888(bx,     by,     r, g, b);
      matrix.drawPixelRGB888(bx + 1, by,     r, g, b);
      matrix.drawPixelRGB888(bx,     by + 1, r, g, b);
      matrix.drawPixelRGB888(bx + 1, by + 1, r, g, b);
    }
  }
}

static float prevProgressFor(const TransitState& prev, const MapVehicle& v, float* delta_out) {
  for (uint8_t i = 0; i < prev.vehicleCount; ++i) {
    if (prev.vehicles[i].edge == v.edge && strncmp(prev.vehicles[i].line, v.line, sizeof(v.line)) == 0) {
      float d = v.progress - prev.vehicles[i].progress;
      if (d < -0.5f) d += 1.0f;
      if (d > 0.5f) d -= 1.0f;
      *delta_out = d;
      return prev.vehicles[i].progress;
    }
  }
  *delta_out = 0.0f;
  return v.progress;
}

void TransitRenderer_renderVehiclesDirect(AmoledMatrix& matrix, const TransitState& state, const TransitState& prev, float t) {
  for (uint8_t i = 0; i < state.vehicleCount; ++i) {
    const MapVehicle& v = state.vehicles[i];
    if (v.edge >= state.edgeCount) continue;
    const MapEdge& e = state.edges[v.edge];
    if (e.a >= state.nodeCount || e.b >= state.nodeCount) continue;
    float delta = 0.0f;
    float p = prevProgressFor(prev, v, &delta);
    p += delta * t;
    while (p > 1.0f) p -= 1.0f;
    while (p < 0.0f) p += 1.0f;
    const float ax = scaleXf(state.nodes[e.a].x, state.zoomed);
    const float ay = scaleYf(state.nodes[e.a].y, state.zoomed);
    const float bx = scaleXf(state.nodes[e.b].x, state.zoomed);
    const float by = scaleYf(state.nodes[e.b].y, state.zoomed);
    stampIcon(matrix, ax + (bx - ax) * p, ay + (by - ay) * p, v.product, v.color);
  }
}

void TransitRenderer_renderStatic(AmoledMatrix& matrix, const TransitState& state) {
  if (matrix.background == nullptr) return;
  matrix.background->fillScreen(0);

  for (uint8_t i = 0; i < state.edgeCount; ++i) {
    const MapEdge& e = state.edges[i];
    if (e.a >= state.nodeCount || e.b >= state.nodeCount) continue;
    const bool isTrack = (e.color == 0xC800 || e.color == 0x0460);
    const bool z = state.zoomed;
    const float x0f = scaleXf(state.nodes[e.a].x, z);
    const float y0f = scaleYf(state.nodes[e.a].y, z);
    const float x1f = scaleXf(state.nodes[e.b].x, z);
    const float y1f = scaleYf(state.nodes[e.b].y, z);
    float cx, cy;
    controlPt(x0f, y0f, x1f, y1f, i, &cx, &cy);
    if (isTrack) {
      float dx = x1f - x0f, dy = y1f - y0f;
      float len = sqrtf(dx*dx + dy*dy);
      if (len > 0.0f) {
        float nx = -dy / len, ny = dx / len;
        float cx1, cy1, cx2, cy2;
        controlPt(x0f + nx*3, y0f + ny*3, x1f + nx*3, y1f + ny*3, i, &cx1, &cy1);
        controlPt(x0f - nx*3, y0f - ny*3, x1f - nx*3, y1f - ny*3, i, &cx2, &cy2);
        drawBezier(matrix.background, x0f + nx*3, y0f + ny*3, cx1, cy1, x1f + nx*3, y1f + ny*3, e.color, 1);
        drawBezier(matrix.background, x0f - nx*3, y0f - ny*3, cx2, cy2, x1f - nx*3, y1f - ny*3, e.color, 1);
      }
    } else {
      drawBezier(matrix.background, x0f, y0f, cx, cy, x1f, y1f, e.color, 2);
    }
  }

  for (uint8_t i = 0; i < state.nodeCount; ++i) {
    const MapNode& n = state.nodes[i];
    const int16_t x = (int16_t)lroundf(scaleXf(n.x, state.zoomed));
    const int16_t y = (int16_t)lroundf(scaleYf(n.y, state.zoomed));
    if (x < -4 || y < -4 || x > AmoledSettings::LOGICAL_WIDTH + 4 || y > AmoledSettings::LOGICAL_HEIGHT + 4) continue;
    if (n.id[0] == '_') {
      matrix.background->fillCircle(x, y, 6, (uint16_t)0xF800);
      matrix.background->drawCircle(x, y, 6, (uint16_t)0xFFFF);
      continue;
    }
    uint8_t deg = 0;
    for (uint8_t e = 0; e < state.edgeCount; ++e) {
      if (state.edges[e].a == i || state.edges[e].b == i) ++deg;
    }
    if (deg <= 1) {
      matrix.background->drawCircle(x, y, 10, (uint16_t)0xFFFF);
    } else {
      matrix.background->fillCircle(x, y, 10, (uint16_t)0xFFFF);
    }
  }
}

void TransitRenderer_renderError(AmoledMatrix& matrix, const char* message) {
  if (matrix.foreground == nullptr) return;
  matrix.foreground->fillScreen(0);
  matrix.foreground->setTextColor(0xFFFF);
  matrix.foreground->setTextSize(6);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  matrix.foreground->getTextBounds(message, 0, 0, &x1, &y1, &w, &h);
  matrix.foreground->setCursor((AmoledSettings::LOGICAL_WIDTH - w) / 2,
                               (AmoledSettings::LOGICAL_HEIGHT - h) / 2);
  matrix.foreground->print(message);
}

void TransitRenderer_renderTooltipDirect(AmoledMatrix& matrix, const char* text, int16_t tapX, int16_t tapY) {
  if (!matrix.foreground || !matrix.foreground->isReady()) return;

  const int16_t margin = 10, maxW = AmoledSettings::LOGICAL_WIDTH - 2 * margin;

  char buf[128];
  strlcpy(buf, text, sizeof(buf));
  const char* lines[10];
  uint8_t lineCount = 0;
  lines[lineCount++] = buf;
  for (char* p = buf; *p; ++p) {
    if (*p == '\n') {
      *p = 0;
      lines[lineCount++] = p + 1;
      if (lineCount >= 10) break;
    }
  }

  uint16_t boxW = 0;
  uint16_t boxH = 0;
  uint16_t totalTextH = 0;
  matrix.foreground->setTextSize(2);
  matrix.foreground->setTextColor(0x0000);
  for (uint8_t i = 0; i < lineCount; ++i) {
    int16_t x1, y1;
    uint16_t w, h;
    matrix.foreground->getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
    if (w > boxW) boxW = w;
    if (i > 0) totalTextH += 2;
    totalTextH += h;
  }
  boxW += 2 * margin;
  boxH = totalTextH + 2 * margin;
  if (boxW > maxW) boxW = maxW;

  const bool left = tapX < AmoledSettings::LOGICAL_WIDTH / 2;
  const bool top = tapY < AmoledSettings::LOGICAL_HEIGHT / 2;
  const int16_t bx = left ? AmoledSettings::LOGICAL_WIDTH - boxW - margin : margin;
  const int16_t by = top ? AmoledSettings::LOGICAL_HEIGHT - boxH - margin : margin;

  // Draw pill onto foreground layer
  matrix.foreground->fillRect(bx, by, boxW, boxH, 0xFFFF);
  matrix.foreground->drawRect(bx, by, boxW, boxH, (uint16_t)0x0000);
  matrix.foreground->setTextColor(0x0000);
  int16_t cy = by + margin;
  for (uint8_t i = 0; i < lineCount; ++i) {
    int16_t x1, y1;
    uint16_t w, h;
    matrix.foreground->getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
    const int16_t cx = bx + (boxW - w) / 2;
    matrix.foreground->setCursor(cx, cy);
    matrix.foreground->print(lines[i]);
    cy += h + 2;
  }

  // Selective composite: copy all pill pixels from foreground to framebuffer
  for (uint16_t y = 0; y < boxH; ++y) {
    for (uint16_t x = 0; x < boxW; ++x) {
      CRGB fg = matrix.foreground->pixels->data[by + y][bx + x];
      matrix.drawPixelRGB888(bx + x, by + y, fg.r, fg.g, fg.b);
    }
  }

  // Clear pill from foreground for next frame
  matrix.foreground->fillRect(bx, by, boxW, boxH, matrix.foreground->transparency_colour);
}

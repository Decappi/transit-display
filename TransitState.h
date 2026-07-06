// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#pragma once

#include <Arduino.h>

struct MapNode {
  char id[16];
  char name[32];
  int16_t x;
  int16_t y;
};

struct MapEdge {
  uint8_t a;
  uint8_t b;
  uint16_t color;
};

struct MapVehicle {
  char line[8];
  char direction[32];
  char product[8];
  uint8_t edge;
  float progress;
  uint16_t color;
};

struct TransitState {
  char updatedAt[32];
  char profile[32];
  MapNode nodes[32];
  uint8_t nodeCount;
  MapEdge edges[32];
  uint8_t edgeCount;
  MapVehicle vehicles[32];
  uint8_t vehicleCount;
  bool stale;
  bool offline;
  bool zoomed;
  char error[32];
};

void TransitState_init(TransitState& s);

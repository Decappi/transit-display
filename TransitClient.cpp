// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#include "TransitClient.h"
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

static const char* CACHE_PATH = "/cache/last_transit.json";

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static uint16_t parseColor(const char* hex) {
  if (!hex || hex[0] != '#') return 0xFFFF;
  const size_t len = strlen(hex + 1);
  const uint32_t value = strtol(hex + 1, nullptr, 16);
  if (len == 4) {
    return (uint16_t)value;
  }
  if (len == 6) {
    const uint8_t r = (value >> 16) & 0xFF;
    const uint8_t g = (value >> 8) & 0xFF;
    const uint8_t b = value & 0xFF;
    return rgb565(r, g, b);
  }
  return (uint16_t)value;
}

static bool parseResponse(const JsonObject& root, TransitState& state) {
  TransitState_init(state);
  strlcpy(state.updatedAt, root["updated_at"] | "", sizeof(state.updatedAt));
  strlcpy(state.profile, root["profile"] | "", sizeof(state.profile));

  JsonObject mapRoot = root["map"];
  if (!mapRoot) return false;

  JsonArray nodes = mapRoot["nodes"];
  state.nodeCount = min((size_t)32, nodes.size());
  for (uint8_t i = 0; i < state.nodeCount; ++i) {
    JsonObject n = nodes[i];
    strlcpy(state.nodes[i].id, n["id"] | "", sizeof(state.nodes[i].id));
    strlcpy(state.nodes[i].name, n["name"] | "", sizeof(state.nodes[i].name));
    state.nodes[i].x = n["x"] | 0;
    state.nodes[i].y = n["y"] | 0;
  }

  JsonArray edges = mapRoot["edges"];
  state.edgeCount = min((size_t)32, edges.size());
  for (uint8_t i = 0; i < state.edgeCount; ++i) {
    JsonObject e = edges[i];
    state.edges[i].a = e["a"] | 0;
    state.edges[i].b = e["b"] | 0;
    state.edges[i].color = parseColor(e["color"]);
  }

  JsonArray vehicles = mapRoot["vehicles"];
  state.vehicleCount = min((size_t)32, vehicles.size());
  for (uint8_t i = 0; i < state.vehicleCount; ++i) {
    JsonObject v = vehicles[i];
    strlcpy(state.vehicles[i].line, v["line"] | "", sizeof(state.vehicles[i].line));
    strlcpy(state.vehicles[i].direction, v["direction"] | "", sizeof(state.vehicles[i].direction));
    strlcpy(state.vehicles[i].product, v["product"] | "", sizeof(state.vehicles[i].product));
    state.vehicles[i].edge = v["edge"] | 0;
    state.vehicles[i].progress = v["progress"] | 0.0f;
    state.vehicles[i].color = parseColor(v["color"]);
  }
  return true;
}

bool TransitClient_fetch(const TransitConfig& cfg, TransitState& state) {
  if (WiFi.status() != WL_CONNECTED) {
    strlcpy(state.error, "no_wifi", sizeof(state.error));
    state.offline = true;
    return TransitClient_loadCache(state);
  }

  HTTPClient http;
  char url[256];
  snprintf(url, sizeof(url), "%s/api/transit?profile=%s", cfg.bridgeUrl, cfg.profile);
  http.begin(url);
  http.setTimeout(8000);
  http.setUserAgent("transit_display-esp32");
  String auth = String("Bearer ") + cfg.bridgeToken;
  http.addHeader("Authorization", auth.c_str());
  int code = http.GET();
  if (code != 200) {
    strlcpy(state.error, "bridge_error", sizeof(state.error));
    state.offline = true;
    http.end();
    return TransitClient_loadCache(state);
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err || !parseResponse(doc.as<JsonObject>(), state)) {
    strlcpy(state.error, "json_error", sizeof(state.error));
    state.offline = true;
    return TransitClient_loadCache(state);
  }

  state.offline = false;
  TransitClient_saveCache(state);
  return true;
}

bool TransitClient_loadCache(TransitState& state) {
  File f = SD_MMC.open(CACHE_PATH, FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  return parseResponse(doc.as<JsonObject>(), state);
}

bool TransitClient_saveCache(const TransitState& state) {
  File f = SD_MMC.open(CACHE_PATH, FILE_WRITE, true);
  if (!f) return false;
  JsonDocument doc;
  doc["updated_at"] = state.updatedAt;
  doc["profile"] = state.profile;
  JsonObject mapRoot = doc["map"].to<JsonObject>();
  JsonArray nodes = mapRoot["nodes"].to<JsonArray>();
  for (uint8_t i = 0; i < state.nodeCount; ++i) {
    JsonObject n = nodes.add<JsonObject>();
    n["id"] = state.nodes[i].id;
    n["name"] = state.nodes[i].name;
    n["x"] = state.nodes[i].x;
    n["y"] = state.nodes[i].y;
  }
  JsonArray edges = mapRoot["edges"].to<JsonArray>();
  for (uint8_t i = 0; i < state.edgeCount; ++i) {
    JsonObject e = edges.add<JsonObject>();
    e["a"] = state.edges[i].a;
    e["b"] = state.edges[i].b;
    char hex[8];
    snprintf(hex, sizeof(hex), "#%04X", state.edges[i].color);
    e["color"] = hex;
  }
  JsonArray vehicles = mapRoot["vehicles"].to<JsonArray>();
  for (uint8_t i = 0; i < state.vehicleCount; ++i) {
    JsonObject v = vehicles.add<JsonObject>();
    v["line"] = state.vehicles[i].line;
    v["direction"] = state.vehicles[i].direction;
    v["edge"] = state.vehicles[i].edge;
    v["progress"] = state.vehicles[i].progress;
    char hex[8];
    snprintf(hex, sizeof(hex), "#%04X", state.vehicles[i].color);
    v["color"] = hex;
  }
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

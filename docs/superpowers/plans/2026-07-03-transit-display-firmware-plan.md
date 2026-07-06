# Transit Display ESP32 Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB- SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans`. Steps use checkbox syntax for tracking.

**Goal:** Build the ESP32 Arduino firmware that fetches the transit map from the backend, caches it on SD, and renders it on the 368×448 AMOLED.

**Architecture:** Reuse the AMOLED/SD hardware layer from `aquarium/`. Add small C++ modules for config parsing, HTTP client, state, and rendering. `transit_display.ino` drives setup/loop and state machine.

**Tech Stack:** Arduino Framework (ESP32), `Arduino_GFX_Library`, `ArduinoJson`, `SD_MMC`, `WiFi`.

---

## File Structure

```text
transit_display/
├── transit_display.ino
├── TransitConfig.h/.cpp
├── TransitState.h/.cpp
├── TransitClient.h/.cpp
├── TransitRenderer.h/.cpp
├── AmoledMatrix.h/.cpp          # copied from aquarium
├── AmoledTouch.h/.cpp           # copied from aquarium
├── SdConfig.h/.cpp              # copied from aquarium
├── AmoledSettings.h             # copied from aquarium
├── pin_config.h                 # copied from aquarium
├── lib/
│   ├── Matrix/                  # copied from aquarium/lib/Matrix
│   └── GFX_Lite/                # copied from aquarium/lib/GFX_Lite
├── compile.sh
├── Taskfile.yml
└── docs/superpowers/plans/2026-07-03-transit-display-firmware-plan.md
```

---

### Task 1: Hardware layer and project skeleton

**Files:**
- Copy: `aquarium/AmoledMatrix.*`, `aquarium/AmoledTouch.*`, `aquarium/SdConfig.*`, `aquarium/AmoledSettings.h`, `aquarium/pin_config.h`
- Copy: `aquarium/lib/Matrix`, `aquarium/lib/GFX_Lite`
- Create: `transit_display/transit_display.ino`
- Create: `transit_display/compile.sh`
- Create: `transit_display/Taskfile.yml`

- [ ] **Step 1: Copy hardware files**

```bash
cd /home/nikita/dev/esp32
cp aquarium/AmoledMatrix.h aquarium/AmoledMatrix.cpp transit_display/
cp aquarium/AmoledTouch.h aquarium/AmoledTouch.cpp transit_display/
cp aquarium/SdConfig.h aquarium/SdConfig.cpp transit_display/
cp aquarium/AmoledSettings.h aquarium/pin_config.h transit_display/
cp -r aquarium/lib/Matrix transit_display/lib/
cp -r aquarium/lib/GFX_Lite transit_display/lib/
```

- [ ] **Step 2: Write minimal sketch and build scripts**

```cpp
// transit_display.ino
#include <Arduino.h>
#include "AmoledMatrix.h"
#include "SdConfig.h"

AmoledMatrix matrix;

void setup() {
  Serial.begin(115200);
  initSD();
  matrix.init();
}

void loop() {
  matrix.clearScreen();
  matrix.update();
  delay(1000);
}
```

```bash
#!/bin/bash
# compile.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
FQBN="esp32:esp32:waveshare_esp32_s3_touch_amoled_18:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=enabled,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
arduino-cli compile --fqbn "$FQBN" \
  --build-property "build.partitions=app3M_fat9M_16MB" \
  --build-property "upload.maximum_size=3145728" \
  --library lib/Matrix --library lib/GFX_Lite \
  --build-path "$SCRIPT_DIR/build/esp32.esp32.esp32s3" .
echo "Build complete."
```

```yaml
# Taskfile.yml
version: 3
vars:
  PROJECT: transit_display
  FQBN: esp32:esp32:waveshare_esp32_s3_touch_amoled_18:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=enabled,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
  DEBUG_PORT: /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_1C:DB:D4:7A:0A:18-if00
  READ_PORT: /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_1C:DB:D4:7A:0A:18-if00
  MERGED_BIN: build/esp32.esp32.esp32s3/{{.PROJECT}}.ino.merged.bin

tasks:
  build:
    desc: Build firmware
    cmds:
      - bash compile.sh
  flash:
    desc: Flash firmware to ESP32 from host
    cmds:
      - python3 -m esptool --chip esp32s3 --port {{.DEBUG_PORT}} write-flash 0x0 /home/nikita/dev/esp32/{{.PROJECT}}/{{.MERGED_BIN}}
  screenshot:
    desc: Receive screenshot over serial
    cmds:
      - python3 /home/nikita/dev/esp32/aquarium/recv_screenshot.py {{.READ_PORT}} {{.CLI_ARGS | default ""}}
  monitor:
    desc: Open serial monitor
    cmds:
      - python3 -m serial.tools.miniterm {{.READ_PORT}} 115200
  clean:
    desc: Remove build artifacts
    cmds:
      - rm -rf build/esp32.esp32.esp32s3/
```

- [ ] **Step 3: Run compile test**

Run: `task build`
Expected: PASS (exit 0)

- [ ] **Step 4: Commit**

```bash
git add transit_display
git commit -m "feat(firmware): hardware layer and project skeleton"
```

---

### Task 2: TransitConfig parser

**Files:**
- Create: `transit_display/TransitConfig.h`
- Create: `transit_display/TransitConfig.cpp`

- [ ] **Step 1: Write the failing compile test**

Modify `transit_display.ino` to call `TransitConfig_load()`:

```cpp
#include "TransitConfig.h"
TransitConfig cfg;
void setup() {
  Serial.begin(115200);
  TransitConfig_load(cfg);
}
```

Run: `task build`
Expected: FAIL (TransitConfig.h not found)

- [ ] **Step 2: Implement TransitConfig**

```cpp
// TransitConfig.h
#pragma once
#include <Arduino.h>

struct TransitConfig {
  char profile[32];
  uint16_t refreshSec;
  uint16_t staleAfterSec;
  char bridgeUrl[128];
  char bridgeToken[64];
};

bool TransitConfig_load(TransitConfig& cfg);
```

```cpp
// TransitConfig.cpp
#include "TransitConfig.h"
#include <SD_MMC.h>
#include <ArduinoJson.h>

static const char* CONFIG_PATH = "/config/transit_display.json";

bool TransitConfig_load(TransitConfig& cfg) {
  File f = SD_MMC.open(CONFIG_PATH, FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  JsonObject root = doc["transit_display"];
  if (!root) return false;

  strlcpy(cfg.profile, root["profile"] | "home", sizeof(cfg.profile));
  cfg.refreshSec = root["refresh_sec"] | 30;
  cfg.staleAfterSec = root["stale_after_sec"] | 120;
  strlcpy(cfg.bridgeUrl, root["bridge_url"] | "", sizeof(cfg.bridgeUrl));
  strlcpy(cfg.bridgeToken, root["bridge_token"] | "", sizeof(cfg.bridgeToken));
  return true;
}
```

Update `transit_display.ino` to use the real header only for compile.

- [ ] **Step 3: Run compile test**

Run: `task build`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add transit_display
git commit -m "feat(firmware): load transit config from SD"
```

---

### Task 3: TransitState

**Files:**
- Create: `transit_display/TransitState.h`
- Create: `transit_display/TransitState.cpp`

- [ ] **Step 1: Write the failing compile test**

Reference `TransitState` in `transit_display.ino`:

```cpp
#include "TransitState.h"
TransitState state;
void setup() { TransitState_init(state); }
```

Run: `task build`
Expected: FAIL

- [ ] **Step 2: Implement TransitState**

```cpp
// TransitState.h
#pragma once
#include <Arduino.h>

struct MapNode {
  char id[16];
  char name[32];
  uint8_t x;
  uint8_t y;
};

struct MapEdge {
  uint8_t a;
  uint8_t b;
  uint16_t color;
};

struct MapVehicle {
  char line[8];
  char direction[32];
  uint8_t edge;
  float progress;
  uint16_t color;
};

struct TransitState {
  char updatedAt[32];
  char profile[32];
  MapNode nodes[8];
  uint8_t nodeCount;
  MapEdge edges[16];
  uint8_t edgeCount;
  MapVehicle vehicles[16];
  uint8_t vehicleCount;
  bool stale;
  bool offline;
  char error[32];
};

void TransitState_init(TransitState& s);
```

```cpp
// TransitState.cpp
#include "TransitState.h"

void TransitState_init(TransitState& s) {
  memset(&s, 0, sizeof(s));
}
```

- [ ] **Step 3: Run compile test**

Run: `task build`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add transit_display
git commit -m "feat(firmware): TransitState data model"
```

---

### Task 4: TransitClient

**Files:**
- Create: `transit_display/TransitClient.h`
- Create: `transit_display/TransitClient.cpp`

- [ ] **Step 1: Write the failing compile test**

Reference `TransitClient_fetch` in `transit_display.ino`:

```cpp
#include "TransitClient.h"
void setup() {
  TransitConfig cfg;
  TransitState state;
  TransitClient_fetch(cfg, state);
}
```

Run: `task build`
Expected: FAIL

- [ ] **Step 2: Implement TransitClient**

```cpp
// TransitClient.h
#pragma once
#include "TransitConfig.h"
#include "TransitState.h"

bool TransitClient_fetch(const TransitConfig& cfg, TransitState& state);
bool TransitClient_loadCache(TransitState& state);
bool TransitClient_saveCache(const TransitState& state);
```

```cpp
// TransitClient.cpp
#include "TransitClient.h"
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

static const char* CACHE_PATH = "/cache/last_transit.json";

static uint16_t parseColor(const char* hex) {
  if (!hex || hex[0] != '#') return 0xFFFF;
  return (uint16_t)strtol(hex + 1, nullptr, 16);
}

static bool parseResponse(const JsonObject& root, TransitState& state) {
  TransitState_init(state);
  strlcpy(state.updatedAt, root["updated_at"] | "", sizeof(state.updatedAt));
  strlcpy(state.profile, root["profile"] | "", sizeof(state.profile));

  JsonObject mapRoot = root["map"];
  if (!mapRoot) return false;

  JsonArray nodes = mapRoot["nodes"];
  state.nodeCount = min((size_t)8, nodes.size());
  for (uint8_t i = 0; i < state.nodeCount; ++i) {
    JsonObject n = nodes[i];
    strlcpy(state.nodes[i].id, n["id"] | "", sizeof(state.nodes[i].id));
    strlcpy(state.nodes[i].name, n["name"] | "", sizeof(state.nodes[i].name));
    state.nodes[i].x = n["x"] | 0;
    state.nodes[i].y = n["y"] | 0;
  }

  JsonArray edges = mapRoot["edges"];
  state.edgeCount = min((size_t)16, edges.size());
  for (uint8_t i = 0; i < state.edgeCount; ++i) {
    JsonObject e = edges[i];
    state.edges[i].a = e["a"] | 0;
    state.edges[i].b = e["b"] | 0;
    state.edges[i].color = parseColor(e["color"]);
  }

  JsonArray vehicles = mapRoot["vehicles"];
  state.vehicleCount = min((size_t)16, vehicles.size());
  for (uint8_t i = 0; i < state.vehicleCount; ++i) {
    JsonObject v = vehicles[i];
    strlcpy(state.vehicles[i].line, v["line"] | "", sizeof(state.vehicles[i].line));
    strlcpy(state.vehicles[i].direction, v["direction"] | "", sizeof(state.vehicles[i].direction));
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
  http.addHeader("Authorization", (String("Bearer ") + cfg.bridgeToken).c_str());
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
```

- [ ] **Step 3: Run compile test**

Run: `task build`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add transit_display
git commit -m "feat(firmware): HTTP client and SD cache"
```

---

### Task 5: TransitRenderer

**Files:**
- Create: `transit_display/TransitRenderer.h`
- Create: `transit_display/TransitRenderer.cpp`

- [ ] **Step 1: Write the failing compile test**

Reference `TransitRenderer_render` in `transit_display.ino`.

Run: `task build`
Expected: FAIL

- [ ] **Step 2: Implement renderer**

```cpp
// TransitRenderer.h
#pragma once
#include "AmoledMatrix.h"
#include "TransitState.h"

void TransitRenderer_render(AmoledMatrix& matrix, const TransitState& state, float localProgress);
void TransitRenderer_renderError(AmoledMatrix& matrix, const char* message);
```

```cpp
// TransitRenderer.cpp
#include "TransitRenderer.h"
#include "AmoledSettings.h"
#include <stdio.h>

static int16_t scaleX(uint8_t v) { return (int16_t)v * AmoledSettings::LOGICAL_WIDTH / 255; }
static int16_t scaleY(uint8_t v) { return (int16_t)v * AmoledSettings::LOGICAL_HEIGHT / 255; }

void TransitRenderer_render(AmoledMatrix& matrix, const TransitState& state, float localProgress) {
  if (matrix.foreground == nullptr) return;
  matrix.foreground->fillScreen(0);

  // edges
  for (uint8_t i = 0; i < state.edgeCount; ++i) {
    const MapEdge& e = state.edges[i];
    int16_t x0 = scaleX(state.nodes[e.a].x);
    int16_t y0 = scaleY(state.nodes[e.a].y);
    int16_t x1 = scaleX(state.nodes[e.b].x);
    int16_t y1 = scaleY(state.nodes[e.b].y);
    matrix.foreground->drawLine(x0, y0, x1, y1, e.color);
  }

  // nodes
  for (uint8_t i = 0; i < state.nodeCount; ++i) {
    const MapNode& n = state.nodes[i];
    int16_t x = scaleX(n.x);
    int16_t y = scaleY(n.y);
    matrix.foreground->fillCircle(x, y, 2, 0xFFFF);
    matrix.foreground->setTextColor(0xFFFF);
    matrix.foreground->setTextSize(1);
    matrix.foreground->setCursor(x + 3, y - 3);
    matrix.foreground->print(n.name);
  }

  // vehicles
  for (uint8_t i = 0; i < state.vehicleCount; ++i) {
    const MapVehicle& v = state.vehicles[i];
    if (v.edge >= state.edgeCount) continue;
    const MapEdge& e = state.edges[v.edge];
    float p = v.progress + localProgress;
    while (p > 1.0f) p -= 1.0f;
    int16_t x0 = scaleX(state.nodes[e.a].x);
    int16_t y0 = scaleY(state.nodes[e.a].y);
    int16_t x1 = scaleX(state.nodes[e.b].x);
    int16_t y1 = scaleY(state.nodes[e.b].y);
    int16_t x = x0 + (int16_t)((x1 - x0) * p);
    int16_t y = y0 + (int16_t)((y1 - y0) * p);
    matrix.foreground->fillCircle(x, y, 3, v.color);
  }
}

void TransitRenderer_renderError(AmoledMatrix& matrix, const char* message) {
  if (matrix.foreground == nullptr) return;
  matrix.foreground->fillScreen(0);
  matrix.foreground->setTextColor(0xF800);
  matrix.foreground->setTextSize(1);
  matrix.foreground->setCursor(4, 4);
  matrix.foreground->print(message);
}
```

- [ ] **Step 3: Run compile test**

Run: `task build`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add transit_display
git commit -m "feat(firmware): map renderer with vehicle dots"
```

---

### Task 6: Main integration and state machine

**Files:**
- Modify: `transit_display/transit_display.ino`

- [ ] **Step 1: Replace sketch with integrated loop**

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "AmoledMatrix.h"
#include "AmoledTouch.h"
#include "SdConfig.h"
#include "TransitConfig.h"
#include "TransitClient.h"
#include "TransitRenderer.h"
#include "TransitState.h"

AmoledMatrix matrix;
AmoledTouch touch;
TransitConfig cfg;
TransitState state;

unsigned long lastFetchMs = 0;
unsigned long lastRenderMs = 0;
float vehicleAnimation = 0.0f;

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

static char wifiSsid[64] = WIFI_SSID;
static char wifiPass[64] = WIFI_PASSWORD;

static void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(wifiSsid, wifiPass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(100);
  }
}

static void fetchIfNeeded() {
  unsigned long interval = cfg.refreshSec > 0 ? cfg.refreshSec * 1000UL : 30000UL;
  if (millis() - lastFetchMs < interval) return;
  lastFetchMs = millis();
  TransitClient_fetch(cfg, state);
}

void setup() {
  Serial.begin(115200);
  initSD();
  loadWiFiFromSD(wifiSsid, sizeof(wifiSsid), wifiPass, sizeof(wifiPass));
  connectWiFi();
  matrix.init();
  touch.init();

  if (!TransitConfig_load(cfg)) {
    TransitRenderer_renderError(matrix, "Bad config");
    matrix.update();
    while (true) { delay(1000); }
  }

  TransitState_init(state);
  TransitClient_fetch(cfg, state);
}

void loop() {
  connectWiFi();
  fetchIfNeeded();

  if (state.offline && state.nodeCount == 0) {
    TransitRenderer_renderError(matrix, state.error[0] ? state.error : "No data");
  } else {
    vehicleAnimation += 0.002f;
    if (vehicleAnimation > 1.0f) vehicleAnimation -= 1.0f;
    TransitRenderer_render(matrix, state, vehicleAnimation);
  }

  if (millis() - lastRenderMs > 33) {
    matrix.update();
    lastRenderMs = millis();
  }
  delay(5);
}
```

- [ ] **Step 2: Run compile test**

Run: `task build`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add transit_display
git commit -m "feat(firmware): main integration loop"
```

---

### Task 7: Manual QA

- [ ] **Step 1: Prepare SD card files**

Create `/config/transit_display.json` and `/settings.json` (WiFi) on the SD card.

- [ ] **Step 2: Build and flash**

Run: `task build && task flash`
Expected: success.

- [ ] **Step 3: Verify serial output**

Run: `task monitor`
Expected: logs show config loaded, Wi-Fi connected, HTTP 200, map rendered.

- [ ] **Step 4: Capture screenshot**

Run: `task screenshot`
Expected: screenshot shows black background with lines, stop dots, and moving vehicle dots.

- [ ] **Step 5: Commit QA notes**

Add a short `README.md` in `transit_display/` with flash and SD setup notes.

```bash
git add transit_display/README.md
git commit -m "docs(firmware): setup and QA notes"
```

---

## Self-review checklist

- [x] Spec coverage: config, HTTP client, cache, renderer, main loop, hardware reuse all have tasks.
- [x] Placeholder scan: no TBD/TODO/fill-in details.
- [x] Type consistency: `TransitConfig`, `TransitState`, `MapNode`, `MapEdge`, `MapVehicle` used consistently.

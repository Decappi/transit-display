// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nikita Khakham

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "AmoledMatrix.h"
#include "AmoledTouch.h"
#include "SdConfig.h"
#include "TransitConfig.h"
#include "TransitClient.h"
#include "TransitRenderer.h"
#include "TransitMath.h"
#include "TransitState.h"

// ── timing constants ──
static constexpr uint32_t RENDER_INTERVAL_MS = 33;
static constexpr uint32_t TOOLTIP_TIMEOUT_MS = 5000;
static constexpr uint32_t ANIM_DEFAULT_MS = 30000;
static constexpr uint32_t FPS_REPORT_INTERVAL_MS = 1000;

// touch / button
static constexpr int16_t  TOUCH_RADIUS = 30;
static constexpr int16_t  TOUCH_RADIUS_SQ = TOUCH_RADIUS * TOUCH_RADIUS;
static constexpr uint32_t BTN_LONG_PRESS_MS = 800;
static constexpr uint32_t BTN_MULTI_DEBOUNCE_MS = 400;
static constexpr uint32_t BTN_LONG_REPEAT_MS = 300;
static constexpr int16_t  FLASH_RADIUS = 25;
static constexpr int16_t  FLASH_CX = 224, FLASH_CY = 184;
static constexpr uint8_t  BRIGHTNESS_MIN = 10, BRIGHTNESS_MAX = 100, BRIGHTNESS_STEP = 10;

// wifi + tasks
static constexpr uint32_t WIFI_TIMEOUT_MS = 10000;
static constexpr uint32_t DEFAULT_REFRESH_MS = 30000;

// colors
static constexpr uint16_t CLR_GREEN = 0x07E0;
static constexpr uint16_t CLR_WHITE = 0xFFFF;
static constexpr uint16_t CLR_GRAY  = 0x39C7;
static constexpr uint16_t CLR_GRAY_BLINK = 0x632C;

// boot screen
static const char* const BOOT_LABELS[] = {"SD","CFG","TOUCH","WIFI","FETCH"};
static constexpr uint8_t BOOT_STEPS = 5;
static uint8_t bootStep = 0;

AmoledMatrix matrix;
AmoledTouch touch;
TransitConfig cfg;
TransitState state;
TransitState prevState;

// Background fetch handoff
static TransitState s_fetchedState;
static volatile bool s_fetchDone = false;
static volatile bool s_fetchRunning = false;

unsigned long lastRenderMs = 0;
unsigned long animStartMs = 0;
unsigned long animDurationMs = ANIM_DEFAULT_MS;

unsigned long fpsLastPrintMs = 0;
unsigned long fpsCount = 0;

// Sleep mode
static unsigned long lastActivityMs = 0;
static bool sleeping = false;

int16_t tooltipNode = -1;
int16_t tooltipTapX = 0, tooltipTapY = 0;
unsigned long tooltipUntil = 0;
char tooltipText[256];
bool wasTouching = false;

static void drawBootProgress(AmoledMatrix& matrix) {
  if (matrix.foreground == nullptr) return;
  matrix.foreground->setTextSize(2);
  const int16_t y = AmoledSettings::LOGICAL_HEIGHT / 2 + 30;
  for (uint8_t i = 0; i < BOOT_STEPS; ++i) {
    uint16_t color;
    if (i < bootStep)           color = CLR_GREEN;
    else if (i == bootStep)     color = (millis() / 300) % 2 ? CLR_WHITE : CLR_GRAY_BLINK;
    else                        color = CLR_GRAY;
    matrix.foreground->setTextColor(color);
    matrix.foreground->setCursor(4 + i * (AmoledSettings::LOGICAL_WIDTH / BOOT_STEPS), y);
    matrix.foreground->print(BOOT_LABELS[i]);
  }
  matrix.foreground->setTextColor(CLR_WHITE);
}

static void present() {
  matrix.compositeLayers();
  matrix.update();
}

static void showStatus(const char* msg) {
  TransitRenderer_renderError(matrix, msg);
  present();
  Serial.println(msg);
}

static void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("wifi: connecting to \"%s\" ...\n", cfg.wifiSsid);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
}

static void fetchTaskFunc(void* arg) {
  TransitConfig* pcfg = static_cast<TransitConfig*>(arg);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (WiFi.status() != WL_CONNECTED) continue;
    unsigned long interval = pcfg->refreshSec > 0 ? pcfg->refreshSec * 1000UL : DEFAULT_REFRESH_MS;
    static unsigned long lastFetch = 0;
    if (millis() - lastFetch < interval) continue;
    lastFetch = millis();
    s_fetchRunning = true;
    TransitState_init(s_fetchedState);
    TransitClient_fetch(*pcfg, s_fetchedState);
    s_fetchDone = true;
    s_fetchRunning = false;
  }
}

static void startFetchTask() {
  xTaskCreatePinnedToCore(fetchTaskFunc, "fetch", 16384, &cfg, 1, NULL, 0);
}

static void enterLightSleep() {
  if (sleeping) return;
  sleeping = true;
  Serial.println("sleep: entering light sleep");
  s_fetchRunning = false;
  matrix.displayOff();
  matrix.setBrightness(0);
  delay(5);
  gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(GPIO_NUM_21, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  Serial.flush();
  esp_light_sleep_start();
  // woke up
  matrix.displayOn();
  matrix.setBrightness(cfg.brightness);
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  lastActivityMs = millis();
  sleeping = false;
  Serial.println("sleep: wake");
}

static void checkFetchedData() {
  if (!s_fetchDone) return;
  s_fetchDone = false;
  unsigned long interval = cfg.refreshSec > 0 ? cfg.refreshSec * 1000UL : DEFAULT_REFRESH_MS;
  animDurationMs = interval;
  prevState = state;
  animStartMs = millis();
  bool ok = !s_fetchedState.offline && s_fetchedState.nodeCount > 0;
  if (ok) {
    bool wasZoomed = state.zoomed;
    state = s_fetchedState;
    state.zoomed = wasZoomed;
    TransitRenderer_renderStatic(matrix, state);
    matrix.precompositeBackground();
    lastActivityMs = millis();
  }
  Serial.printf("transit: ok=%d offline=%d nodes=%u edges=%u vehicles=%u err=%s\n",
                ok ? 1 : 0, s_fetchedState.offline ? 1 : 0,
                s_fetchedState.nodeCount, s_fetchedState.edgeCount,
                s_fetchedState.vehicleCount,
                s_fetchedState.error[0] ? s_fetchedState.error : "-");
}

static void handleBootButton(AmoledMatrix& matrix) {
  static unsigned long btnPressMs = 0, btnLastRelease = 0;
  static bool btnWasLow = false;
  static uint8_t btnClicks = 0;

  bool btnLow = digitalRead(BTN_BOOT) == LOW;
  unsigned long now = millis();

  if (btnLow && !btnWasLow) {
    btnPressMs = now;
    lastActivityMs = now;
  }
  if (!btnLow && btnWasLow) {
    unsigned long dt = now - btnPressMs;
    if (dt < BTN_LONG_PRESS_MS) {
      btnClicks++;
      btnLastRelease = now;
    } else {
      btnClicks = 0;
    }
  }
  btnWasLow = btnLow;

  if (btnClicks == 2 && now - btnLastRelease > BTN_MULTI_DEBOUNCE_MS) {
    btnClicks = 0;
    Serial.setTxTimeoutMs(30000);
    matrix.sendScreenshot();
    Serial.setTxTimeoutMs(0);
    for (int dy = -FLASH_RADIUS; dy <= FLASH_RADIUS; ++dy)
      for (int dx = -FLASH_RADIUS; dx <= FLASH_RADIUS; ++dx)
        if (dx*dx + dy*dy <= FLASH_RADIUS*FLASH_RADIUS)
          matrix.drawPixelRGB888(FLASH_CX + dx, FLASH_CY + dy, 0xFF, 0xFF, 0xFF);
    matrix.update();
    Serial.println("screenshot: sent");
  }
  if (btnClicks == 1 && now - btnLastRelease > BTN_MULTI_DEBOUNCE_MS && !btnLow) {
    btnClicks = 0;
    uint8_t b = matrix.getBrightness();
    b = (b >= BRIGHTNESS_MAX) ? BRIGHTNESS_MAX : b + BRIGHTNESS_STEP;
    matrix.setBrightness(b);
    Serial.printf("brightness: %u\n", b);
  }
  if (btnLow && btnWasLow && now - btnPressMs > BTN_LONG_PRESS_MS
      && (now - btnPressMs) % BTN_LONG_REPEAT_MS < 10) {
    uint8_t b = matrix.getBrightness();
    b = (b <= BRIGHTNESS_MIN) ? BRIGHTNESS_MIN : b - BRIGHTNESS_STEP;
    matrix.setBrightness(b);
    Serial.printf("brightness: %u\n", b);
  }
}

static void handleTouch(const AmoledTouchPoint& pt, bool nowTouching) {
  if (!nowTouching || wasTouching) return;
  lastActivityMs = millis();

  int16_t lx = pt.logicalX;
  int16_t ly = pt.logicalY;

  if (pt.fingerNum >= 2 && state.nodeCount > 0) {
    state.zoomed = !state.zoomed;
    TransitRenderer_renderStatic(matrix, state);
    matrix.precompositeBackground();
    prevState = state;
    animStartMs = millis();
    Serial.printf("zoom: %s\n", state.zoomed ? "on" : "off");
    return;
  }

  char combined[256] = "";
  for (uint8_t i = 0; i < state.nodeCount; ++i) {
    if (strstr(state.nodes[i].name, "(off)")) continue;
    if (state.nodes[i].id[0] == '_') continue;
    int32_t nx = scaleX(state.nodes[i].x, state.zoomed);
    int32_t ny = scaleY(state.nodes[i].y, state.zoomed);
    int32_t d2 = (lx - nx) * (lx - nx) + (ly - ny) * (ly - ny);
    if (d2 <= TOUCH_RADIUS_SQ) {
      if (combined[0]) strcat(combined, "\n");
      strcat(combined, state.nodes[i].name);
    }
  }
  for (uint8_t i = 0; i < state.vehicleCount; ++i) {
    const MapVehicle& v = state.vehicles[i];
    if (v.edge >= state.edgeCount) continue;
    const MapEdge& e = state.edges[v.edge];
    if (e.a >= state.nodeCount || e.b >= state.nodeCount) continue;
    const float p = v.progress;
    const float vx = (float)state.nodes[e.a].x + ((float)state.nodes[e.b].x - (float)state.nodes[e.a].x) * p;
    const float vy = (float)state.nodes[e.a].y + ((float)state.nodes[e.b].y - (float)state.nodes[e.a].y) * p;
    int32_t vlx = scaleX((int16_t)vx, state.zoomed);
    int32_t vly = scaleY((int16_t)vy, state.zoomed);
    int32_t d2 = (lx - vlx) * (lx - vlx) + (ly - vly) * (ly - vly);
    if (d2 <= TOUCH_RADIUS_SQ) {
      if (combined[0]) strcat(combined, "\n");
      strcat(combined, v.line);
      strcat(combined, " ");
      strcat(combined, v.direction);
    }
  }

  if (combined[0]) {
    tooltipNode = 1;
    strlcpy(tooltipText, combined, sizeof(tooltipText));
  } else {
    tooltipNode = -1;
  }
  tooltipTapX = lx;
  tooltipTapY = ly;
  tooltipUntil = millis() + TOOLTIP_TIMEOUT_MS;
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // non-blocking serial (drops data if buffer full)
  delay(200);
  Serial.println("boot");

  {
    unsigned long t = millis();
    bootStep = 0;
    if (!initSD()) {
      Serial.println("sd: mount failed");
    }
    Serial.printf("t_sd=%ums\n", millis() - t);
  }

  matrix.init();
  if (!matrix.isReady()) {
    Serial.println("display: init failed");
  }
  showStatus("Loading...");
  {
    unsigned long t = millis();
    bootStep = 1; drawBootProgress(matrix); present();
    Serial.printf("t_splash=%ums\n", millis() - t);
  }

  {
    unsigned long t = millis();
    bootStep = 1; drawBootProgress(matrix); present();
    if (!TransitConfig_load(cfg)) {
      showStatus("Bad config");
      while (true) { delay(1000); }
    }
    bootStep = 1; drawBootProgress(matrix); present();
    Serial.printf("t_cfg=%ums profile=%s bridge=%s refresh=%us\n",
                  millis() - t, cfg.profile, cfg.bridgeUrl, cfg.refreshSec);
  }

  pinMode(BTN_BOOT, INPUT_PULLUP);

  {
    unsigned long t = millis();
    bootStep = 2; drawBootProgress(matrix); present();
    touch.init();
    touch.startBackgroundTask();
    bootStep = 2; drawBootProgress(matrix); present();
    Serial.printf("t_touch=%ums\n", millis() - t);
  }

  {
    unsigned long t = millis();
    bootStep = 3; drawBootProgress(matrix); present();
    connectWiFi();
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < WIFI_TIMEOUT_MS) {
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("wifi: connected %s (%lums)\n", WiFi.localIP().toString().c_str(), millis() - t);
    } else {
      Serial.println("wifi: failed");
    }
    bootStep = 3; drawBootProgress(matrix); present();
    Serial.printf("t_wifi=%ums\n", millis() - t);
  }

  ArduinoOTA.setHostname("transit-display");
  ArduinoOTA.setPort(8266);
  ArduinoOTA.onStart([]() {
    Serial.println("ota: start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("ota: done");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("ota: %u%%\r", progress / (total / 100));
  });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("ota: error %d\n", err);
  });
  ArduinoOTA.begin();
  Serial.printf("ota: ready on %s\n", WiFi.localIP().toString().c_str());

  TransitState_init(state);
  TransitState_init(prevState);

  {
    unsigned long t = millis();
    const unsigned long initInterval = cfg.refreshSec > 0 ? cfg.refreshSec * 1000UL : DEFAULT_REFRESH_MS;
    animDurationMs = initInterval;
    bootStep = 4; drawBootProgress(matrix); present();
    TransitClient_fetch(cfg, state);
    if (!state.offline && state.nodeCount > 0) {
      TransitRenderer_renderStatic(matrix, state);
      matrix.precompositeBackground();
      matrix.copyBackground();
      matrix.update();
      matrix.setBrightness(cfg.brightness);
    }
    lastActivityMs = millis();
    animStartMs = millis();
    bootStep = 5; drawBootProgress(matrix); present();
    Serial.printf("t_fetch=%ums offline=%d nodes=%u edges=%u err=%s\n",
                  millis() - t,
                  state.offline ? 1 : 0, state.nodeCount, state.edgeCount,
                  state.error[0] ? state.error : "-");
  }
  startFetchTask();
}

void loop() {
  connectWiFi();
  ArduinoOTA.handle();
  checkFetchedData();
  handleBootButton(matrix);

  const AmoledTouchPoint& pt = touch.take();
  bool nowTouching = pt.pressed;
  if (nowTouching && !wasTouching) {
    handleTouch(pt, nowTouching);
  }

  bool useFastPath = (state.nodeCount > 0 && !state.offline);

  if (millis() - lastRenderMs > RENDER_INTERVAL_MS) {
    if (useFastPath) {
      matrix.copyBackground();
      float t = (float)(millis() - animStartMs) / (float)animDurationMs;
      if (t > 1.0f) t = 1.0f;
      TransitRenderer_renderVehiclesDirect(matrix, state, prevState, t);
      if (tooltipNode >= 0 && millis() < tooltipUntil) {
        TransitRenderer_renderTooltipDirect(matrix, tooltipText, tooltipTapX, tooltipTapY);
      }
      if (millis() < tooltipUntil) {
        const int16_t cx = tooltipTapX, cy = tooltipTapY;
        for (int16_t dy = -TOUCH_RADIUS; dy <= TOUCH_RADIUS; ++dy) {
          for (int16_t dx = -TOUCH_RADIUS; dx <= TOUCH_RADIUS; ++dx) {
            const int32_t d2 = dx*dx + dy*dy;
            const int32_t r2 = TOUCH_RADIUS * TOUCH_RADIUS;
            const int32_t ir2 = (TOUCH_RADIUS - 1) * (TOUCH_RADIUS - 1);
            if (d2 <= r2 && d2 > ir2) {
              matrix.drawPixelRGB888(cx + dx, cy + dy, 0xF8, 0x00, 0x00);
            }
          }
        }
      } else {
        tooltipNode = -1;
      }
      matrix.update();
    } else if (state.offline && state.nodeCount == 0) {
      TransitRenderer_renderError(matrix, state.error[0] ? state.error : "No data");
      present();
    } else if (state.nodeCount == 0) {
      TransitRenderer_renderError(matrix, "No transit nearby");
      present();
    }
    lastRenderMs = millis();
  }

  if (cfg.sleepTimeout > 0 && !sleeping && state.nodeCount > 0) {
    if (millis() - lastActivityMs > (unsigned long)cfg.sleepTimeout * 1000UL) {
      enterLightSleep();
      lastRenderMs = 0;
    }
  }

  ++fpsCount;
  if (millis() - fpsLastPrintMs >= FPS_REPORT_INTERVAL_MS) {
    Serial.printf("fps=%lu\n", fpsCount);
    fpsCount = 0;
    fpsLastPrintMs = millis();
  }
  wasTouching = nowTouching;
}

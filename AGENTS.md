# Transit Display — AI Development Context

## Hardware Platform

Waveshare ESP32-S3-Touch-AMOLED-1.8.
CPU: ESP32-S3 dual-core Xtensa LX7, 240 MHz.
PSRAM: 8 MB Octal PSRAM (used for framebuffers).
Flash: 16 MB.

### Display
- Controller: CO5300 AMOLED, 368×448.
- Interface: QSPI (Arduino_ESP32QSPI driver).
- QSPI clock: 60 MHz (stable; default was 40 MHz).
- DMA buffer (`ESP32QSPI_MAX_PIXELS_AT_ONCE`): currently 16000 (64 KB, 2 buffers × 16000 × 2 bytes). 18000 fails.
- Partial flush (32 rows): ~449 fps. Full-screen flush: ~72 fps.

### Touch (CST816S)
- I²C address 0x15, interrupt on FALLING edge.
- REG_CHIP_ID 0xA7, REG_FINGER_NUM 0x02.
- **Two-finger (P2) detection critical**: REG_FINGER_NUM always reports 1 even with 2 fingers.
- P2 data in regs[9..12] (nibble-packed). Heuristic: regs[9]!=0xFF && regs[12]!=0xFF, coords within 368×448, not same as P1 within 5px.
- If P2 heuristic passes, `fingerNum=2` set manually. Used for zoom toggle.
- Interrupt pin TP_INT (GPIO21). Background task polls every 10ms.

### PMU (AXP2101)
- Manages power, battery monitoring.
- ALDO1/ALDO2 must be cycled on warm boot to force CO5300 reset.

### SD Card
- SDMMC 1-bit mode (CLK=GPIO2, CMD=GPIO1, D0=GPIO3).
- TCA9554 I²C GPIO expander configures EXIO7 for DAT3 pull-up.
- Config at `/.config/settings.json`.

## Display pipeline

```
GFX_Layer(background)  ──► precompositeBackground() ──► bgFrameBuffer (PSRAM)
                                                              │
GFX_Layer(foreground)  ──► foreground composited on demand    │
                                                              ▼
                                                      frameBuffer (PSRAM)
                                                              │
                                                      gfx->draw16bitRGBBitmap()
                                                              │
                                                         QSPI → CO5300
```

**Two render paths:**
1. **Fast path** (default when data available):
   - `copyBackground()` — memcpy bgFrameBuffer → frameBuffer
   - Draw vehicles/tooltip directly via `drawPixelRGB888`
   - `matrix.update()` — flush to display
2. **Slow path** (GFX_Layer compositor):
   - `compositeLayers()` — iterate every pixel, blend foreground/background
   - Used only for error screens

### Direct Send Optimization
- Original `writePixels`: copies via DMA buffer with MSB_32_16_16_SET → 31 fps.
- Modified: sends directly from source buffer (no intermediate copy) → 71 fps (2.3×).
- Direct send requires **big-endian** byte order (hi_byte, lo_byte per pixel).
- Modified `writePixels` at: `/home/decappi/Arduino/libraries/GFX_Library_for_Arduino/src/databus/Arduino_ESP32QSPI.cpp`

### Byte order note
| Format | bytes in memory | direct send | MSB_32_16_16_SET |
|--------|----------------|-------------|------------------|
| little-endian | lo, hi | wrong colors | correct |
| big-endian | hi, lo | correct | wrong colors |

## Coordinate System

**Map coords**: [0..255] × [0..255] (8-bit).
**Zoom IN**: [64..191] → full screen (2× magnification of center).
**Scale formula (non-zoomed)**: `v * (screen-1) / 255`
**Scale formula (zoomed)**: `(v - 64) * (screen-1) / 127`
**Logical screen**: 448 × 368 (after software rotation from physical 368×448).
**Software rotation**: 90° CW + horizontal mirror — physical (x,y) → logical (y, 367-x).

Shared math functions in `TransitMath.h` (float + int32 variants).

## Touch → logical coordinate mapping

```
logicalX = rawY / SCALE
logicalY = LOGICAL_H - 1 - (rawX / SCALE)
```

Edge correction applied in `AmoledTouch::correct_edge()` — cubic push from edges to compensate for touch sensor dead zone.

## Backend Architecture

Python FastAPI (uvicorn). Two modes:

### Demo mode (`DEMO_MODE=1`)
Synthetic map of Berlin transit (S1, U2, U8, Tram, Bus) with animated vehicles at constant speed. No VBB API needed.

### Live mode
1. Load profile from `config/profiles.json`.
2. For each profile stop: fetch stop metadata + departures from BVG HAFAS (direct, no transport.rest).
3. Fetch trip details for each unique tripId.
4. Build schematic map: project stops to 8-bit coords, draw edges from trip stopovers.
5. Position vehicles on edges by interpolating scheduled arrival/departure.

### VBB/HAFAS client (`vbb_client.py`)
- Endpoint: `https://bvg.hafas.cloud/apps/gate`
- Auth: AID token (hardcoded — same as BVG mobile app).
- Product classes: 1=suburban, 2=subway, 4=tram, 8=bus, 16=ferry, 32=express, 64=regional.
- In-memory TTL caches: stop (300s), departures (30s), trip (30s).
- **Important**: `_now_berlin()` uses `ZoneInfo("Europe/Berlin")` — do NOT hardcode UTC+2 (wrong in winter).

### Vehicle tracker (`vehicle_tracker.py`)
- For each departure: find which edge the vehicle is on based on scheduled stop times.
- Progress = elapsed time between stops / total scheduled time.
- Clamped to [0.0, 1.0].

## Touch behavior in firmware

1. `AmoledTouch::take()` returns latest point (non-blocking, from background task).
2. On press start → check fingerNum:
   - `fingerNum >= 2` → toggle zoom
   - else → hit-test nodes (radius 30 logical px), then vehicles (same radius)
   - if hit found → show tooltip (5s timeout)
3. Direct rendering loop (33ms interval):
   - copyBackground
   - renderVehiclesDirect (with progress interpolation)
   - renderTooltipDirect (selective composite from foreground layer)
   - draw tooltip ring

## Sleep

- Light sleep after `sleep_timeout` seconds of inactivity.
- Wake on GPIO0 (BOOT) or GPIO21 (touch) LOW level.
- displayOff()/displayOn() (not sleep/wake on CO5300).
- Brightness restored on wake.

## Known quirks

- `ESP32QSPI_MAX_PIXELS_AT_ONCE=16000` max for DMA; 18000 crashes.
- `Serial.setTxTimeoutMs(0)` for non-blocking serial during rendering. Must set 30000 before screenshot.
- WiFi reconnect on wake is blocking — display stays black briefly.
- CST816S sometimes misses release edge on P2 → touch state can "stick". Background task polling every 10ms mitigates this.
- `getXResolution`/`getYResolution` return `uint8_t` (base class constraint) — truncates values >255 (448/368). Callers should use `AmoledSettings` constants directly.

## Build/flash workflow

### Inside container
```bash
# Build
arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600,LoopCore=1,EventsCore=1" --output-dir build .
```

### On host (flash)
```fish
set PROJECT transit_display
python3 -m esptool --chip esp32s3 \
  --port /dev/serial/by-id/REPLACE_WITH_YOUR_PORT \
  write-flash 0x0 \
  build/esp32.esp32.esp32s3/$PROJECT.ino.merged.bin
```

## Profile config (SD card)

```json
{
  "wifi": { "ssid": "...", "password": "..." },
  "transit_display": {
    "profile": "home",
    "refresh_sec": 30,
    "stale_after_sec": 120,
    "sleep_timeout": 300,
    "bridge_url": "http://192.168.0.10:8000",
    "bridge_token": "change-me",
    "brightness": 100
  }
}
```

Backend profiles in `config/profiles.json` define which stops+lines to track.

## Files reference

| File | Purpose |
|------|---------|
| `transit_display.ino` | Main loop, touch handling, sleep, OTA |
| `AmoledMatrix.cpp/h` | Display driver (double buffer, rotation, screenshot) |
| `AmoledTouch.cpp/h` | CST816S driver + P2 heuristic |
| `AmoledSettings.h` | Display constants, coordinate mapping |
| `TransitState.h/cpp` | Data structs (MapNode, MapEdge, MapVehicle) |
| `TransitConfig.h/cpp` | SD config parser |
| `TransitClient.h/cpp` | HTTP bridge client + cache |
| `TransitRenderer.h/cpp` | Map rendering |
| `TransitMath.h` | Shared scale functions |
| `TransportIcons.h` | 12×12 bitmap icons |
| `SdConfig.h/cpp` | SD init + WiFi-from-SD |
| `pin_config.h` | Pin definitions |
| `Matrix.h` | Abstract base class (from aquarium/) |
| `lib/Matrix/` | GFX_Layer compositor library |
| `backend/src/transit_display_backend/main.py` | FastAPI server |
| `backend/src/transit_display_backend/config.py` | Profile loading |
| `backend/src/transit_display_backend/models.py` | Pydantic models |
| `backend/src/transit_display_backend/map_builder.py` | Map construction |
| `backend/src/transit_display_backend/vbb_client.py` | HAFAS client |
| `backend/src/transit_display_backend/vehicle_tracker.py` | Vehicle positioning |

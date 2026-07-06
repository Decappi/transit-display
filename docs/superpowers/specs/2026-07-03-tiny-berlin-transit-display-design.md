# Tiny Berlin Transit Display — Design Spec

Standalone app for Waveshare ESP32-S3-Touch-AMOLED-1.8. Shows a schematic Mini Metro-style map of nearby Berlin public transport, with animated dots representing vehicles.

## Decisions made during design

- Backend bridge is implemented in this repo as a Dockerized Python FastAPI service.
- ESP loads backend URL and auth token from SD card config.
- No idle scene in the first version.
- No weather row in the first version.
- UI is a **map**, not a departure list. The original departure-list UI is replaced.
- Vehicle positions are interpolated from departure/trip timetable data (approach 3), not live GPS/radar.
- Map style: **straight geographic-ish lines** (option A), not orthogonal mini-metro lines.
- First draft has no touch interaction with the map.

## Purpose

Show a compact always-on Berlin transit map:

- nearby U-Bahn, S-Bahn, bus, tram lines;
- moving vehicle dots on a schematic map;
- nearest 2–3 stops visible;
- offline/stale indicator;
- clock and status bar.

## Hardware target

- Waveshare ESP32-S3-Touch-AMOLED-1.8
- Display: 368×448 portrait AMOLED
- SD card for config and cache
- Reuse AMOLED init, touch, and SD code from `aquarium/`.

## Project layout

```text
transit_display/
├── transit_display.ino          # setup/loop, Wi-Fi/NTP, state machine
├── TransitConfig.h/.cpp         # SD config parsing
├── TransitClient.h/.cpp         # HTTP fetch + cache save/load
├── TransitState.h/.cpp          # current map + stale/offline flags
├── TransitRenderer.h/.cpp       # map rendering + local animation
├── AmoledMatrix.h/.cpp          # copied from aquarium (hardware)
├── AmoledTouch.h/.cpp           # copied from aquarium (hardware)
├── SdConfig.h/.cpp              # copied from aquarium (hardware)
├── AmoledSettings.h             # copied from aquarium (hardware)
├── pin_config.h                 # copied from aquarium (hardware)
├── Taskfile.yml                 # build/flash/screenshot/monitor
├── compile.sh                   # arduino-cli build script
├── backend/
│   ├── Dockerfile
│   ├── compose.yml
│   ├── main.py                  # FastAPI app
│   ├── models.py                # Pydantic models
│   ├── config.py                # profile + token loading
│   ├── vbb_client.py            # VBB REST client
│   ├── map_builder.py           # graph projection
│   ├── vehicle_tracker.py       # interpolate vehicle positions
│   ├── cache.py                 # in-memory TTL cache
│   └── tests/                   # pytest suite
└── docs/superpowers/specs/2026-07-03-tiny-berlin-transit-display-design.md
```

## App config

Single SD card file with lowercase keys:

```text
/.config/settings.json
```

Example (see `example_settings.json`):

```json
{
  "wifi": {
    "ssid": "YOUR_SSID",
    "password": "YOUR_PASSWORD"
  },
  "transit_display": {
    "profile": "home",
    "refresh_sec": 30,
    "stale_after_sec": 120,
    "bridge_url": "http://192.168.0.10:8000",
    "bridge_token": "change-me"
  }
}
```

## Backend endpoint

```http
GET /api/transit?profile=home
Authorization: Bearer <bridge_token>
```

Expected response:

```json
{
  "updated_at": "2026-07-03T19:30:00+02:00",
  "profile": "home",
  "map": {
    "nodes": [
      {"id": "900017101", "name": "U Mehringdamm", "x": 128, "y": 200},
      {"id": "900017102", "name": "U Hallesches Tor", "x": 128, "y": 80}
    ],
    "edges": [
      {"a": 0, "b": 1, "color": "#0066cc"}
    ],
    "vehicles": [
      {"line": "U6", "direction": "Alt-Tegel", "edge": 0, "progress": 0.4, "color": "#0066cc"}
    ]
  }
}
```

Coordinates are normalized to 0–255. The renderer maps the normalized `(x, y)` to the 368×448 framebuffer by scaling each axis independently.

## Backend config

Backend loads profiles from `/app/config/profiles.json` (mounted into the Docker container). The file has the same shape as the `profiles` object in the ESP config:

```json
{
  "profiles": {
    "home": {
      "title": "Berlin",
      "stops": [
        {"id": "900017101", "name": "U Mehringdamm", "lines": ["U6", "U7"]}
      ]
    }
  }
}
```

The bearer token is set via the `BRIDGE_TOKEN` environment variable.

## Backend behavior

1. Load the requested profile from `profiles.json`.
2. For each configured stop, call `GET /stops/:id` to get coordinates.
3. For each configured stop, call `GET /stops/:id/departures?results=4&duration=20`.
4. Deduplicate `tripId`s, then fetch `GET /trips/:id?stopovers=true&polyline=true` for each unique trip.
5. Cache VBB responses in memory for 30 seconds.
6. Build a graph:
   - nodes = center stop + up to 2–3 nearest stops total across all configured lines;
   - edges = straight segments of the trip polyline between consecutive stops in the trip sequence, keeping the line connected even when intermediate stops are not labeled;
   - each edge carries the line color.
7. For each active departure/trip:
   - find the previous and next stop in the trip stopovers;
   - locate the edge (or sequence of edges) between those two stops;
   - interpolate `progress` along that edge from `previousWhen` to `nextWhen` (using realtime times when available).
8. Return the compact `map` object.

## Product types and line colors

Normalize products into: `subway`, `suburban`, `tram`, `bus`, `regional`, `ferry`, `unknown`.

Backend uses VBB line metadata for colors. If a line has no color, fall back to:

| product | fallback color |
|---|---|
| subway | `#0066cc` |
| suburban | `#008f00` |
| tram | `#cc0000` |
| bus | `#ff8800` |
| regional | `#6e6e6e` |
| ferry | `#00aaff` |
| unknown | `#aaaaaa` |

## UI

Display target: 368×448 portrait.

Main view:

- black background;
- straight colored edges between stops;
- white/gray stop dots, labeled with small text for the center + 2–3 nearest stops;
- animated colored dots for vehicles;
- small clock in a corner;
- status bar: last update age, stale/offline indicator.

No touch interaction in the first draft.

## Cache

Cache path:

```text
/cache/last_transit.json
```

Behavior:

- Save the last successful backend response.
- On bridge failure, render the cached map with a stale indicator.
- If no cache exists, show an error screen.

## Error states

| state | screen |
|---|---|
| No Wi-Fi | "No Wi-Fi" + retry countdown |
| Bridge unreachable | "Bridge offline" + cached map if any |
| HTTP 401/403 | "Auth failed" |
| Invalid profile/stop | "Bad config" |
| VBB API error | "Transit data unavailable" + cache |
| No transit in range | "No transit nearby" |
| Invalid JSON | "Data error" + cache |
| Stale data | map with stale indicator |

## ESP data flow

1. Boot → mount SD → read `/.config/settings.json`.
2. Connect Wi-Fi (10 s timeout).
3. Fetch `/api/transit?profile=...`.
4. Save response to `/cache/last_transit.json`.
5. Render map.
6. Repeat fetch every `refresh_sec`.
7. Between fetches, advance vehicle dots locally along their edges for smooth animation.

## Testing plan

- Backend unit tests with mocked VBB responses:
  - profile loading;
  - map graph construction;
  - vehicle interpolation;
  - auth middleware.
- ESP compile test via `arduino-cli`.
- Manual QA on device: verify map renders, dots animate, stale/offline screens appear.

## Not in scope

- Idle scene / aquarium view.
- Weather row.
- Touch interaction with the map.
- Live GPS vehicle positions (radar / GTFS-RT VehiclePositions).
- Orthogonal mini-metro layout generation.
- Official VBB API direct coupling on the ESP.

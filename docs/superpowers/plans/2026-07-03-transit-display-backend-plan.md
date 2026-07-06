# Transit Display Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB- SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans`. Steps use checkbox syntax for tracking.

**Goal:** Build a Dockerized FastAPI backend that proxies/normalizes VBB data into a small `map` JSON for the ESP32 transit display.

**Architecture:** FastAPI app exposes one authenticated endpoint. A thin httpx client fetches VBB stop, departure, and trip data with an in-memory TTL cache. A map builder projects nearby stops into normalized coordinates and a vehicle tracker interpolates each active trip along its current edge. Pydantic models enforce the contract.

**Tech Stack:** Python 3.12, FastAPI, Uvicorn, httpx, Pydantic, pytest, respx, uv.

---

## File Structure

```text
transit_display/backend/
├── pyproject.toml
├── Dockerfile
├── compose.yml
├── .python-version
├── src/
│   ├── transit_display_backend/
│   │   ├── __init__.py
│   │   ├── main.py              # FastAPI app + endpoint
│   │   ├── config.py            # profile loader + token
│   │   ├── models.py            # Pydantic request/response models
│   │   ├── vbb_client.py        # httpx VBB client with TTL cache
│   │   ├── map_builder.py       # graph projection
│   │   └── vehicle_tracker.py   # interpolate positions
│   └── tests/
│       ├── test_config.py
│       ├── test_vbb_client.py
│       ├── test_map_builder.py
│       ├── test_vehicle_tracker.py
│       └── test_main.py
```

---

### Task 1: Project skeleton and config loader

**Files:**
- Create: `transit_display/backend/pyproject.toml`
- Create: `transit_display/backend/.python-version`
- Create: `transit_display/backend/src/transit_display_backend/__init__.py`
- Create: `transit_display/backend/src/transit_display_backend/config.py`
- Create: `transit_display/backend/src/tests/test_config.py`

- [ ] **Step 1: Write the failing test**

```python
# src/tests/test_config.py
from transit_display_backend.config import load_profiles, get_token

def test_load_profiles_reads_file(tmp_path):
    config_path = tmp_path / "profiles.json"
    config_path.write_text('{"profiles":{"home":{"stops":[{"id":"x","name":"X","lines":[]}]}}}')
    profiles = load_profiles(config_path)
    assert "home" in profiles
    assert profiles["home"].stops[0].id == "x"

def test_get_token_from_env(monkeypatch):
    monkeypatch.setenv("BRIDGE_TOKEN", "secret")
    assert get_token() == "secret"
```

Run: `uv run pytest src/tests/test_config.py -v`
Expected: FAIL (module/config not defined)

- [ ] **Step 2: Create project files**

```toml
# pyproject.toml
[project]
name = "transit-display-backend"
version = "0.1.0"
requires-python = ">=3.12"
dependencies = [
  "fastapi>=0.111",
  "httpx>=0.27",
  "pydantic>=2.7",
  "uvicorn>=0.30",
]

[project.optional-dependencies]
dev = ["pytest>=8", "respx>=0.21"]

[tool.pytest.ini_options]
testpaths = ["src/tests"]
```

```text
# .python-version
3.12
```

- [ ] **Step 3: Implement config loader**

```python
# src/transit_display_backend/config.py
import os
from pathlib import Path
from pydantic import BaseModel

class StopConfig(BaseModel):
    id: str
    name: str
    lines: list[str]

class ProfileConfig(BaseModel):
    title: str
    stops: list[StopConfig]

class ProfilesFile(BaseModel):
    profiles: dict[str, ProfileConfig]

DEFAULT_PROFILES_PATH = Path("/app/config/profiles.json")

def load_profiles(path: Path | None = None) -> dict[str, ProfileConfig]:
    target = path or DEFAULT_PROFILES_PATH
    data = ProfilesFile.model_validate_json(target.read_text())
    return data.profiles

def get_token() -> str:
    return os.environ["BRIDGE_TOKEN"]
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `uv run pytest src/tests/test_config.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add transit_display/backend
git commit -m "feat(backend): project skeleton and config loader"
```

---

### Task 2: VBB client with TTL cache

**Files:**
- Create: `transit_display/backend/src/transit_display_backend/vbb_client.py`
- Create: `transit_display/backend/src/tests/test_vbb_client.py`

- [ ] **Step 1: Write the failing test**

```python
# src/tests/test_vbb_client.py
import httpx
import respx
from transit_display_backend.vbb_client import VbbClient

@respx.mock
def test_fetch_stop():
    route = respx.get("https://v6.vbb.transport.rest/stops/900017101").mock(
        return_value=httpx.Response(200, json={"id": "900017101", "name": "U Mehringdamm", "location": {"latitude": 52.49357, "longitude": 13.388138}})
    )
    client = VbbClient()
    stop = client.fetch_stop_sync("900017101")
    assert stop["name"] == "U Mehringdamm"
    assert route.called
```

Run: `uv run pytest src/tests/test_vbb_client.py -v`
Expected: FAIL

- [ ] **Step 2: Implement VBB client**

```python
# src/transit_display_backend/vbb_client.py
import time
from functools import wraps
import httpx

BASE_URL = "https://v6.vbb.transport.rest"
TTL_SECONDS = 30

def _cached(ttl: int):
    store: dict = {}
    def decorator(fn):
        @wraps(fn)
        def wrapper(*args):
            key = (fn.__name__, args)
            now = time.monotonic()
            if key in store and now - store[key][0] < ttl:
                return store[key][1]
            result = fn(*args)
            store[key] = (now, result)
            return result
        return wrapper
    return decorator

class VbbClient:
    def __init__(self):
        self.client = httpx.Client(base_url=BASE_URL, timeout=15.0)

    @_cached(TTL_SECONDS)
    def fetch_stop_sync(self, stop_id: str) -> dict:
        r = self.client.get(f"/stops/{stop_id}")
        r.raise_for_status()
        return r.json()

    @_cached(TTL_SECONDS)
    def fetch_departures_sync(self, stop_id: str, results: int = 4, duration: int = 20) -> list[dict]:
        r = self.client.get(
            f"/stops/{stop_id}/departures",
            params={"results": results, "duration": duration},
        )
        r.raise_for_status()
        return r.json().get("departures", [])

    @_cached(TTL_SECONDS)
    def fetch_trip_sync(self, trip_id: str) -> dict:
        r = self.client.get(
            f"/trips/{trip_id}",
            params={"stopovers": "true", "polyline": "true"},
        )
        r.raise_for_status()
        return r.json()
```

- [ ] **Step 3: Run tests to verify they pass**

Run: `uv run pytest src/tests/test_vbb_client.py -v`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add transit_display/backend/src
git commit -m "feat(backend): VBB client with TTL cache"
```

---

### Task 3: Map builder

**Files:**
- Create: `transit_display/backend/src/transit_display_backend/models.py`
- Create: `transit_display/backend/src/transit_display_backend/map_builder.py`
- Create: `transit_display/backend/src/tests/test_map_builder.py`

- [ ] **Step 1: Write the failing test**

```python
# src/tests/test_map_builder.py
from transit_display_backend.models import ProfileConfig, StopConfig
from transit_display_backend.map_builder import build_map

def test_build_map_basic():
    profile = ProfileConfig(title="home", stops=[StopConfig(id="a", name="A", lines=["U6"])])
    stop_meta = {"a": {"id": "a", "name": "A", "location": {"latitude": 52.5, "longitude": 13.4}}}
    trip_data = {
        "t1": {
            "line": {"name": "U6", "product": "subway", "color": {"fg": "#fff", "bg": "#0066cc"}},
            "polyline": {"features": [{"geometry": {"coordinates": [[13.4, 52.5], [13.41, 52.51]]}}]},
            "stopovers": [
                {"stop": {"id": "a", "name": "A"}, "arrival": "2026-07-03T10:00:00+02:00"},
                {"stop": {"id": "b", "name": "B"}, "arrival": "2026-07-03T10:05:00+02:00"},
            ],
        }
    }
    departures = [{"tripId": "t1", "line": {"name": "U6"}, "when": "2026-07-03T10:02:00+02:00"}]
    result = build_map(profile, stop_meta, trip_data, departures)
    assert result.profile == "home"
    assert len(result.map.nodes) >= 2
    assert len(result.map.edges) == 1
```

Run: `uv run pytest src/tests/test_map_builder.py -v`
Expected: FAIL

- [ ] **Step 2: Implement models and map builder**

```python
# src/transit_display_backend/models.py
from pydantic import BaseModel

class MapNode(BaseModel):
    id: str
    name: str
    x: int
    y: int

class MapEdge(BaseModel):
    a: int
    b: int
    color: str

class MapVehicle(BaseModel):
    line: str
    direction: str
    edge: int
    progress: float
    color: str

class MapData(BaseModel):
    nodes: list[MapNode]
    edges: list[MapEdge]
    vehicles: list[MapVehicle]

class TransitResponse(BaseModel):
    updated_at: str
    profile: str
    map: MapData
```

```python
# src/transit_display_backend/map_builder.py
from datetime import datetime, timezone
from transit_display_backend.models import MapNode, MapEdge, MapData, TransitResponse
from transit_display_backend.config import ProfileConfig

MARGIN = 20
MAX_NODES = 4  # center + up to 3 nearest

PRODUCT_FALLBACK = {
    "subway": "#0066cc",
    "suburban": "#008f00",
    "tram": "#cc0000",
    "bus": "#ff8800",
    "regional": "#6e6e6e",
    "ferry": "#00aaff",
    "unknown": "#aaaaaa",
}

def _line_color(line: dict) -> str:
    color = line.get("color") or {}
    bg = color.get("bg")
    if bg:
        return bg
    return PRODUCT_FALLBACK.get(line.get("product", "unknown"), "#aaaaaa")

def _project_nodes(nodes_lonlat: dict[str, tuple[float, float]]) -> dict[str, tuple[int, int]]:
    lats = [ll[1] for ll in nodes_lonlat.values()]
    lons = [ll[0] for ll in nodes_lonlat.values()]
    min_lat, max_lat = min(lats), max(lats)
    min_lon, max_lon = min(lons), max(lons)
    def norm_x(lon: float) -> int:
        if max_lon == min_lon:
            return 128
        return int(MARGIN + (lon - min_lon) / (max_lon - min_lon) * (255 - 2 * MARGIN))
    def norm_y(lat: float) -> int:
        if max_lat == min_lat:
            return 128
        return int(MARGIN + (max_lat - lat) / (max_lat - min_lat) * (255 - 2 * MARGIN))
    return {sid: (norm_x(lon), norm_y(lat)) for sid, (lon, lat) in nodes_lonlat.items()}

def build_map(
    profile: ProfileConfig,
    stop_meta: dict[str, dict],
    trip_data: dict[str, dict],
    departures: list[dict],
    profile_name: str = "home",
) -> TransitResponse:
    # collect candidate stop coordinates from trip stopovers
    center_id = profile.stops[0].id
    center = stop_meta[center_id]
    coords: dict[str, tuple[float, float]] = {
        center_id: (
            center["location"]["longitude"],
            center["location"]["latitude"],
        )
    }
    stop_dists: dict[str, float] = {}
    c_lon, c_lat = coords[center_id]
    for trip in trip_data.values():
        for stopover in trip.get("stopovers", []):
            stop = stopover.get("stop") or {}
            sid = stop.get("id")
            loc = stop.get("location")
            if not sid or not loc or sid in coords:
                continue
            coords[sid] = (loc["longitude"], loc["latitude"])
            stop_dists[sid] = ((coords[sid][0] - c_lon) ** 2 + (coords[sid][1] - c_lat) ** 2) ** 0.5

    # keep center + nearest stops up to MAX_NODES
    nearest = sorted(stop_dists.items(), key=lambda kv: kv[1])[: MAX_NODES - 1]
    selected_ids = {center_id} | {sid for sid, _ in nearest}

    projected = _project_nodes({sid: coords[sid] for sid in selected_ids})
    nodes = [MapNode(id=sid, name=stop_meta.get(sid, {}).get("name", sid), x=p[0], y=p[1]) for sid, p in projected.items()]
    node_index = {n.id: i for i, n in enumerate(nodes)}

    edges: list[MapEdge] = []
    for trip in trip_data.values():
        line = trip.get("line", {})
        color = _line_color(line)
        stopovers = trip.get("stopovers", [])
        prev_idx = None
        for stopover in stopovers:
            sid = (stopover.get("stop") or {}).get("id")
            if sid in node_index:
                if prev_idx is not None and prev_idx != node_index[sid]:
                    edges.append(MapEdge(a=prev_idx, b=node_index[sid], color=color))
                prev_idx = node_index[sid]

    updated_at = datetime.now(timezone.utc).isoformat()
    return TransitResponse(
        updated_at=updated_at,
        profile=profile_name,
        map=MapData(nodes=nodes, edges=edges, vehicles=[]),
    )
```

- [ ] **Step 3: Run tests to verify they pass**

Run: `uv run pytest src/tests/test_map_builder.py -v`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add transit_display/backend/src
git commit -m "feat(backend): map builder with projection"
```

---

### Task 4: Vehicle tracker

**Files:**
- Create: `transit_display/backend/src/transit_display_backend/vehicle_tracker.py`
- Create: `transit_display/backend/src/tests/test_vehicle_tracker.py`

- [ ] **Step 1: Write the failing test**

```python
# src/tests/test_vehicle_tracker.py
from datetime import datetime, timezone
from transit_display_backend.models import MapData, MapNode, MapEdge
from transit_display_backend.vehicle_tracker import track_vehicles

def test_track_vehicles_between_stops():
    map_data = MapData(
        nodes=[MapNode(id="a", name="A", x=0, y=0), MapNode(id="b", name="B", x=255, y=0)],
        edges=[MapEdge(a=0, b=1, color="#0066cc")],
        vehicles=[],
    )
    trip = {
        "line": {"name": "U6", "product": "subway"},
        "stopovers": [
            {"stop": {"id": "a"}, "arrival": "2026-07-03T10:00:00+02:00"},
            {"stop": {"id": "b"}, "arrival": "2026-07-03T10:10:00+02:00"},
        ],
    }
    now = datetime(2026, 7, 3, 8, 5, 0, tzinfo=timezone.utc)  # 10:05 Berlin
    vehicles = track_vehicles(map_data, {"t1": trip}, [{"tripId": "t1", "line": {"name": "U6"}, "when": None, "direction": "B"}], now=now)
    assert len(vehicles) == 1
    assert vehicles[0].edge == 0
    assert 0.4 < vehicles[0].progress < 0.6
```

Run: `uv run pytest src/tests/test_vehicle_tracker.py -v`
Expected: FAIL

- [ ] **Step 2: Implement vehicle tracker**

```python
# src/transit_display_backend/vehicle_tracker.py
from datetime import datetime, timezone
from transit_display_backend.models import MapData, MapVehicle

def _parse_when(value: str | None) -> datetime | None:
    if not value:
        return None
    return datetime.fromisoformat(value)

def track_vehicles(
    map_data: MapData,
    trip_data: dict[str, dict],
    departures: list[dict],
    *,
    now: datetime | None = None,
) -> list[MapVehicle]:
    now = now or datetime.now(timezone.utc)
    node_index = {n.id: i for i, n in enumerate(map_data.nodes)}
    vehicles: list[MapVehicle] = []

    for dep in departures:
        trip_id = dep.get("tripId")
        trip = trip_data.get(trip_id)
        if not trip:
            continue
        line = trip.get("line", {})
        color = line.get("color", {}).get("bg") or {
            "subway": "#0066cc", "suburban": "#008f00", "tram": "#cc0000",
            "bus": "#ff8800", "regional": "#6e6e6e", "ferry": "#00aaff",
        }.get(line.get("product", "unknown"), "#aaaaaa")
        direction = dep.get("direction") or ""

        stopovers = trip.get("stopovers", [])
        for i in range(1, len(stopovers)):
            prev = stopovers[i - 1]
            nxt = stopovers[i]
            prev_id = (prev.get("stop") or {}).get("id")
            next_id = (nxt.get("stop") or {}).get("id")
            if prev_id not in node_index or next_id not in node_index:
                continue
            prev_when = _parse_when(prev.get("arrival") or prev.get("departure"))
            next_when = _parse_when(nxt.get("arrival") or nxt.get("departure"))
            if not prev_when or not next_when:
                continue
            if prev_when <= now <= next_when:
                edge_idx = None
                for e_idx, e in enumerate(map_data.edges):
                    if (e.a == node_index[prev_id] and e.b == node_index[next_id]) or (e.a == node_index[next_id] and e.b == node_index[prev_id]):
                        edge_idx = e_idx
                        break
                if edge_idx is None:
                    continue
                total = (next_when - prev_when).total_seconds()
                elapsed = (now - prev_when).total_seconds()
                progress = max(0.0, min(1.0, elapsed / total if total > 0 else 0.0))
                vehicles.append(MapVehicle(
                    line=line.get("name", "?"),
                    direction=direction,
                    edge=edge_idx,
                    progress=progress,
                    color=color,
                ))
                break
    return vehicles
```

- [ ] **Step 3: Run tests to verify they pass**

Run: `uv run pytest src/tests/test_vehicle_tracker.py -v`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add transit_display/backend/src
git commit -m "feat(backend): vehicle tracker"
```

---

### Task 5: FastAPI endpoint and auth

**Files:**
- Create: `transit_display/backend/src/transit_display_backend/main.py`
- Create: `transit_display/backend/src/tests/test_main.py`
- Modify: `transit_display/backend/src/transit_display_backend/config.py` to expose `get_token`

- [ ] **Step 1: Write the failing test**

```python
# src/tests/test_main.py
from fastapi.testclient import TestClient
from transit_display_backend import main

def test_transit_without_auth():
    client = TestClient(main.app)
    response = client.get("/api/transit?profile=home")
    assert response.status_code == 401
```

Run: `uv run pytest src/tests/test_main.py -v`
Expected: FAIL

- [ ] **Step 2: Implement FastAPI app**

```python
# src/transit_display_backend/main.py
from fastapi import FastAPI, Depends, HTTPException, Header
from transit_display_backend.config import load_profiles, get_token, ProfileConfig
from transit_display_backend.vbb_client import VbbClient
from transit_display_backend.map_builder import build_map
from transit_display_backend.vehicle_tracker import track_vehicles

app = FastAPI(title="Transit Display Backend")
client = VbbClient()

def verify_token(authorization: str | None = Header(None)):
    if authorization is None:
        raise HTTPException(status_code=401, detail="Missing token")
    scheme, _, token = authorization.partition(" ")
    if scheme.lower() != "bearer" or token != get_token():
        raise HTTPException(status_code=403, detail="Invalid token")

@app.get("/api/transit")
def get_transit(profile: str, _auth: None = Depends(verify_token)):
    profiles = load_profiles()
    if profile not in profiles:
        raise HTTPException(status_code=400, detail="Invalid profile")
    profile_cfg = profiles[profile]

    stop_meta: dict[str, dict] = {}
    departures: list[dict] = []
    for stop in profile_cfg.stops:
        try:
            meta = client.fetch_stop_sync(stop.id)
        except Exception as exc:
            raise HTTPException(status_code=502, detail=f"VBB stop error: {exc}")
        stop_meta[stop.id] = meta
        try:
            deps = client.fetch_departures_sync(stop.id)
        except Exception as exc:
            raise HTTPException(status_code=502, detail=f"VBB departures error: {exc}")
        departures.extend(deps)

    trip_ids = {d.get("tripId") for d in departures if d.get("tripId")}
    trip_data: dict[str, dict] = {}
    for tid in trip_ids:
        try:
            trip_data[tid] = client.fetch_trip_sync(tid)
        except Exception:
            continue

    response = build_map(profile_cfg, stop_meta, trip_data, departures, profile_name=profile)
    response.map.vehicles = track_vehicles(response.map, trip_data, departures)
    return response
```

- [ ] **Step 3: Run tests to verify they pass**

Run: `uv run pytest src/tests/test_main.py -v`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add transit_display/backend/src
git commit -m "feat(backend): FastAPI endpoint with auth"
```

---

### Task 6: Docker packaging and run verification

**Files:**
- Create: `transit_display/backend/Dockerfile`
- Create: `transit_display/backend/compose.yml`

- [ ] **Step 1: Add Dockerfile**

```dockerfile
FROM ghcr.io/astral-sh/uv:python3.12-bookworm-slim AS builder
WORKDIR /app
COPY pyproject.toml .python-version src ./
RUN uv sync --no-dev

FROM python:3.12-slim-bookworm
WORKDIR /app
COPY --from=builder /app/.venv /app/.venv
COPY --from=builder /app/src /app/src
ENV PATH="/app/.venv/bin:$PATH" PYTHONPATH="/app/src"
ENV BRIDGE_TOKEN=change-me
EXPOSE 8000
CMD ["uvicorn", "transit_display_backend.main:app", "--host", "0.0.0.0", "--port", "8000"]
```

- [ ] **Step 2: Add compose.yml**

```yaml
services:
  transit-backend:
    build: .
    ports:
      - "8000:8000"
    environment:
      BRIDGE_TOKEN: ${BRIDGE_TOKEN:-change-me}
    volumes:
      - ./config:/app/config:ro
```

- [ ] **Step 3: Verify build**

Run:
```bash
cd transit_display/backend
uv sync
uv run pytest src/tests -v
```
Expected: all tests pass.

Run:
```bash
cd transit_display/backend
docker compose build
```
Expected: image builds successfully.

- [ ] **Step 4: Commit**

```bash
git add transit_display/backend
git commit -m "feat(backend): Docker packaging"
```

---

## Self-review checklist

- [x] Spec coverage: config loading, VBB client, map builder, vehicle tracker, endpoint, auth, Docker all have tasks.
- [x] Placeholder scan: no TBD/TODO/fill-in details.
- [x] Type consistency: `MapData`, `MapNode`, `MapEdge`, `MapVehicle`, `TransitResponse` used consistently across tasks.

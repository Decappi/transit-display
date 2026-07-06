# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

import os
from datetime import datetime, timezone

from fastapi import Depends, FastAPI, Header, HTTPException

from transit_display_backend.config import StopConfig, get_token, load_profiles
from transit_display_backend.map_builder import build_map
from transit_display_backend.models import MapData, MapEdge, MapNode, MapVehicle, TransitResponse
from transit_display_backend.vbb_client import VbbClient
from transit_display_backend.vehicle_tracker import track_vehicles

app = FastAPI(title="Transit Display Backend")
client = VbbClient()


def verify_token(authorization: str | None = Header(None)) -> None:
    if authorization is None:
        raise HTTPException(status_code=401, detail="Missing token")
    scheme, _, token = authorization.partition(" ")
    if scheme.lower() != "bearer" or token != get_token():
        raise HTTPException(status_code=403, detail="Invalid token")


def demo_response(profile: str) -> TransitResponse:
    now = datetime.now(timezone.utc).timestamp()
    # All stops unique; Gesundbrunnen and Alexanderplatz shared by S1/U8
    nodes = [
        MapNode(id="s1bn", name="S Bornholmer Str. (off)", x=70, y=-30),
        MapNode(id="ge",   name="S+U Gesundbrunnen", x=70, y=45),
        MapNode(id="s1os", name="S Oranienburger Str.", x=70, y=95),
        MapNode(id="s1fr", name="S Friedrichstr.", x=70, y=145),
        MapNode(id="s1ah", name="S Anhalter Bhf", x=70, y=195),
        MapNode(id="s1ss", name="S Suedkreuz (off)", x=70, y=270),

        MapNode(id="u2vn", name="S+U Pankow (off)", x=140, y=-30),
        MapNode(id="u2sh", name="S+U Schoenhauser Allee", x=140, y=45),
        MapNode(id="u2rl", name="S+U Rosa-Luxemburg-Pl.", x=140, y=95),
        MapNode(id="al",   name="S+U Alexanderplatz", x=140, y=145),
        MapNode(id="u2ja", name="S+U Jannowitzbruecke", x=175, y=165),
        MapNode(id="u2os", name="S Ostkreuz (off)", x=300, y=240),

        MapNode(id="u8wm", name="U Weinmeisterstr.", x=110, y=100),
        MapNode(id="u8ha", name="U Hackescher Markt", x=130, y=140),
        MapNode(id="u8he", name="S+U Hermannstr. (off)", x=300, y=280),

        MapNode(id="trnw", name="Tram Nordwest (off)", x=-20, y=-20),
        MapNode(id="trsp", name="Tram Seestr.", x=60, y=55),
        MapNode(id="trsh", name="Tram Schonhauser Allee", x=115, y=75),
        MapNode(id="treb", name="Tram Eberswalder Str.", x=175, y=90),
        MapNode(id="trws", name="Tram Warschauer (off)", x=270, y=115),

        MapNode(id="bunw", name="Bus Nordwest (off)", x=-20, y=60),
        MapNode(id="bust", name="Stahlheimer Str.", x=115, y=60),
        MapNode(id="bupf", name="S Prenzlauer Allee", x=175, y=50),
        MapNode(id="busb", name="S Bornholmer Str. (off)", x=270, y=-30),
    ]
    edges = [
        MapEdge(a=0, b=1, color="#008f00"),   # S1 Bornholmer → Gesundbrunnen
        MapEdge(a=1, b=2, color="#008f00"),
        MapEdge(a=2, b=3, color="#008f00"),
        MapEdge(a=3, b=4, color="#008f00"),
        MapEdge(a=4, b=5, color="#008f00"),

        MapEdge(a=6, b=7, color="#0066cc"),   # U2 Pankow → Schoenhauser
        MapEdge(a=7, b=8, color="#0066cc"),
        MapEdge(a=8, b=9, color="#0066cc"),
        MapEdge(a=9, b=10, color="#0066cc"),  # Alexanderplatz → Jannowitzbruecke
        MapEdge(a=10, b=11, color="#0066cc"),

        MapEdge(a=1, b=12, color="#0066cc"),  # U8 Gesundbrunnen → Weinmeisterstr
        MapEdge(a=12, b=13, color="#0066cc"),
        MapEdge(a=13, b=9, color="#0066cc"),  # Hackescher → Alexanderplatz
        MapEdge(a=9, b=14, color="#0066cc"),

        MapEdge(a=15, b=16, color="#cc0000"), # Tram
        MapEdge(a=16, b=17, color="#cc0000"),
        MapEdge(a=17, b=18, color="#cc0000"),
        MapEdge(a=18, b=19, color="#cc0000"),

        MapEdge(a=20, b=21, color="#ff8800"), # Bus
        MapEdge(a=21, b=22, color="#ff8800"),
        MapEdge(a=22, b=23, color="#ff8800"),
    ]

    def prog(speed: float, offset: float) -> float:
        return (now * speed + offset) % 1.0

    vehicles = [
        MapVehicle(line="S1", direction="Oranienburg", product="suburban", edge=0, progress=prog(0.009, 0.0), color="#008f00"),
        MapVehicle(line="S1", direction="Suedkreuz", product="suburban", edge=4, progress=prog(0.009, 0.5), color="#008f00"),
        MapVehicle(line="U2", direction="Pankow", product="subway", edge=5, progress=prog(0.01, 0.1), color="#0066cc"),
        MapVehicle(line="U2", direction="Ruhleben", product="subway", edge=9, progress=prog(0.01, 0.6), color="#0066cc"),
        MapVehicle(line="U8", direction="Wittenau", product="subway", edge=10, progress=prog(0.01, 0.2), color="#0066cc"),
        MapVehicle(line="U8", direction="Hermannstr.", product="subway", edge=13, progress=prog(0.01, 0.7), color="#0066cc"),
        MapVehicle(line="M10", direction="Warschauer", product="tram", edge=18, progress=prog(0.015, 0.3), color="#cc0000"),
        MapVehicle(line="M10", direction="Seestr.", product="tram", edge=14, progress=prog(0.015, 0.8), color="#cc0000"),
        MapVehicle(line="M27", direction="Westend", product="bus", edge=19, progress=prog(0.02, 0.1), color="#ff8800"),
        MapVehicle(line="M27", direction="Bornholmer", product="bus", edge=21, progress=prog(0.02, 0.5), color="#ff8800"),
    ]
    return TransitResponse(
        updated_at=datetime.now(timezone.utc).isoformat(),
        profile=profile,
        map=MapData(nodes=nodes, edges=edges, vehicles=vehicles),
    )


@app.get("/api/transit")
def get_transit(profile: str, _auth: None = Depends(verify_token)):
    if os.environ.get("DEMO_MODE") == "1":
        return demo_response(profile)

    profiles = load_profiles()
    if profile not in profiles:
        raise HTTPException(status_code=400, detail="Invalid profile")
    profile_cfg = profiles[profile]

    # Resolve stops — hardcoded or proximity
    stops: list[StopConfig]
    allowed_lines: set[str]
    if profile_cfg.stops is not None:
        stops = profile_cfg.stops
        allowed_lines = set()
        for s in stops:
            allowed_lines.update(s.lines)
    else:
        if profile_cfg.user_position is None:
            raise HTTPException(status_code=400, detail="Profile needs user_position for proximity mode")
        pos = profile_cfg.user_position
        raw = client.fetch_stops_near(pos.lat, pos.lon, max_dist=profile_cfg.proximity_radius)
        stops = [StopConfig(id=s["id"], name=s["name"], lines=[]) for s in raw]
        allowed_lines = set(profile_cfg.lines)

    stop_meta: dict[str, dict] = {}
    raw_deps: list[dict] = []
    for stop in stops:
        try:
            meta = client.fetch_stop_sync(stop.id)
        except Exception as exc:
            raise HTTPException(status_code=502, detail=f"VBB stop error: {exc}")
        stop_meta[stop.id] = meta
        try:
            deps = client.fetch_departures_sync(stop.id)
        except Exception as exc:
            raise HTTPException(status_code=502, detail=f"VBB departures error: {exc}")
        raw_deps.extend(deps)

    departures = [
        d for d in raw_deps
        if d.get("line", {}).get("name", "").strip() in allowed_lines
    ]

    trip_ids = {d.get("tripId") for d in departures if d.get("tripId")}
    trip_data: dict[str, dict] = {}
    for d in departures:
        tid = d.get("tripId")
        if not tid or tid in trip_data:
            continue
        when = d.get("when", "")
        date = when.split("T")[0] if "T" in when else ""
        try:
            trip_data[tid] = client.fetch_trip_sync(tid, date)
        except Exception:
            continue

    # Deduplicate departures by tripId — same train appears at multiple profile stops
    seen_trips: set[str] = set()
    unique_departures: list[dict] = []
    for d in departures:
        tid = d.get("tripId")
        if tid and tid not in seen_trips:
            seen_trips.add(tid)
            unique_departures.append(d)

    response = build_map(profile_cfg, stop_meta, trip_data, unique_departures, profile_name=profile, stops=stops)
    response.map.vehicles = track_vehicles(response.map, trip_data, unique_departures)
    return response

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

from datetime import datetime, timezone

from transit_display_backend.config import ProfileConfig, StopConfig
from transit_display_backend.models import MapData, MapEdge, MapNode, TransitResponse

MARGIN = 20
MAX_NODES = 25  # center + up to 24 nearest

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
    stops: list[StopConfig] | None = None,
) -> TransitResponse:
    stops = stops or profile.stops or []
    coords: dict[str, tuple[float, float]] = {}
    stop_names: dict[str, str] = {}
    profile_ids: set[str] = set()
    for stop in stops:
        profile_ids.add(stop.id)
        meta = stop_meta.get(stop.id)
        if meta and meta.get("location", {}).get("lat"):
            coords[stop.id] = (meta["location"]["lon"], meta["location"]["lat"])
            name = meta.get("name", "")
            if name and name != stop.id:
                stop_names[stop.id] = name

    if profile.user_position:
        c_lon = profile.user_position.lon
        c_lat = profile.user_position.lat
    else:
        c_lon, c_lat = 13.4133, 52.5219
    stop_dists: dict[str, float] = {}

    for trip in trip_data.values():
        for stopover in trip.get("stopovers", []):
            stop = stopover.get("stop") or {}
            sid = stop.get("id")
            loc = stop.get("location")
            name = stop.get("name")
            if not sid or not loc:
                continue
            if sid not in stop_names and name:
                stop_names[sid] = name
            if sid in coords:
                continue
            coords[sid] = (loc["lon"], loc["lat"])
            stop_dists[sid] = (
                (coords[sid][0] - c_lon) ** 2 + (coords[sid][1] - c_lat) ** 2
            ) ** 0.5

    user_pos = profile.user_position
    if user_pos:
        coords["_user"] = (user_pos.lon, user_pos.lat)

    # Always keep all profile stops with valid coords; fill remaining with nearest trip stops
    selected_ids: set[str] = profile_ids & coords.keys()
    if user_pos:
        selected_ids.add("_user")
    sorted_stops = sorted(stop_dists.items(), key=lambda kv: kv[1])
    for stop_id, _ in sorted_stops:
        if len(selected_ids) >= MAX_NODES:
            break
        selected_ids.add(stop_id)

    projected = _project_nodes({sid: coords[sid] for sid in selected_ids})
    nodes = [
        MapNode(
            id=sid,
            name="" if sid == "_user" else (stop_meta.get(sid, {}).get("name") or stop_names.get(sid, sid)),
            x=p[0], y=p[1],
        )
        for sid, p in projected.items()
    ]
    node_index = {n.id: i for i, n in enumerate(nodes)}

    edges: list[MapEdge] = []
    seen_edges: set[tuple[int, int, str]] = set()
    for trip in trip_data.values():
        line = trip.get("line", {})
        color = _line_color(line)
        stopovers = trip.get("stopovers", [])
        prev_idx = None
        for stopover in stopovers:
            sid = (stopover.get("stop") or {}).get("id")
            if sid in node_index:
                if prev_idx is not None and prev_idx != node_index[sid]:
                    key = (min(prev_idx, node_index[sid]), max(prev_idx, node_index[sid]), color)
                    if key not in seen_edges:
                        seen_edges.add(key)
                        edges.append(MapEdge(a=prev_idx, b=node_index[sid], color=color))
                prev_idx = node_index[sid]

    updated_at = datetime.now(timezone.utc).isoformat()
    return TransitResponse(
        updated_at=updated_at,
        profile=profile_name,
        map=MapData(nodes=nodes, edges=edges, vehicles=[]),
    )

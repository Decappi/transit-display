# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

from datetime import datetime, timezone
from zoneinfo import ZoneInfo

from transit_display_backend.models import MapData, MapVehicle

COLOR_FALLBACK = {
    "subway": "#0066cc",
    "suburban": "#008f00",
    "tram": "#cc0000",
    "bus": "#ff8800",
    "regional": "#6e6e6e",
    "ferry": "#00aaff",
    "unknown": "#aaaaaa",
}


def _parse_when(value: str | None) -> datetime | None:
    if not value or not isinstance(value, str):
        return None
    if value.count("T") != 1 or value.endswith("T"):
        return None
    try:
        dt = datetime.fromisoformat(value)
    except (ValueError, TypeError):
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=ZoneInfo("Europe/Berlin"))
    return dt


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
        color = line.get("color", {}).get("bg") or COLOR_FALLBACK.get(
            line.get("product", "unknown"), "#aaaaaa"
        )
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
                    if (e.a == node_index[prev_id] and e.b == node_index[next_id]) or (
                        e.a == node_index[next_id] and e.b == node_index[prev_id]
                    ):
                        edge_idx = e_idx
                        break
                if edge_idx is None:
                    continue
                total = (next_when - prev_when).total_seconds()
                elapsed = (now - prev_when).total_seconds()
                progress = max(0.0, min(1.0, elapsed / total if total > 0 else 0.0))
                vehicles.append(
                    MapVehicle(
                        line=line.get("name", "?"),
                        direction=direction,
                        product=line.get("product", "unknown"),
                        edge=edge_idx,
                        progress=progress,
                        color=color,
                    )
                )
                break
    return vehicles

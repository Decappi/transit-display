# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

from datetime import datetime, timezone

from transit_display_backend.models import MapData, MapEdge, MapNode
from transit_display_backend.vehicle_tracker import track_vehicles


def test_track_vehicles_between_stops() -> None:
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
    vehicles = track_vehicles(
        map_data,
        {"t1": trip},
        [{"tripId": "t1", "line": {"name": "U6"}, "when": None, "direction": "B"}],
        now=now,
    )
    assert len(vehicles) == 1
    assert vehicles[0].edge == 0
    assert 0.4 < vehicles[0].progress < 0.6

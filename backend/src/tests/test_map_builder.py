# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

from transit_display_backend.config import ProfileConfig, StopConfig
from transit_display_backend.map_builder import build_map


def test_build_map_basic() -> None:
    profile = ProfileConfig(
        title="home",
        stops=[StopConfig(id="a", name="A", lines=["U6"])],
    )
    stop_meta = {
        "a": {
            "id": "a",
            "name": "A",
            "location": {"lat": 52.5, "lon": 13.4},
        },
        "b": {
            "id": "b",
            "name": "B",
            "location": {"lat": 52.51, "lon": 13.41},
        },
    }
    trip_data = {
        "t1": {
            "line": {
                "name": "U6",
                "product": "subway",
                "color": {"fg": "#fff", "bg": "#0066cc"},
            },
            "polyline": {
                "features": [{"geometry": {"coordinates": [[13.4, 52.5], [13.41, 52.51]]}}]
            },
            "stopovers": [
                {
                    "stop": {"id": "a", "name": "A"},
                    "arrival": "2026-07-03T10:00:00+02:00",
                },
                {
                    "stop": {
                        "id": "b",
                        "name": "B",
                        "location": {"lat": 52.51, "lon": 13.41},
                    },
                    "arrival": "2026-07-03T10:05:00+02:00",
                },
            ],
        }
    }
    departures = [
        {
            "tripId": "t1",
            "line": {"name": "U6"},
            "when": "2026-07-03T10:02:00+02:00",
        }
    ]
    result = build_map(profile, stop_meta, trip_data, departures)
    assert result.profile == "home"
    assert len(result.map.nodes) >= 2
    assert len(result.map.edges) == 1

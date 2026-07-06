# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

from transit_display_backend.vbb_client import HafasClient


def test_fetch_stop(monkeypatch) -> None:
    client = HafasClient()
    monkeypatch.setattr(client, "_request", lambda method, req: {
        "locL": [{
            "lid": "L=900017101",
            "name": "U Mehringdamm",
            "extId": "900017101",
            "crd": {"x": 13388138, "y": 52493570},
        }],
    })
    stop = client.fetch_stop_sync("900017101")
    assert stop["name"] == "U Mehringdamm"
    assert stop["location"]["lat"] == 52.49357
    assert stop["location"]["lon"] == 13.388138

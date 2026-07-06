# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

from fastapi.testclient import TestClient

from transit_display_backend import main
from transit_display_backend.config import ProfileConfig, StopConfig


def test_transit_without_auth() -> None:
    client = TestClient(main.app)
    response = client.get("/api/transit?profile=home")
    assert response.status_code == 401


def test_transit_with_auth(monkeypatch) -> None:
    monkeypatch.setenv("BRIDGE_TOKEN", "secret")
    monkeypatch.setattr(
        main,
        "load_profiles",
        lambda: {
            "home": ProfileConfig(
                title="home",
                stops=[StopConfig(id="a", name="A", lines=["U6"])],
            )
        },
    )
    monkeypatch.setattr(main.client, "fetch_stop_sync", lambda _sid: {
        "id": "a",
        "name": "A",
        "location": {"lat": 52.5, "lon": 13.4},
    })
    monkeypatch.setattr(main.client, "fetch_departures_sync", lambda _sid: [])

    client = TestClient(main.app)
    response = client.get("/api/transit?profile=home", headers={"Authorization": "Bearer secret"})
    assert response.status_code == 200
    assert response.json()["profile"] == "home"
    assert "map" in response.json()

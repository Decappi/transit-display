# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

from pathlib import Path

from transit_display_backend.config import get_token, load_profiles


def test_load_profiles_reads_file(tmp_path: Path) -> None:
    config_path = tmp_path / "profiles.json"
    config_path.write_text(
        '{"profiles":{"home":{"title":"Home","stops":[{"id":"x","name":"X","lines":[]}]}}}'
    )
    profiles = load_profiles(config_path)
    assert "home" in profiles
    assert profiles["home"].stops[0].id == "x"


def test_get_token_from_env(monkeypatch) -> None:
    monkeypatch.setenv("BRIDGE_TOKEN", "secret")
    assert get_token() == "secret"

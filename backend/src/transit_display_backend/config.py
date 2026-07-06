# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

import os
from pathlib import Path

from pydantic import BaseModel

DEV_PROFILES_PATH = Path(__file__).resolve().parents[2] / "config" / "profiles.json"


class StopConfig(BaseModel):
    id: str
    name: str
    lines: list[str]


class UserPosition(BaseModel):
    lat: float
    lon: float


class ProfileConfig(BaseModel):
    title: str
    user_position: UserPosition | None = None
    stops: list[StopConfig] | None = None
    lines: list[str] = []
    proximity_radius: int = 1500


class ProfilesFile(BaseModel):
    profiles: dict[str, ProfileConfig]


DEFAULT_PROFILES_PATH = Path(os.environ.get("PROFILES_PATH", DEV_PROFILES_PATH))


def load_profiles(path: Path | None = None) -> dict[str, ProfileConfig]:
    target = path or DEFAULT_PROFILES_PATH
    data = ProfilesFile.model_validate_json(target.read_text())
    return data.profiles


def get_token() -> str:
    return os.environ["BRIDGE_TOKEN"]

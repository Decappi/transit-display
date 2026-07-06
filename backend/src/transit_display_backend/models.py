# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

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
    product: str
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

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

"""Direct BVG HAFAS client — no transport.rest intermediary.

Uses the same endpoint as the BVG mobile app.
"""
import os
import time
from datetime import datetime, timezone
from functools import wraps
from zoneinfo import ZoneInfo
import httpx

HAFAS_URL = "https://bvg.hafas.cloud/apps/gate"
HAFAS_AID = os.environ.get("BRIDGE_HAFAS_AID", "dVg4TZbW8anjx9ztPwe2uk4LVRi9wO")
HAFAS_AUTH = {"type": "AID", "aid": HAFAS_AID}
HAFAS_CLIENT = {
    "type": "WEB",
    "id": "VBB",
    "v": 10002,
    "name": "webapp",
    "l": "vs_webapp",
}
HAFAS_VER = "1.72"
HAFAS_EXT = "BVG.1"

# HAFAS product class bitmask → product string
CLS_TO_PRODUCT = {
    1: "suburban",
    2: "subway",
    4: "tram",
    8: "bus",
    16: "ferry",
    32: "express",
    64: "regional",
}

PRODUCT_FALLBACK_COLOR = {
    "subway": "#0066cc",
    "suburban": "#008f00",
    "tram": "#cc0000",
    "bus": "#ff8800",
    "regional": "#6e6e6e",
    "ferry": "#00aaff",
    "express": "#444444",
    "unknown": "#aaaaaa",
}


def _cls_to_product(cls: int) -> str:
    for bit, name in CLS_TO_PRODUCT.items():
        if cls & bit:
            return name
    return "unknown"


def _now_berlin() -> tuple[str, str]:
    now = datetime.now(ZoneInfo("Europe/Berlin"))
    return now.strftime("%Y%m%d"), now.strftime("%H%M%S")


def _cached(ttl: int):
    store: dict = {}

    def decorator(fn):
        @wraps(fn)
        def wrapper(*args, **kwargs):
            key = (fn.__name__, args, tuple(sorted(kwargs.items())))
            now = time.monotonic()
            if key in store and now - store[key][0] < ttl:
                return store[key][1]
            result = fn(*args, **kwargs)
            store[key] = (now, result)
            return result
        return wrapper
    return decorator


class HafasClient:
    def __init__(self):
        self.http = httpx.Client(timeout=10.0)

    def _request(self, method: str, req: dict) -> dict:
        payload = {
            "auth": HAFAS_AUTH,
            "ver": HAFAS_VER,
            "ext": HAFAS_EXT,
            "client": HAFAS_CLIENT,
            "lang": "deu",
            "svcReqL": [{"meth": method, "req": req}],
        }
        resp = self.http.post(HAFAS_URL, json=payload)
        resp.raise_for_status()
        data = resp.json()
        svc = data.get("svcResL", [])
        if not svc:
            raise RuntimeError(f"HAFAS empty svcResL: {data.get('err', '?')}")
        return svc[0].get("res", {})

    @_cached(300)
    def fetch_stops_near(self, lat: float, lon: float, max_count: int = 20, max_dist: int = 2000) -> list[dict]:
        res = self._request("LocGeoPos", {
            "ring": {
                "cCrd": {"x": int(lon * 1e6), "y": int(lat * 1e6)},
                "maxDist": max_dist,
            },
            "maxLoc": max_count,
            "getStops": True,
            "getPOIs": False,
        })
        locL = res.get("locL", [])
        stops = []
        for loc in locL:
            lid = loc.get("lid", "")
            stop_id = loc.get("extId", "")
            if not stop_id:
                stop_id = lid[2:] if lid.startswith("L=") else lid
            crd = loc.get("crd", {})
            stops.append({
                "id": stop_id,
                "name": loc.get("name", stop_id),
                "location": {"lat": crd.get("y", 0) / 1e6, "lon": crd.get("x", 0) / 1e6},
            })
        return stops

    @_cached(300)
    def fetch_stop_sync(self, stop_id: str) -> dict:
        res = self._request("LocDetails", {"locL": [{"lid": f"L={stop_id}"}]})
        common = res.get("common", {})
        locL = res.get("locL", []) or common.get("locL", [])
        loc = locL[0] if locL else {}
        crd = loc.get("crd", {})
        return {
            "name": loc.get("name", stop_id),
            "location": {"lat": crd.get("y", 0) / 1e6, "lon": crd.get("x", 0) / 1e6},
        }

    @_cached(30)
    def fetch_departures_sync(self, stop_id: str, results: int = 10, duration: int = 30) -> list[dict]:
        date, time_str = _now_berlin()
        res = self._request("StationBoard", {
            "type": "DEP",
            "date": date,
            "time": time_str,
            "stbLoc": {"type": "S", "lid": f"L={stop_id}"},
            "maxJny": results,
            "dur": duration,
        })
        common = res.get("common", {})
        prodL = common.get("prodL", [])
        jnyL = res.get("jnyL", [])
        departures = []
        for j in jnyL:
            px = j.get("prodX", 0)
            prod = prodL[px] if px < len(prodL) else {}
            cls = prod.get("cls", 0)
            product = _cls_to_product(cls)
            departures.append({
                "tripId": j.get("jid", ""),
                "line": {
                    "name": prod.get("name", "?"),
                    "product": product,
                    "color": {"bg": PRODUCT_FALLBACK_COLOR.get(product, "#aaaaaa")},
                },
                "direction": j.get("dirTxt", "?"),
                "when": f"{j.get('date', '')}T{j.get('time', '')}",
                "plannedWhen": f"{j.get('date', '')}T{j.get('dTimeS', j.get('time', ''))}",
                "delay": j.get("dlyIn", 0),
            })
        return departures

    @_cached(30)
    def fetch_trip_sync(self, trip_id: str, date: str = "") -> dict:
        if not date:
            date, _ = _now_berlin()
        res = self._request("JourneyDetails", {"jid": trip_id, "date": date})
        common = res.get("common", {})
        locL = common.get("locL", [])
        journey = res.get("journey", {})
        prodL = common.get("prodL", [])
        stopL = journey.get("stopL", [])
        stopovers = []
        for s in stopL:
            lx = s.get("locX", 0)
            loc = locL[lx] if lx < len(locL) else {}
            crd = loc.get("crd", {})
            a_time = s.get("aTimeS", "")
            d_time = s.get("dTimeS", "")
            stopovers.append({
                "stop": {
                    "id": str(loc.get("extId", "")),
                    "name": loc.get("name", "?"),
                    "location": {
                        "lon": crd.get("x", 0) / 1e6,
                        "lat": crd.get("y", 0) / 1e6,
                    },
                },
                "arrival": f"{s.get('aDate', date)}T{a_time}" if a_time else None,
                "departure": f"{s.get('dDate', date)}T{d_time}" if d_time else None,
            })
        line_info = {}
        if prodL:
            cls = prodL[0].get("cls", 0)
            product = _cls_to_product(cls)
            line_info = {
                "name": prodL[0].get("name", "?"),
                "product": product,
                "color": {"bg": PRODUCT_FALLBACK_COLOR.get(product, "#aaaaaa")},
            }
        return {"stopovers": stopovers, "line": line_info}


# Backward-compatible alias
VbbClient = HafasClient

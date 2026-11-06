"""Runtime configuration, read from environment variables."""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _default_graph() -> Path:
    processed = ROOT / "data" / "processed"
    for name in ("jaipur-v1.graph", "demo-v1.graph"):
        if (processed / name).exists():
            return processed / name
    return processed / "jaipur-v1.graph"


@dataclass(frozen=True)
class Settings:
    graph_path: Path
    engine_path: Path
    max_tour_stops: int
    max_snap_distance_m: float
    snap_to_main_component: bool
    engine_timeout_s: float
    max_trace_events: int
    cors_origins: list[str]
    frontend_dist: Path
    data_dir: Path
    osm_cache_dir: Path
    max_area_radius_m: int
    fleet_timeout_s: float
    max_fleet_requests: int
    max_fleet_vehicles: int

    @classmethod
    def from_env(cls) -> "Settings":
        return cls(
            graph_path=Path(os.environ.get("PATHOS_GRAPH", _default_graph())),
            engine_path=Path(os.environ.get("PATHOS_ENGINE", ROOT / "engine" / "build" / "pathos_engine")),
            max_tour_stops=int(os.environ.get("PATHOS_MAX_TOUR_STOPS", 25)),
            max_snap_distance_m=float(os.environ.get("PATHOS_MAX_SNAP_M", 750)),
            snap_to_main_component=os.environ.get("PATHOS_SNAP_MAIN_COMPONENT", "1") not in ("0", "false", "no"),
            engine_timeout_s=float(os.environ.get("PATHOS_ENGINE_TIMEOUT_S", 30)),
            max_trace_events=int(os.environ.get("PATHOS_MAX_TRACE_EVENTS", 150_000)),
            cors_origins=os.environ.get("PATHOS_CORS_ORIGINS", "http://localhost:5173,http://127.0.0.1:5173").split(","),
            frontend_dist=Path(os.environ.get("PATHOS_FRONTEND_DIST", ROOT / "frontend" / "dist")),
            data_dir=Path(os.environ.get("PATHOS_DATA_DIR", ROOT / "data" / "processed")),
            osm_cache_dir=Path(os.environ.get("PATHOS_OSM_CACHE", ROOT / "data" / "raw" / "osmnx-cache")),
            max_area_radius_m=int(os.environ.get("PATHOS_MAX_AREA_RADIUS_M", 8000)),
            fleet_timeout_s=float(os.environ.get("PATHOS_FLEET_TIMEOUT_S", 180)),
            max_fleet_requests=int(os.environ.get("PATHOS_MAX_FLEET_REQUESTS", 120)),
            max_fleet_vehicles=int(os.environ.get("PATHOS_MAX_FLEET_VEHICLES", 20)),
        )

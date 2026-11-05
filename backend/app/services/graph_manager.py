"""Owns the active road graph and lets the API switch or download areas at runtime.

A graph switch builds the new GraphStore and starts a new engine process
first, then swaps them in under a lock. Requests already running keep the old
pair until they finish; the old engine is stopped after a grace period.
"""

from __future__ import annotations

import json
import logging
import re
import sys
import threading
from dataclasses import dataclass
from pathlib import Path

from app.config import ROOT, Settings
from app.services.engine import EngineClient
from app.services.graph_store import GraphStore

sys.path.insert(0, str(ROOT / "scripts"))
import prepare_graph  # noqa: E402  (scripts/prepare_graph.py)

log = logging.getLogger("pathos.graphs")

NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,80}$")


class GraphNotFound(LookupError):
    pass


class BuildInProgress(RuntimeError):
    pass


@dataclass(frozen=True)
class ActiveGraph:
    path: Path
    store: GraphStore
    engine: EngineClient


class GraphManager:
    def __init__(self, settings: Settings):
        self.settings = settings
        self._active: ActiveGraph | None = None
        self._swap_lock = threading.Lock()
        self._build_lock = threading.Lock()

    # ------------------------------------------------------------ access
    @property
    def active(self) -> ActiveGraph:
        with self._swap_lock:
            if self._active is None:
                raise RuntimeError("no graph loaded")
            return self._active

    @property
    def building(self) -> bool:
        return self._build_lock.locked()

    # ----------------------------------------------------------- loading
    def load(self, path: Path) -> ActiveGraph:
        store = GraphStore.load(path, self.settings.snap_to_main_component)
        engine = EngineClient(self.settings.engine_path, path, self.settings.engine_timeout_s)
        engine.start()
        with self._swap_lock:
            old, self._active = self._active, ActiveGraph(path, store, engine)
        if old is not None and old.engine is not engine:
            # Let requests that already hold the old engine finish first.
            timer = threading.Timer(30.0, old.engine.stop)
            timer.daemon = True
            timer.start()
        log.info("active graph: %s (%d nodes)", store.version, len(store.lats))
        return self._active  # type: ignore[return-value]

    def select(self, name: str) -> ActiveGraph:
        return self.load(self.path_for(name, must_exist=True))

    def shutdown(self) -> None:
        with self._swap_lock:
            active, self._active = self._active, None
        if active:
            active.engine.stop()

    def path_for(self, name: str, must_exist: bool = False) -> Path:
        if not NAME_RE.match(name):
            raise ValueError("graph names may only contain letters, digits, '.', '_' and '-'")
        path = self.settings.data_dir / f"{name}.graph"
        if must_exist and not path.exists():
            raise GraphNotFound(f"no saved map area called '{name}'")
        return path

    # ---------------------------------------------------------- catalogue
    def available(self) -> list[dict]:
        out = []
        active_path = self._active.path.resolve() if self._active else None
        for path in sorted(self.settings.data_dir.glob("*.graph")):
            meta_path = path.with_suffix(".meta.json")
            try:
                meta = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {}
            except json.JSONDecodeError:
                meta = {}
            out.append({
                "name": path.stem,
                "nodes": meta.get("nodes"),
                "edges": meta.get("edges"),
                "center": meta.get("center"),
                "place": meta.get("place"),
                "radius_m": (meta.get("query") or {}).get("dist_m") if isinstance(meta.get("query"), dict) else None,
                "source": meta.get("source"),
                "active": path.resolve() == active_path,
            })
        return out

    # ------------------------------------------------------------ download
    def build_area(self, lat: float, lon: float, radius_m: int, place: str | None = None) -> ActiveGraph:
        """Download (or reuse a cached) OSM area around (lat, lon) and make it active."""
        if not self._build_lock.acquire(blocking=False):
            raise BuildInProgress("another map area is already being downloaded; try again shortly")
        try:
            if place:
                name = f"{prepare_graph.slugify(place)}-{radius_m}m"
            else:
                name = f"area_{lat:.4f}_{lon:.4f}_{radius_m}m"
            path = self.path_for(name)
            if not path.exists():
                prepare_graph.build_graph(
                    point=(lat, lon),
                    dist=radius_m,
                    name=name,
                    out_dir=self.settings.data_dir,
                    cache_dir=self.settings.osm_cache_dir,
                    extra_meta={"place": place} if place else None,
                    log=log.info,
                )
            else:
                log.info("reusing saved area %s", name)
            return self.load(path)
        finally:
            self._build_lock.release()

"""Node coordinates, road geometry and coordinate snapping for the API layer.

The C++ engine owns the routing graph; this module only reads what the API
needs to talk in latitude/longitude: snapping clicked points to road nodes
and turning node-ID paths back into drawable geometry.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path

EARTH_RADIUS_M = 6371008.8


def haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    r = math.pi / 180
    a = (
        math.sin((lat2 - lat1) * r / 2) ** 2
        + math.cos(lat1 * r) * math.cos(lat2 * r) * math.sin((lon2 - lon1) * r / 2) ** 2
    )
    return 2 * EARTH_RADIUS_M * math.asin(min(1.0, math.sqrt(a)))


@dataclass(frozen=True)
class Snap:
    node_id: int
    lat: float
    lon: float
    distance_m: float


class GraphStore:
    """Coordinates + edge shapes + a uniform-grid spatial index for nearest-node queries."""

    CELL_DEG = 0.0025  # ~250 m cells

    def __init__(self, lats: list[float], lons: list[float], shapes: dict[tuple[int, int], list[tuple[float, float]]],
                 snappable: list[bool], version: str, metadata: dict, main_component_size: int = 0):
        self.lats = lats
        self.lons = lons
        self.shapes = shapes
        self.version = version
        self.metadata = metadata
        self._grid: dict[tuple[int, int], list[int]] = {}
        for i, ok in enumerate(snappable):
            if ok:
                self._grid.setdefault(self._cell(lats[i], lons[i]), []).append(i)
        self.snappable_count = sum(snappable)
        self.main_component_size = main_component_size

    # ---------------------------------------------------------------- loading
    @classmethod
    def load(cls, path: Path, snap_to_main_component: bool = True) -> "GraphStore":
        """Read a graph file.

        With `snap_to_main_component`, clicks are only snapped to nodes in the
        largest strongly connected component, so every pair of snapped points
        is mutually reachable despite one-way streets. Otherwise any node with
        both incoming and outgoing edges is used and unreachable pairs are
        reported by the engine.
        """
        with path.open(encoding="utf-8") as f:
            tokens = _tokens(f)
            if next(tokens) != "pathos-graph" or next(tokens) != "1":
                raise ValueError(f"{path}: not a pathos-graph v1 file")
            _expect(tokens, "version")
            version = next(tokens)
            _expect(tokens, "nodes")
            n = int(next(tokens))
            lats, lons = [0.0] * n, [0.0] * n
            for i in range(n):
                lats[i] = float(next(tokens))
                lons[i] = float(next(tokens))
                next(tokens)  # osm id
            _expect(tokens, "edges")
            m = int(next(tokens))
            adj: list[list[int]] = [[] for _ in range(n)]
            in_deg = [0] * n
            shapes: dict[tuple[int, int], list[tuple[float, float]]] = {}
            for _ in range(m):
                u, v = int(next(tokens)), int(next(tokens))
                next(tokens), next(tokens)  # length, time
                k = int(next(tokens))
                pts = [(float(next(tokens)), float(next(tokens))) for _ in range(k)]
                if pts:
                    shapes[(u, v)] = pts
                adj[u].append(v)
                in_deg[v] += 1
        comp = largest_scc(adj)
        if snap_to_main_component:
            snappable = comp
        else:
            snappable = [bool(adj[i]) and in_deg[i] > 0 for i in range(n)]
        meta_path = path.with_suffix(".meta.json")
        metadata = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {}
        return cls(lats, lons, shapes, snappable, version, metadata, sum(comp))

    # -------------------------------------------------------------- snapping
    def _cell(self, lat: float, lon: float) -> tuple[int, int]:
        return (math.floor(lat / self.CELL_DEG), math.floor(lon / self.CELL_DEG))

    def nearest(self, lat: float, lon: float, max_distance_m: float) -> Snap | None:
        """Nearest snappable node within max_distance_m, searching grid rings outward."""
        if not self._grid:
            return None
        ci, cj = self._cell(lat, lon)
        # One cell spans at least this many metres in either direction.
        cell_m = self.CELL_DEG * 111_320 * max(0.1, math.cos(math.radians(lat)))
        max_ring = int(max_distance_m / cell_m) + 2
        best: Snap | None = None
        for ring in range(max_ring + 1):
            # Points in ring r are at least (r - 1) cells away.
            if best is not None and (ring - 1) * cell_m > best.distance_m:
                break
            for di in range(-ring, ring + 1):
                for dj in range(-ring, ring + 1):
                    if max(abs(di), abs(dj)) != ring:
                        continue
                    for node in self._grid.get((ci + di, cj + dj), ()):
                        d = haversine_m(lat, lon, self.lats[node], self.lons[node])
                        if best is None or d < best.distance_m:
                            best = Snap(node, self.lats[node], self.lons[node], d)
        if best is None or best.distance_m > max_distance_m:
            return None
        return best

    # -------------------------------------------------------------- geometry
    def path_coordinates(self, path: list[int]) -> list[list[float]]:
        """GeoJSON-ordered [lon, lat] points along a node path, including curved road shapes."""
        if not path:
            return []
        coords = [[self.lons[path[0]], self.lats[path[0]]]]
        for u, v in zip(path, path[1:]):
            for la, lo in self.shapes.get((u, v), ()):
                coords.append([lo, la])
            coords.append([self.lons[v], self.lats[v]])
        return coords

    def edge_coordinates(self, u: int, v: int) -> list[list[float]]:
        """Road shape of one directed edge as [lon, lat] points."""
        return self.path_coordinates([u, v])

    def timed_path(self, nodes: list[int], times: list[float]) -> list[list[float]]:
        """[lon, lat, t] points along a node path with times given at the nodes.

        Intermediate road-shape points get times interpolated by distance, so a
        vehicle drawn from this list follows the curved road at a steady speed
        between two nodes.
        """
        pts: list[list[float]] = []
        for i, node in enumerate(nodes):
            if i == 0:
                pts.append([self.lons[node], self.lats[node], times[0]])
                continue
            u = nodes[i - 1]
            shape = [(lat, lon) for lat, lon in self.shapes.get((u, node), ())]
            shape.append((self.lats[node], self.lons[node]))
            prev = (pts[-1][1], pts[-1][0])
            legs = []
            for lat, lon in shape:
                legs.append(haversine_m(prev[0], prev[1], lat, lon))
                prev = (lat, lon)
            total = sum(legs) or 1.0
            t0, t1 = times[i - 1], times[i]
            acc = 0.0
            for (lat, lon), d in zip(shape, legs):
                acc += d
                pts.append([lon, lat, t0 + (t1 - t0) * acc / total])
        return pts

    def trace_segments(self, flat: list[int], limit: int) -> tuple[list[list[float]], bool]:
        """Engine trace [node, parent, ...] -> [[plat, plon, lat, lon], ...] (parent may equal node for the root)."""
        segs: list[list[float]] = []
        truncated = False
        for i in range(0, len(flat) - 1, 2):
            if len(segs) >= limit:
                truncated = True
                break
            node, parent = flat[i], flat[i + 1]
            p = parent if parent >= 0 else node
            segs.append([
                round(self.lats[p], 6), round(self.lons[p], 6),
                round(self.lats[node], 6), round(self.lons[node], 6),
            ])
        return segs, truncated

    @property
    def bbox(self) -> dict[str, float]:
        return {"south": min(self.lats), "west": min(self.lons), "north": max(self.lats), "east": max(self.lons)}


def largest_scc(adj: list[list[int]]) -> list[bool]:
    """Membership mask of the largest strongly connected component (Kosaraju, iterative).

    Pass 1: DFS on the graph, recording nodes in order of finishing time.
    Pass 2: DFS on the reversed graph in decreasing finishing time; each tree
    found is one strongly connected component.  O(V + E).
    """
    n = len(adj)
    order: list[int] = []
    seen = [False] * n
    for root in range(n):
        if seen[root]:
            continue
        seen[root] = True
        stack = [(root, 0)]
        while stack:
            u, i = stack[-1]
            if i < len(adj[u]):
                stack[-1] = (u, i + 1)
                v = adj[u][i]
                if not seen[v]:
                    seen[v] = True
                    stack.append((v, 0))
            else:
                stack.pop()
                order.append(u)

    radj: list[list[int]] = [[] for _ in range(n)]
    for u, vs in enumerate(adj):
        for v in vs:
            radj[v].append(u)

    comp = [-1] * n
    sizes: list[int] = []
    for root in reversed(order):
        if comp[root] != -1:
            continue
        c = len(sizes)
        comp[root] = c
        stack, size = [root], 0
        while stack:
            u = stack.pop()
            size += 1
            for v in radj[u]:
                if comp[v] == -1:
                    comp[v] = c
                    stack.append(v)
        sizes.append(size)
    if not sizes:
        return []
    best = max(range(len(sizes)), key=sizes.__getitem__)
    return [c == best for c in comp]


def _tokens(f):
    for line in f:
        if line.startswith("#"):
            continue
        yield from line.split()


def _expect(tokens, word: str) -> None:
    got = next(tokens)
    if got != word:
        raise ValueError(f"expected '{word}', got '{got}'")

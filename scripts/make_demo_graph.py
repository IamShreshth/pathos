"""Generate a synthetic, city-like road graph so pathos runs fully offline.

It is an irregular, jittered street grid around Jaipur's coordinates with
one-way streets, missing blocks, diagonal avenues and faster arterial roads.
It is NOT real map data — use scripts/prepare_graph.py for real OSM roads.

    python scripts/make_demo_graph.py            # -> data/processed/demo-v1.graph
"""

from __future__ import annotations

import argparse
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from graph_format import EdgeRecord, write_graph  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    r = math.pi / 180
    a = math.sin((lat2 - lat1) * r / 2) ** 2 + math.cos(lat1 * r) * math.cos(lat2 * r) * math.sin((lon2 - lon1) * r / 2) ** 2
    return 2 * 6371008.8 * math.asin(min(1.0, math.sqrt(a)))


def build(rows: int, cols: int, seed: int, center: tuple[float, float]):
    rng = random.Random(seed)
    spacing_lat = 0.0011  # ~120 m
    spacing_lon = 0.0012
    lat0 = center[0] - rows / 2 * spacing_lat
    lon0 = center[1] - cols / 2 * spacing_lon

    nodes: list[tuple[float, float, int]] = []
    idx: dict[tuple[int, int], int] = {}
    for r in range(rows):
        for c in range(cols):
            if rng.random() < 0.04 and 0 < r < rows - 1 and 0 < c < cols - 1:
                continue  # a missing intersection (park, building, ...)
            lat = lat0 + r * spacing_lat + rng.uniform(-0.3, 0.3) * spacing_lat
            lon = lon0 + c * spacing_lon + rng.uniform(-0.3, 0.3) * spacing_lon
            idx[(r, c)] = len(nodes)
            nodes.append((lat, lon, -1))

    edges: dict[tuple[int, int], EdgeRecord] = {}

    def add(a: int, b: int, kmh: float, oneway: bool) -> None:
        la, lo = nodes[a][:2]
        lb, lob = nodes[b][:2]
        # A gentle bend so the road is a little longer than the straight line.
        bend = rng.uniform(-0.15, 0.15)
        mid = ((la + lb) / 2 + bend * (lob - lo) * 0.3, (lo + lob) / 2 - bend * (lb - la) * 0.3)
        length = haversine_m(la, lo, *mid) + haversine_m(*mid, lb, lob)
        t = length / (kmh / 3.6)
        edges[(a, b)] = EdgeRecord(a, b, length, t, [mid])
        if not oneway:
            edges[(b, a)] = EdgeRecord(b, a, length, t, [mid])

    for (r, c), a in idx.items():
        for dr, dc in ((0, 1), (1, 0)):
            b = idx.get((r + dr, c + dc))
            if b is None or rng.random() < 0.06:
                continue  # missing block
            arterial = (dr == 0 and r % 6 == 0) or (dc == 0 and c % 6 == 0)
            kmh = 50 if arterial else rng.choice((20, 25, 30))
            oneway = not arterial and rng.random() < 0.25
            if oneway and rng.random() < 0.5:
                a, b = b, a
            add(a, b, kmh, oneway)
            a = idx[(r, c)]

    # Two diagonal avenues.
    for start_c, step in ((0, 1), (cols - 1, -1)):
        r, c = 0, start_c
        while 0 <= r + 1 < rows and 0 <= c + step < cols:
            a, b = idx.get((r, c)), idx.get((r + 1, c + step))
            if a is not None and b is not None:
                add(a, b, 40, oneway=False)
            r, c = r + 1, c + step

    return nodes, list(edges.values())


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rows", type=int, default=40)
    p.add_argument("--cols", type=int, default=40)
    p.add_argument("--seed", type=int, default=2120)
    p.add_argument("--name", default="demo-v1")
    p.add_argument("--out-dir", type=Path, default=ROOT / "data" / "processed")
    a = p.parse_args()

    nodes, edges = build(a.rows, a.cols, a.seed, (26.9124, 75.7873))
    meta = write_graph(
        a.out_dir / f"{a.name}.graph",
        a.name,
        nodes,
        edges,
        {"source": "synthetic demo grid (not real roads)", "seed": a.seed},
    )
    print(f"wrote {a.out_dir / (a.name + '.graph')}  ({meta['nodes']} nodes, {meta['edges']} edges)")


if __name__ == "__main__":
    main()

"""Writer for the pathos graph file format shared by the C++ engine and the API.

Format (text, whitespace separated):

    # comment lines are allowed at the top
    pathos-graph 1
    version <name-without-spaces>
    nodes <N>
    <lat> <lon> <osm_id>                              N lines; line i is node i
    edges <M>
    <u> <v> <length_m> <time_s> <k> [<lat> <lon>]*k   M lines; directed u -> v

`k` intermediate shape points describe a curved road for drawing only; the
engine ignores them. One-way roads appear in one direction only.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


@dataclass
class EdgeRecord:
    u: int
    v: int
    length_m: float
    time_s: float
    shape: list[tuple[float, float]] = field(default_factory=list)  # (lat, lon), endpoints excluded


def write_graph(
    path: Path,
    version: str,
    nodes: Iterable[tuple[float, float, int]],
    edges: Iterable[EdgeRecord],
    metadata: dict,
) -> dict:
    """Write the graph file plus a `<name>.meta.json` sidecar; return the metadata."""
    if any(c.isspace() for c in version) or not version:
        raise ValueError("version must be a non-empty string without spaces")
    nodes = list(nodes)
    edges = list(edges)
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", encoding="utf-8") as f:
        f.write("# pathos road graph — generated, do not edit by hand\n")
        f.write(f"pathos-graph 1\nversion {version}\n")
        f.write(f"nodes {len(nodes)}\n")
        for lat, lon, osm_id in nodes:
            f.write(f"{lat:.7f} {lon:.7f} {osm_id}\n")
        f.write(f"edges {len(edges)}\n")
        for e in edges:
            pts = " ".join(f"{la:.7f} {lo:.7f}" for la, lo in e.shape)
            f.write(f"{e.u} {e.v} {e.length_m:.3f} {e.time_s:.3f} {len(e.shape)}")
            f.write(f" {pts}\n" if pts else "\n")

    lats = [n[0] for n in nodes]
    lons = [n[1] for n in nodes]
    meta = {
        "graph_version": version,
        "nodes": len(nodes),
        "edges": len(edges),
        "bbox": {"south": min(lats), "west": min(lons), "north": max(lats), "east": max(lons)},
        "center": {"lat": (min(lats) + max(lats)) / 2, "lon": (min(lons) + max(lons)) / 2},
        **metadata,
    }
    path.with_suffix(".meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    return meta

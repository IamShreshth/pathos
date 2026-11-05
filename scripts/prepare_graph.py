"""Download an OpenStreetMap road network with OSMnx and export it for pathos.

Examples
--------
    # 3 km around central Jaipur (default)
    python scripts/prepare_graph.py

    # 3 km around a named place (geocoded with Nominatim)
    python scripts/prepare_graph.py --place "Bharatpur, Rajasthan, India" --name bharatpur-v1

    # 5 km around a coordinate
    python scripts/prepare_graph.py --point 27.2106,77.4978 --dist 5000 --name my-area

The output goes to data/processed/<name>.graph (+ .meta.json). Point the API
at it with PATHOS_GRAPH=data/processed/<name>.graph, or pick it in the web
app's "Map area" panel. The web app can also download new areas by itself;
it calls build_graph() below.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))
from graph_format import EdgeRecord, write_graph  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT_DIR = ROOT / "data" / "processed"
DEFAULT_CACHE_DIR = ROOT / "data" / "raw" / "osmnx-cache"

# Typical speeds (km/h) per OSM road type, used where roads have no maxspeed
# tag. Many towns and rural areas have none at all, and OSMnx then needs these.
DEFAULT_SPEEDS_KMH = {
    "motorway": 100, "motorway_link": 60,
    "trunk": 80, "trunk_link": 50,
    "primary": 60, "primary_link": 40,
    "secondary": 50, "secondary_link": 35,
    "tertiary": 40, "tertiary_link": 30,
    "unclassified": 30, "residential": 25,
    "living_street": 15, "service": 15, "road": 30,
}
FALLBACK_SPEED_KMH = 30


class PlaceNotFound(ValueError):
    """Nominatim could not geocode the place name."""


class NoRoadsFound(ValueError):
    """The requested area contains no drivable roads."""


def slugify(text: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s[:48] or "area"


def geocode(place: str) -> tuple[float, float]:
    import osmnx as ox

    try:
        lat, lon = ox.geocode(place)
    except Exception as exc:  # osmnx raises InsufficientResponseError / ValueError
        raise PlaceNotFound(f"could not find a place called '{place}'") from exc
    return float(lat), float(lon)


def build_graph(
    *,
    point: tuple[float, float],
    dist: int = 3000,
    name: str,
    network: str = "drive",
    out_dir: Path = DEFAULT_OUT_DIR,
    cache_dir: Path = DEFAULT_CACHE_DIR,
    strongly_connected: bool = False,
    extra_meta: dict | None = None,
    log: Callable[[str], None] = print,
) -> dict:
    """Download roads within `dist` metres of `point`, write <name>.graph and return its metadata."""
    import osmnx as ox
    import pandas as pd

    t0 = time.perf_counter()
    ox.settings.use_cache = True
    ox.settings.cache_folder = str(cache_dir)
    ox.settings.log_console = False

    lat, lon = point
    log(f"downloading {dist} m around ({lat:.5f}, {lon:.5f}) [{network}] ...")
    try:
        G = ox.graph_from_point((lat, lon), dist=dist, network_type=network, simplify=True)
    except (ValueError, ox._errors.InsufficientResponseError) as exc:
        raise NoRoadsFound(f"no {network} roads found within {dist} m of ({lat:.5f}, {lon:.5f})") from exc

    if strongly_connected:
        G = ox.truncate.largest_component(G, strongly=True)

    # Speeds (km/h) from OSM maxspeed tags, imputed per highway type, then
    # free-flow travel time per edge. These are static estimates, not traffic.
    G = ox.add_edge_speeds(G, hwy_speeds=DEFAULT_SPEEDS_KMH, fallback=FALLBACK_SPEED_KMH)
    G = ox.add_edge_travel_times(G)

    nodes_gdf, edges_gdf = ox.graph_to_gdfs(G)
    if len(nodes_gdf) < 2 or len(edges_gdf) == 0:
        raise NoRoadsFound(f"no {network} roads found within {dist} m of ({lat:.5f}, {lon:.5f})")
    log(f"raw graph: {len(nodes_gdf)} nodes, {len(edges_gdf)} directed edges")

    # Compact integer IDs 0..N-1 for array-based storage in C++.
    nodes_gdf = nodes_gdf.sort_index()
    compact = pd.Series(range(len(nodes_gdf)), index=nodes_gdf.index)

    edges = edges_gdf.reset_index()[["u", "v", "length", "travel_time", "geometry"]].copy()
    edges = edges[edges["u"] != edges["v"]]  # drop self loops
    edges = edges.dropna(subset=["length"])
    edges["travel_time"] = edges["travel_time"].fillna(edges["length"] / (30 / 3.6))
    # Parallel edges between the same pair: keep the shortest one.
    edges = edges.sort_values("length").drop_duplicates(subset=["u", "v"], keep="first")
    edges = edges.sort_values(["u", "v"])

    records: list[EdgeRecord] = []
    for row in edges.itertuples(index=False):
        shape: list[tuple[float, float]] = []
        geom = row.geometry
        if geom is not None and not (isinstance(geom, float) and pd.isna(geom)):
            coords = list(geom.coords)  # (lon, lat), includes both endpoints
            shape = [(la, lo) for lo, la in coords[1:-1]]
        records.append(
            EdgeRecord(int(compact[row.u]), int(compact[row.v]), float(row.length), float(row.travel_time), shape)
        )

    node_rows = [(float(r.y), float(r.x), int(osm_id)) for osm_id, r in nodes_gdf.iterrows()]
    out = out_dir / f"{name}.graph"
    meta = write_graph(
        out,
        name,
        node_rows,
        records,
        {
            "source": "OpenStreetMap via OSMnx",
            "query": {"point": [lat, lon], "dist_m": dist},
            "network_type": network,
            "osmnx_version": ox.__version__,
            "created_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "attribution": "© OpenStreetMap contributors (ODbL)",
            **(extra_meta or {}),
        },
    )
    pairs = {(r.u, r.v) for r in records}
    oneway = sum(1 for r in records if (r.v, r.u) not in pairs)
    log(
        f"wrote {out} ({meta['nodes']} nodes, {meta['edges']} edges, {oneway} one-way) "
        f"in {time.perf_counter() - t0:.1f}s"
    )
    return meta


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = p.add_mutually_exclusive_group()
    src.add_argument("--place", help="place name, geocoded with Nominatim (e.g. 'Bharatpur, India')")
    src.add_argument("--point", default="26.9124,75.7873", help="lat,lon centre (default: central Jaipur)")
    p.add_argument("--dist", type=int, default=3000, help="radius in metres (default 3000)")
    p.add_argument("--network", default="drive", help="OSMnx network type (default: drive)")
    p.add_argument("--name", help="graph version / output file name (default: derived from place or jaipur-v1)")
    p.add_argument(
        "--strongly-connected",
        action="store_true",
        help="keep only the largest strongly connected component",
    )
    p.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    return p.parse_args()


def main() -> None:
    args = parse_args()
    if args.place:
        point = geocode(args.place)
        name = args.name or f"{slugify(args.place)}-{args.dist // 1000}km"
        extra = {"place": args.place}
    else:
        point = tuple(float(x) for x in args.point.split(","))  # type: ignore[assignment]
        name = args.name or "jaipur-v1"
        extra = {}
    try:
        build_graph(
            point=point,  # type: ignore[arg-type]
            dist=args.dist,
            name=name,
            network=args.network,
            out_dir=args.out_dir,
            strongly_connected=args.strongly_connected,
            extra_meta=extra,
        )
    except (PlaceNotFound, NoRoadsFound) as exc:
        sys.exit(f"error: {exc}")


if __name__ == "__main__":
    main()

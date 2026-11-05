"""REST endpoints: /api/v1/..."""

from __future__ import annotations

import logging
import math

from fastapi import APIRouter, HTTPException, Request

from app.config import Settings
from app.schemas.models import (
    AreaRequest,
    CompareRequest,
    Coordinate,
    GraphMetadata,
    GraphSummary,
    Health,
    LineString,
    RouteRequest,
    RouteResponse,
    RouteResult,
    SelectGraphRequest,
    SnappedPoint,
    TourLeg,
    TourRequest,
    TourResponse,
)
from app.services.engine import EngineClient, EngineError, EngineRequestError
from app.services.graph_manager import BuildInProgress, GraphManager, GraphNotFound, prepare_graph
from app.services.graph_store import GraphStore, haversine_m

router = APIRouter(prefix="/api/v1")
log = logging.getLogger("pathos.api")


# ------------------------------------------------------------------ helpers
def _ctx(request: Request) -> tuple[Settings, GraphManager]:
    return request.app.state.settings, request.app.state.graphs


def _num(x) -> float | None:
    """Engine sends null for infinity; keep finite numbers only."""
    return x if isinstance(x, (int, float)) and math.isfinite(x) else None


def _distance_to_area_m(store: GraphStore, p: Coordinate) -> float:
    """Distance from p to the loaded area's bounding box (0 if inside)."""
    b = store.bbox
    lat = min(max(p.lat, b["south"]), b["north"])
    lon = min(max(p.lon, b["west"]), b["east"])
    return haversine_m(p.lat, p.lon, lat, lon)


def _snap(store: GraphStore, settings: Settings, point: Coordinate, label: str) -> SnappedPoint:
    snap = store.nearest(point.lat, point.lon, settings.max_snap_distance_m)
    if snap is None:
        away = _distance_to_area_m(store, point)
        where = (
            f"about {away / 1000:.1f} km outside the loaded map area"
            if away > 1000
            else f"more than {settings.max_snap_distance_m:.0f} m from any road"
        )
        raise HTTPException(
            status_code=422,
            detail={
                "code": "outside_coverage",
                "message": f"The {label} ({point.lat:.5f}, {point.lon:.5f}) is {where} "
                f"('{store.version}'). Load the roads for this area first.",
                "point": point.model_dump(),
                "label": label,
                "graph_version": store.version,
                "distance_to_area_m": round(away),
            },
        )
    return SnappedPoint(input=point, node_id=snap.node_id, lat=snap.lat, lon=snap.lon,
                        snap_distance_m=round(snap.distance_m, 1))


def _engine_call(fn, *args):
    try:
        return fn(*args)
    except EngineRequestError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    except EngineError as exc:
        raise HTTPException(status_code=503, detail=f"routing engine unavailable: {exc}") from exc


def _metadata(settings: Settings, manager: GraphManager) -> GraphMetadata:
    active = manager.active
    store, engine = active.store, active.engine
    bbox = store.bbox
    meta = store.metadata
    return GraphMetadata(
        graph_version=store.version,
        nodes=len(store.lats),
        edges=engine.info.get("edges", meta.get("edges", 0)),
        snappable_nodes=store.snappable_count,
        main_component_nodes=store.main_component_size,
        bbox=bbox,
        center=Coordinate(lat=(bbox["south"] + bbox["north"]) / 2, lon=(bbox["west"] + bbox["east"]) / 2),
        source=meta.get("source"),
        attribution=meta.get("attribution"),
        max_tour_stops=settings.max_tour_stops,
        max_snap_distance_m=settings.max_snap_distance_m,
        max_area_radius_m=settings.max_area_radius_m,
    )


# ------------------------------------------------------------------- routes
def _run_routes(request: Request, source: Coordinate, destination: Coordinate, algorithms: list[str],
                weight: str, record_trace: bool) -> RouteResponse:
    settings, manager = _ctx(request)
    active = manager.active  # one consistent store/engine pair for the whole request
    store, engine = active.store, active.engine
    src = _snap(store, settings, source, "source")
    dst = _snap(store, settings, destination, "destination")
    results = []
    for algo in algorithms:
        raw = _engine_call(engine.route, algo, weight, src.node_id, dst.node_id, record_trace)
        path = raw["path"]
        trace, truncated = None, False
        if record_trace:
            trace, truncated = store.trace_segments(raw.get("trace", []), settings.max_trace_events)
            truncated = truncated or raw.get("trace_truncated", False)
        results.append(RouteResult(
            algorithm=raw["algorithm"],
            weight=raw["weight"],
            status=raw["status"],
            cost=_num(raw["cost"]),
            distance_m=raw["distance_m"],
            time_s=raw["time_s"],
            segments=raw["segments"],
            expanded_nodes=raw["expanded_nodes"],
            duration_ms=raw["duration_ms"],
            route_node_ids=path,
            geometry=LineString(coordinates=store.path_coordinates(path)) if path else None,
            trace=trace,
            trace_truncated=truncated,
        ))
    return RouteResponse(graph_version=store.version, source=src, destination=dst, results=results)


@router.post("/routes", response_model=RouteResponse)
def find_route(body: RouteRequest, request: Request) -> RouteResponse:
    """Find one route with the chosen algorithm."""
    return _run_routes(request, body.source, body.destination, [body.algorithm], body.weight, body.record_trace)


@router.post("/routes/compare", response_model=RouteResponse)
def compare_routes(body: CompareRequest, request: Request) -> RouteResponse:
    """Run several algorithms on the same source/destination for side-by-side comparison."""
    return _run_routes(request, body.source, body.destination, body.algorithms, body.weight, body.record_trace)


# -------------------------------------------------------------------- tours
@router.post("/tours", response_model=TourResponse)
def plan_tour(body: TourRequest, request: Request) -> TourResponse:
    """Visit every stop and return to the first: nearest-neighbour + 2-opt on road distances."""
    settings, manager = _ctx(request)
    active = manager.active
    store, engine = active.store, active.engine
    if len(body.stops) > settings.max_tour_stops:
        raise HTTPException(status_code=422, detail=f"at most {settings.max_tour_stops} stops are allowed")
    stops = [_snap(store, settings, p, f"stop {i + 1}") for i, p in enumerate(body.stops)]
    raw = _engine_call(engine.tour, body.weight, [s.node_id for s in stops])

    legs = [
        TourLeg(
            from_stop=l["from"],
            to_stop=l["to"],
            reachable=l["reachable"],
            cost=_num(l["cost"]),
            distance_m=l["distance_m"],
            time_s=l["time_s"],
            geometry=LineString(coordinates=store.path_coordinates(l["path"])) if l["path"] else None,
        )
        for l in raw["legs"]
    ]
    return TourResponse(
        graph_version=store.version,
        weight=raw["weight"],
        status=raw["status"],
        stops=stops,
        order=raw["order"],
        nn_order=raw["nn_order"],
        cost=_num(raw["cost"]),
        nn_cost=_num(raw["nn_cost"]),
        optimal_cost=_num(raw["optimal_cost"]),
        two_opt_moves=raw["two_opt_moves"],
        distance_m=raw["distance_m"],
        time_s=raw["time_s"],
        matrix_ms=raw["matrix_ms"],
        heuristic_ms=raw["heuristic_ms"],
        matrix=[[_num(x) for x in row] for row in raw["matrix"]],
        legs=legs,
        unreachable_legs=[(l.from_stop, l.to_stop) for l in legs if not l.reachable],
    )


# --------------------------------------------------------------- map areas
@router.get("/graph/metadata", response_model=GraphMetadata)
def graph_metadata(request: Request) -> GraphMetadata:
    """The currently loaded road graph."""
    return _metadata(*_ctx(request))


@router.get("/graphs", response_model=list[GraphSummary])
def list_graphs(request: Request) -> list[dict]:
    """Map areas saved in data/processed that can be switched to instantly."""
    _, manager = _ctx(request)
    return manager.available()


@router.post("/graphs/select", response_model=GraphMetadata)
def select_graph(body: SelectGraphRequest, request: Request) -> GraphMetadata:
    """Switch to a saved map area."""
    settings, manager = _ctx(request)
    try:
        manager.select(body.name)
    except ValueError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    except GraphNotFound as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except EngineError as exc:
        raise HTTPException(status_code=503, detail=f"could not start the engine: {exc}") from exc
    return _metadata(settings, manager)


@router.post("/graphs/area", response_model=GraphMetadata)
def load_area(body: AreaRequest, request: Request) -> GraphMetadata:
    """Download the roads around a point or place from OpenStreetMap and make them active.

    Areas already downloaded are reused from data/processed. A new 3 km area
    usually takes 10-60 seconds, depending on the OpenStreetMap servers.
    """
    settings, manager = _ctx(request)
    if body.radius_m > settings.max_area_radius_m:
        raise HTTPException(status_code=422, detail=f"radius_m must be at most {settings.max_area_radius_m} m")
    try:
        if body.place:
            lat, lon = prepare_graph.geocode(body.place)
        else:
            assert body.center is not None
            lat, lon = body.center.lat, body.center.lon
        manager.build_area(lat, lon, body.radius_m, place=body.place)
    except prepare_graph.PlaceNotFound as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except prepare_graph.NoRoadsFound as exc:
        raise HTTPException(status_code=422, detail=f"{exc}. Try a larger radius or another location.") from exc
    except BuildInProgress as exc:
        raise HTTPException(status_code=409, detail=str(exc)) from exc
    except EngineError as exc:
        raise HTTPException(status_code=503, detail=f"could not start the engine: {exc}") from exc
    except (ConnectionError, OSError, TimeoutError) as exc:
        log.warning("OpenStreetMap download failed: %s", exc)
        raise HTTPException(
            status_code=502, detail=f"could not download map data from OpenStreetMap: {exc}"
        ) from exc
    except Exception as exc:  # keep the current map usable and tell the user why
        log.exception("loading map area failed")
        raise HTTPException(status_code=500, detail=f"loading this map area failed: {exc}") from exc
    return _metadata(settings, manager)


@router.get("/health", response_model=Health)
def health(request: Request) -> Health:
    _, manager = _ctx(request)
    try:
        active = manager.active
    except RuntimeError:
        return Health(status="degraded", engine=False, graph_version=None)
    alive = active.engine.alive
    return Health(status="ok" if alive else "degraded", engine=alive, graph_version=active.store.version)

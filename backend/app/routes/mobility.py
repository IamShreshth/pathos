"""Pathos 2.0 endpoints: time-dependent traffic, reliability-aware routing, disruption replanning, fleets."""

from __future__ import annotations

import math
import time

from fastapi import APIRouter, HTTPException, Query, Request

from app.routes.api import _ctx, _engine_call, _num, _snap
from app.schemas.mobility import (
    FleetEventOut,
    FleetRequest,
    FleetRequestOut,
    FleetResponse,
    FleetResultOut,
    FleetVehicleOut,
    ReplanRequest,
    ReplanResponse,
    ReplanStrategyOut,
    TdCandidate,
    TdRouteRequest,
    TdRouteResponse,
    TrafficLevel,
    TrafficResponse,
    TrajectoryOut,
)
from app.schemas.models import Coordinate, LineString, SnappedPoint

router = APIRouter(prefix="/api/v1")

# Congestion buckets drawn on the map: (label, min multiplier). 99 is how the engine marks a road closed right now.
_LEVELS = [("slow", 1.3), ("heavy", 1.7), ("severe", 2.1), ("closed", 50.0)]


def _line(store, nodes: list[int]) -> LineString:
    return LineString(coordinates=store.path_coordinates(nodes))


def _road_lines(store, roads: list[list[int]]) -> list[list[list[float]]]:
    return [store.edge_coordinates(u, v) for u, v in roads]


def _snapped(store, settings, p: Coordinate, label: str) -> SnappedPoint:
    return _snap(store, settings, p, label)


# ------------------------------------------------------------------ traffic
@router.get("/traffic", response_model=TrafficResponse)
def traffic(
    request: Request,
    hour: float = Query(default=8.0, ge=0, lt=24, description="Hour of day (0-24)"),
    min_multiplier: float = Query(default=1.3, ge=1.0, le=5.0),
) -> TrafficResponse:
    """Daily congestion curve and the roads that are clearly slower than free flow at `hour`."""
    _, manager = _ctx(request)
    active = manager.active
    store, engine = active.store, active.engine
    raw = _engine_call(engine.command, {"cmd": "traffic", "t_s": hour * 3600, "min_multiplier": min_multiplier})
    flat = raw.get("edges", [])
    buckets: list[list[list[list[float]]]] = [[] for _ in _LEVELS]
    for i in range(0, len(flat) - 2, 3):
        u, v, m100 = flat[i], flat[i + 1], flat[i + 2]
        m = m100 / 100.0
        k = max(j for j, (_, lo) in enumerate(_LEVELS) if m >= lo)
        buckets[k].append(store.edge_coordinates(u, v))
    levels = [
        TrafficLevel(label=label, min_multiplier=lo, coordinates=lines)
        for (label, lo), lines in zip(_LEVELS, buckets)
    ]
    return TrafficResponse(graph_version=store.version, hour=hour, profile=raw["profile"], levels=levels)


# ------------------------------------------------------------------- routes
@router.post("/mobility/routes", response_model=TdRouteResponse)
def mobility_routes(body: TdRouteRequest, request: Request) -> TdRouteResponse:
    """Compare static, time-dependent, robust and reliability-constrained routes for one trip."""
    settings, manager = _ctx(request)
    active = manager.active
    store, engine = active.store, active.engine
    src = _snapped(store, settings, body.source, "source")
    dst = _snapped(store, settings, body.destination, "destination")
    raw = _engine_call(
        engine.command,
        {
            "cmd": "td_route",
            "source": src.node_id,
            "target": dst.node_id,
            "depart_s": body.depart_hour * 3600,
            "slack": body.slack,
            "alpha": body.alpha,
            "scenarios": body.scenarios,
        },
    )
    if raw.get("status") != "success":
        raise HTTPException(status_code=422, detail="The destination cannot be reached from the source.")
    cands = [
        TdCandidate(
            label=c["label"], kappa=c["kappa"], distance_m=c["distance_m"], free_s=c["free_s"],
            expected_s=c["expected_s"], mc_mean_s=c["mc_mean_s"], mc_std_s=c["mc_std_s"], mc_p50_s=c["mc_p50_s"],
            mc_p95_s=c["mc_p95_s"], mc_worst_s=c["mc_worst_s"], p_on_time=c["p_on_time"],
            geometry=_line(store, c["path"]),
        )
        for c in raw["candidates"]
    ]
    return TdRouteResponse(
        graph_version=store.version, source=src, destination=dst, depart_s=raw["depart_s"],
        deadline_s=raw["deadline_s"], alpha=raw["alpha"], reliable_feasible=raw["reliable_feasible"],
        freeflow=raw["freeflow"], expected=raw["expected"], robust=raw["robust"], reliable=raw["reliable"],
        candidates=cands, duration_ms=raw["duration_ms"],
    )


# ----------------------------------------------------------------- replanning
@router.post("/mobility/replan", response_model=ReplanResponse)
def mobility_replan(body: ReplanRequest, request: Request) -> ReplanResponse:
    """One vehicle meets a closure or accident ahead: keep the plan, recompute everything, or repair locally."""
    settings, manager = _ctx(request)
    active = manager.active
    store, engine = active.store, active.engine
    src = _snapped(store, settings, body.source, "source")
    dst = _snapped(store, settings, body.destination, "destination")
    raw = _engine_call(
        engine.command,
        {
            "cmd": "replan_demo",
            "source": src.node_id,
            "target": dst.node_id,
            "depart_s": body.depart_hour * 3600,
            "type": body.type,
            "duration_s": body.duration_min * 60,
            "event_after_s": body.event_after_s,
            "edges_ahead": body.edges_ahead,
            "repeat": 5,
        },
    )
    if raw.get("status") != "success":
        raise HTTPException(status_code=422, detail="The destination cannot be reached from the source.")

    def strat(d: dict) -> ReplanStrategyOut:
        return ReplanStrategyOut(
            arrival_s=_num(d.get("arrival_s")), travel_s=_num(d.get("travel_s")),
            distance_m=d["distance_m"], expanded_nodes=d["expanded_nodes"], duration_ms=d["duration_ms"],
            changed=d["changed"], affected=d.get("affected"), escalated=d.get("escalated"),
            geometry=_line(store, d["path"]),
        )

    orig = raw["original"]
    original = ReplanStrategyOut(
        arrival_s=orig["arrival_s"], travel_s=orig["travel_s"], distance_m=orig["distance_m"],
        expanded_nodes=0, duration_ms=0.0, changed=False, geometry=_line(store, orig["path"]),
    )
    ev = raw["event_node"]
    return ReplanResponse(
        graph_version=store.version, source=src, destination=dst, type=raw["type"], depart_s=raw["depart_s"],
        event_s=raw["event_s"], duration_s=raw["duration_s"],
        event_point=Coordinate(lat=store.lats[ev], lon=store.lons[ev]),
        disrupted=[LineString(coordinates=c) for c in _road_lines(store, raw["roads"])],
        original=original, static=strat(raw["static"]), full=strat(raw["full"]), selective=strat(raw["selective"]),
    )


# ---------------------------------------------------------------------- fleet
@router.post("/fleet/simulate", response_model=FleetResponse)
def fleet_simulate(body: FleetRequest, request: Request) -> FleetResponse:
    """Run a multi-vehicle day with late requests and disruptions under several replanning strategies."""
    settings, manager = _ctx(request)
    active = manager.active
    store, engine = active.store, active.engine
    if body.generate:
        g = body.generate
        if g.vehicles > settings.max_fleet_vehicles or g.requests > settings.max_fleet_requests:
            raise HTTPException(status_code=422, detail="scenario is larger than this server allows")
        world: dict = {"generate": g.model_dump()}
    else:
        c = body.custom
        assert c is not None
        if len(c.stops) > settings.max_fleet_requests or c.vehicles > settings.max_fleet_vehicles:
            raise HTTPException(status_code=422, detail="scenario is larger than this server allows")
        depot = _snapped(store, settings, c.depot, "depot")
        stops = [_snapped(store, settings, p, f"stop {i + 1}") for i, p in enumerate(c.stops)]
        capacity = c.capacity or max(3.0, math.ceil(len(stops) / c.vehicles * 1.6))
        world = {
            "t_start": 7.5 * 3600,
            "vehicles": [{"depot": depot.node_id, "capacity": capacity} for _ in range(c.vehicles)],
            "requests": [{"node": s.node_id, "demand": 1} for s in stops],
            "auto": {
                "seed": c.seed, "window_min": c.window_min, "dynamic_fraction": c.dynamic_fraction,
                "closures": c.closures, "incidents": c.incidents,
            },
        }
    payload = {
        "cmd": "fleet",
        **world,
        "strategies": body.strategies,
        "params": {
            "kappa": body.kappa, "beta": body.beta, "iters_per_request": body.iters_per_request,
            "scenario": body.scenario,
        },
    }
    if body.mc_scenarios:
        payload["mc"] = {"scenarios": body.mc_scenarios, "disruptions": False}
    t0 = time.perf_counter()
    raw = _engine_call(lambda p: engine.command(p, timeout=settings.fleet_timeout_s), payload)
    wall = (time.perf_counter() - t0) * 1000

    def node_xy(n: int) -> tuple[float, float]:
        return store.lats[n], store.lons[n]

    vehicles = [
        FleetVehicleOut(id=v["id"], lat=node_xy(v["depot"])[0], lon=node_xy(v["depot"])[1],
                        capacity=v["capacity"], shift_end_s=v["shift_end"])
        for v in raw["vehicles"]
    ]
    requests = [
        FleetRequestOut(id=r["id"], lat=node_xy(r["node"])[0], lon=node_xy(r["node"])[1], demand=r["demand"],
                        tw_start_s=r["tw_start"], tw_end_s=r["tw_end"], service_s=r["service_s"],
                        release_s=r["release_s"])
        for r in raw["requests"]
    ]
    events = [
        FleetEventOut(time_s=e["time"], until_s=e["until"], type=e["type"], label=e["label"], factor=e["factor"],
                      lines=_road_lines(store, e["roads"]))
        for e in raw["events"]
    ]
    results = []
    for r in raw["results"]:
        trajs = [
            TrajectoryOut(vehicle=t["vehicle"], points=store.timed_path(t["nodes"], t["times"]))
            for t in r["trajectories"]
        ]
        metrics = {k: float(v) for k, v in r["metrics"].items() if isinstance(v, (int, float))}
        results.append(FleetResultOut(
            strategy=r["strategy"], metrics=metrics, outcomes=r["outcomes"], log=r["log"],
            initial_routes=r["initial_routes"], trajectories=trajs,
        ))
    return FleetResponse(
        graph_version=store.version, t_start_s=raw["t_start"], vehicles=vehicles, requests=requests,
        events=events, results=results, mc=raw.get("mc"), wall_ms=round(wall, 1),
    )

"""Request and response models for the Pathos 2.0 endpoints (traffic, reliability, replanning, fleet)."""

from __future__ import annotations

from typing import Any, Literal

from pydantic import BaseModel, Field, model_validator

from app.schemas.models import Coordinate, LineString, SnappedPoint

StrategyName = Literal["static", "greedy", "full", "selective"]
ALL_STRATEGIES: list[StrategyName] = ["static", "greedy", "full", "selective"]


# ----------------------------------------------------------------- traffic
class TrafficLevel(BaseModel):
    label: str
    min_multiplier: float
    coordinates: list[list[list[float]]] = Field(description="Lines as [lon, lat] points")


class TrafficResponse(BaseModel):
    graph_version: str
    hour: float
    profile: list[float] = Field(description="Congestion multiplier of an average road every 30 minutes (48 values)")
    levels: list[TrafficLevel]
    synthetic: bool = True
    note: str = "Synthetic traffic profile (not measured data)."


# ------------------------------------------------------------ routing
class TdRouteRequest(BaseModel):
    source: Coordinate
    destination: Coordinate
    depart_hour: float = Field(default=8.0, ge=0, lt=24, description="Departure, hours since midnight")
    slack: float = Field(default=1.15, ge=1.0, le=3.0, description="Deadline = slack x expected travel time")
    alpha: float = Field(default=0.1, gt=0, le=0.5, description="Tolerated probability of missing the deadline")
    scenarios: int = Field(default=300, ge=20, le=2000)


class TdCandidate(BaseModel):
    label: str
    kappa: float
    distance_m: float
    free_s: float
    expected_s: float
    mc_mean_s: float
    mc_std_s: float
    mc_p50_s: float
    mc_p95_s: float
    mc_worst_s: float
    p_on_time: float
    geometry: LineString


class TdRouteResponse(BaseModel):
    graph_version: str
    source: SnappedPoint
    destination: SnappedPoint
    depart_s: float
    deadline_s: float
    alpha: float
    reliable_feasible: bool
    freeflow: int = Field(description="Index into candidates: route chosen on the empty network")
    expected: int = Field(description="Fastest in expectation with time-dependent traffic")
    robust: int = Field(description="Minimises mean + z(1-alpha) x std per edge (worst-case flavoured)")
    reliable: int = Field(description="Fastest candidate with P(T <= deadline) >= 1 - alpha")
    candidates: list[TdCandidate]
    duration_ms: float


# ----------------------------------------------------------- replanning
class ReplanRequest(BaseModel):
    source: Coordinate
    destination: Coordinate
    depart_hour: float = Field(default=8.0, ge=0, lt=24)
    type: Literal["closure", "incident"] = "closure"
    duration_min: float = Field(default=60, gt=0, le=600)
    event_after_s: float = Field(default=60, ge=0, le=7200)
    edges_ahead: int = Field(default=10, ge=1, le=200)


class ReplanStrategyOut(BaseModel):
    arrival_s: float | None
    travel_s: float | None
    distance_m: float
    expanded_nodes: int
    duration_ms: float
    changed: bool
    affected: bool | None = None
    escalated: bool | None = None
    geometry: LineString


class ReplanResponse(BaseModel):
    graph_version: str
    source: SnappedPoint
    destination: SnappedPoint
    type: str
    depart_s: float
    event_s: float
    duration_s: float
    event_point: Coordinate
    disrupted: list[LineString]
    original: ReplanStrategyOut
    static: ReplanStrategyOut
    full: ReplanStrategyOut
    selective: ReplanStrategyOut


# ------------------------------------------------------------------ fleet
class GenerateSpec(BaseModel):
    """A random reproducible scenario around the middle of the loaded map."""

    seed: int = 1
    vehicles: int = Field(default=4, ge=1, le=20)
    requests: int = Field(default=40, ge=2, le=120)
    dynamic_fraction: float = Field(default=0.4, ge=0, le=1)
    window_min: float = Field(default=30, ge=5, le=240)
    closures: int = Field(default=2, ge=0, le=10)
    incidents: int = Field(default=1, ge=0, le=10)


class CustomSpec(BaseModel):
    """Depot and stops chosen on the map; windows and disruptions are generated."""

    depot: Coordinate
    stops: list[Coordinate] = Field(min_length=2, max_length=120)
    vehicles: int = Field(default=3, ge=1, le=20)
    capacity: float | None = Field(default=None, gt=0)
    seed: int = 1
    dynamic_fraction: float = Field(default=0.3, ge=0, le=1)
    window_min: float = Field(default=30, ge=5, le=240)
    closures: int = Field(default=2, ge=0, le=10)
    incidents: int = Field(default=1, ge=0, le=10)


class FleetRequest(BaseModel):
    generate: GenerateSpec | None = None
    custom: CustomSpec | None = None
    strategies: list[StrategyName] = Field(default_factory=lambda: list(ALL_STRATEGIES), min_length=1)
    kappa: float = Field(default=0.0, ge=0, le=4, description="Reliability buffer: plan with mean x (1 + kappa x cv)")
    beta: float = Field(default=120.0, ge=0, description="Cost of one disturbed stop in selective replanning")
    iters_per_request: int = Field(default=30, ge=5, le=300)
    scenario: int = Field(default=1, ge=-1, description="Realised travel-time scenario; -1 for expected times")
    mc_scenarios: int = Field(default=0, ge=0, le=500, description="Monte Carlo runs of the static plan (0: skip)")

    @model_validator(mode="after")
    def one_world(self) -> "FleetRequest":
        if (self.generate is None) == (self.custom is None):
            raise ValueError("give exactly one of 'generate' or 'custom'")
        return self


class FleetRequestOut(BaseModel):
    id: int
    lat: float
    lon: float
    demand: float
    tw_start_s: float
    tw_end_s: float
    service_s: float
    release_s: float


class FleetVehicleOut(BaseModel):
    id: int
    lat: float
    lon: float
    capacity: float
    shift_end_s: float


class FleetEventOut(BaseModel):
    time_s: float
    until_s: float
    type: str
    label: str
    factor: float
    lines: list[list[list[float]]]


class TrajectoryOut(BaseModel):
    vehicle: int
    points: list[list[float]] = Field(description="[lon, lat, seconds since midnight]")


class FleetResultOut(BaseModel):
    strategy: StrategyName
    metrics: dict[str, float]
    outcomes: list[dict[str, Any]]
    log: list[dict[str, Any]]
    initial_routes: list[list[int]]
    trajectories: list[TrajectoryOut]


class FleetResponse(BaseModel):
    graph_version: str
    t_start_s: float
    vehicles: list[FleetVehicleOut]
    requests: list[FleetRequestOut]
    events: list[FleetEventOut]
    results: list[FleetResultOut]
    mc: dict[str, Any] | None = None
    wall_ms: float

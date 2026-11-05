"""Request and response models for the pathos REST API."""

from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field, field_validator, model_validator

AlgorithmName = Literal["bfs", "dijkstra", "astar"]
WeightName = Literal["distance", "time"]
ALL_ALGORITHMS: list[AlgorithmName] = ["bfs", "dijkstra", "astar"]


class Coordinate(BaseModel):
    lat: float = Field(ge=-90, le=90)
    lon: float = Field(ge=-180, le=180)


class SnappedPoint(BaseModel):
    input: Coordinate
    node_id: int
    lat: float
    lon: float
    snap_distance_m: float


class LineString(BaseModel):
    type: Literal["LineString"] = "LineString"
    coordinates: list[list[float]]  # GeoJSON order: [lon, lat]


# ------------------------------------------------------------------ routes
class RouteRequest(BaseModel):
    source: Coordinate
    destination: Coordinate
    algorithm: AlgorithmName = "astar"
    weight: WeightName = "distance"
    record_trace: bool = True


class CompareRequest(BaseModel):
    source: Coordinate
    destination: Coordinate
    algorithms: list[AlgorithmName] = Field(default_factory=lambda: list(ALL_ALGORITHMS), min_length=1)
    weight: WeightName = "distance"
    record_trace: bool = True

    @field_validator("algorithms")
    @classmethod
    def unique(cls, v: list[str]) -> list[str]:
        return list(dict.fromkeys(v))


class RouteResult(BaseModel):
    algorithm: AlgorithmName
    weight: WeightName
    status: Literal["success", "unreachable"]
    cost: float | None = Field(description="Objective value: segments for BFS, metres or seconds otherwise")
    distance_m: float
    time_s: float
    segments: int
    expanded_nodes: int
    duration_ms: float
    route_node_ids: list[int]
    geometry: LineString | None
    trace: list[list[float]] | None = Field(
        default=None, description="Search events in expansion order: [parent_lat, parent_lon, lat, lon]"
    )
    trace_truncated: bool = False


class RouteResponse(BaseModel):
    graph_version: str
    source: SnappedPoint
    destination: SnappedPoint
    results: list[RouteResult]


# ------------------------------------------------------------------- tours
class TourRequest(BaseModel):
    stops: list[Coordinate] = Field(min_length=2, description="First stop is the start and end of the tour")
    weight: WeightName = "distance"


class TourLeg(BaseModel):
    from_stop: int
    to_stop: int
    reachable: bool
    cost: float | None
    distance_m: float
    time_s: float
    geometry: LineString | None


class TourResponse(BaseModel):
    graph_version: str
    weight: WeightName
    status: Literal["success", "unreachable"]
    stops: list[SnappedPoint]
    order: list[int] = Field(description="Visiting order as indices into `stops`; returns to order[0]")
    nn_order: list[int]
    cost: float | None
    nn_cost: float | None
    optimal_cost: float | None = Field(description="Exact Held-Karp optimum, only for small tours")
    two_opt_moves: int
    distance_m: float
    time_s: float
    matrix_ms: float
    heuristic_ms: float
    matrix: list[list[float | None]]
    legs: list[TourLeg]
    unreachable_legs: list[tuple[int, int]]


# ------------------------------------------------------------------- misc
class GraphMetadata(BaseModel):
    graph_version: str
    nodes: int
    edges: int
    snappable_nodes: int
    main_component_nodes: int = Field(description="Size of the largest strongly connected component")
    bbox: dict[str, float]
    center: Coordinate
    source: str | None = None
    attribution: str | None = None
    max_tour_stops: int
    max_snap_distance_m: float
    max_area_radius_m: int


class Health(BaseModel):
    status: Literal["ok", "degraded"]
    engine: bool
    graph_version: str | None


# ---------------------------------------------------------- map areas
class GraphSummary(BaseModel):
    name: str
    nodes: int | None = None
    edges: int | None = None
    center: Coordinate | None = None
    place: str | None = None
    radius_m: int | None = None
    source: str | None = None
    active: bool


class AreaRequest(BaseModel):
    """Download the roads around a point or a named place and make them the active map."""

    center: Coordinate | None = None
    place: str | None = Field(default=None, min_length=2, max_length=200)
    radius_m: int = Field(default=3000, ge=300, description="Radius around the centre, in metres")

    @model_validator(mode="after")
    def one_target(self) -> "AreaRequest":
        if (self.center is None) == (self.place is None):
            raise ValueError("give exactly one of 'center' or 'place'")
        return self


class SelectGraphRequest(BaseModel):
    name: str = Field(min_length=1, max_length=80)

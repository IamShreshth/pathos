// Mirrors backend/app/schemas/models.py.

export type Algorithm = "bfs" | "dijkstra" | "astar";
export type Weight = "distance" | "time";

export interface Coordinate {
  lat: number;
  lon: number;
}

export interface SnappedPoint {
  input: Coordinate;
  node_id: number;
  lat: number;
  lon: number;
  snap_distance_m: number;
}

export interface LineString {
  type: "LineString";
  coordinates: [number, number][]; // [lon, lat]
}

export interface RouteResult {
  algorithm: Algorithm;
  weight: Weight;
  status: "success" | "unreachable";
  cost: number | null;
  distance_m: number;
  time_s: number;
  segments: number;
  expanded_nodes: number;
  duration_ms: number;
  route_node_ids: number[];
  geometry: LineString | null;
  trace: [number, number, number, number][] | null; // [parentLat, parentLon, lat, lon]
  trace_truncated: boolean;
}

export interface RouteResponse {
  graph_version: string;
  source: SnappedPoint;
  destination: SnappedPoint;
  results: RouteResult[];
}

export interface TourLeg {
  from_stop: number;
  to_stop: number;
  reachable: boolean;
  cost: number | null;
  distance_m: number;
  time_s: number;
  geometry: LineString | null;
}

export interface TourResponse {
  graph_version: string;
  weight: Weight;
  status: "success" | "unreachable";
  stops: SnappedPoint[];
  order: number[];
  nn_order: number[];
  cost: number | null;
  nn_cost: number | null;
  optimal_cost: number | null;
  two_opt_moves: number;
  distance_m: number;
  time_s: number;
  matrix_ms: number;
  heuristic_ms: number;
  matrix: (number | null)[][];
  legs: TourLeg[];
  unreachable_legs: [number, number][];
}

export interface GraphMetadata {
  graph_version: string;
  nodes: number;
  edges: number;
  snappable_nodes: number;
  bbox: { south: number; west: number; north: number; east: number };
  center: Coordinate;
  source: string | null;
  attribution: string | null;
  max_tour_stops: number;
  max_snap_distance_m: number;
  max_area_radius_m: number;
}

export interface GraphSummary {
  name: string;
  nodes: number | null;
  edges: number | null;
  center: Coordinate | null;
  place: string | null;
  radius_m: number | null;
  source: string | null;
  active: boolean;
}

/** `detail` of a 422 response when a point is not on the loaded map. */
export interface OutsideCoverageDetail {
  code: "outside_coverage";
  message: string;
  point: Coordinate;
  label: string;
  graph_version: string;
  distance_to_area_m: number;
}

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

// ------------------------------------------------ Pathos 2.0 (mirrors schemas/mobility.py)
export interface TrafficLevel {
  label: string;
  min_multiplier: number;
  coordinates: [number, number][][];
}

export interface TrafficResponse {
  graph_version: string;
  hour: number;
  profile: number[];
  levels: TrafficLevel[];
  synthetic: boolean;
  note: string;
}

export interface TdCandidate {
  label: string;
  kappa: number;
  distance_m: number;
  free_s: number;
  expected_s: number;
  mc_mean_s: number;
  mc_std_s: number;
  mc_p50_s: number;
  mc_p95_s: number;
  mc_worst_s: number;
  p_on_time: number;
  geometry: LineString;
}

export interface TdRouteResponse {
  graph_version: string;
  source: SnappedPoint;
  destination: SnappedPoint;
  depart_s: number;
  deadline_s: number;
  alpha: number;
  reliable_feasible: boolean;
  freeflow: number;
  expected: number;
  robust: number;
  reliable: number;
  candidates: TdCandidate[];
  duration_ms: number;
}

export interface ReplanStrategyOut {
  arrival_s: number | null;
  travel_s: number | null;
  distance_m: number;
  expanded_nodes: number;
  duration_ms: number;
  changed: boolean;
  affected: boolean | null;
  escalated: boolean | null;
  geometry: LineString;
}

export interface ReplanResponse {
  graph_version: string;
  source: SnappedPoint;
  destination: SnappedPoint;
  type: "closure" | "incident";
  depart_s: number;
  event_s: number;
  duration_s: number;
  event_point: Coordinate;
  disrupted: LineString[];
  original: ReplanStrategyOut;
  static: ReplanStrategyOut;
  full: ReplanStrategyOut;
  selective: ReplanStrategyOut;
}

export type Strategy = "static" | "greedy" | "full" | "selective";

export interface GenerateSpec {
  seed: number;
  vehicles: number;
  requests: number;
  dynamic_fraction: number;
  window_min: number;
  closures: number;
  incidents: number;
}

export interface CustomSpec {
  depot: Coordinate;
  stops: Coordinate[];
  vehicles: number;
  seed: number;
  dynamic_fraction: number;
  window_min: number;
  closures: number;
  incidents: number;
}

export interface FleetRequestBody {
  generate?: GenerateSpec;
  custom?: CustomSpec;
  strategies: Strategy[];
  kappa: number;
  beta: number;
  iters_per_request: number;
  scenario: number;
  mc_scenarios: number;
}

export interface FleetStop {
  id: number;
  lat: number;
  lon: number;
  demand: number;
  tw_start_s: number;
  tw_end_s: number;
  service_s: number;
  release_s: number;
}

export interface FleetVehicle {
  id: number;
  lat: number;
  lon: number;
  capacity: number;
  shift_end_s: number;
}

export interface FleetEvent {
  time_s: number;
  until_s: number;
  type: "closure" | "incident";
  label: string;
  factor: number;
  lines: [number, number][][];
}

export interface FleetOutcome {
  id: number;
  served: boolean;
  on_time: boolean;
  vehicle: number;
  arrival_s: number;
  lateness_s: number;
}

export interface FleetLogEntry {
  time: number;
  kind: "initial" | "request" | "disruption";
  label: string;
  vehicles: number;
  requests: number;
  latency_ms: number;
  matrix_ms: number;
  solve_ms: number;
  legs_ms: number;
  disruption: number;
  escalated: boolean;
  replanned: boolean;
}

export interface FleetMetrics {
  requests: number;
  on_time: number;
  late: number;
  unserved: number;
  success_rate: number;
  avg_response_s: number;
  p95_response_s: number;
  total_distance_m: number;
  driving_s: number;
  energy_kwh: number;
  lateness_s: number;
  vehicles_used: number;
  cost: number;
  replans: number;
  escalations: number;
  total_latency_ms: number;
  mean_latency_ms: number;
  max_latency_ms: number;
  schedule_disruption: number;
  initial_ms: number;
  makespan_s: number;
}

export interface FleetResult {
  strategy: Strategy;
  metrics: FleetMetrics;
  outcomes: FleetOutcome[];
  log: FleetLogEntry[];
  initial_routes: number[][];
  trajectories: { vehicle: number; points: [number, number, number][] }[];
}

export interface FleetResponse {
  graph_version: string;
  t_start_s: number;
  vehicles: FleetVehicle[];
  requests: FleetStop[];
  events: FleetEvent[];
  results: FleetResult[];
  mc: { scenarios: number; on_time_rate: number; on_time_rate_p5: number; mean_makespan_s: number } | null;
  wall_ms: number;
}

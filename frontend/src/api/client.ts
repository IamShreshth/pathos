import type {
  Algorithm,
  Coordinate,
  GraphMetadata,
  GraphSummary,
  OutsideCoverageDetail,
  FleetRequestBody,
  FleetResponse,
  ReplanResponse,
  RouteResponse,
  TdRouteResponse,
  TourResponse,
  TrafficResponse,
  Weight,
} from "./types";

const BASE = (import.meta.env.VITE_API_BASE as string | undefined) ?? "";

export class ApiError extends Error {
  constructor(
    message: string,
    readonly status = 0,
    /** Structured detail when the server sends one (e.g. outside_coverage). */
    readonly detail: unknown = null,
  ) {
    super(message);
  }

  get outsideCoverage(): OutsideCoverageDetail | null {
    const d = this.detail as Partial<OutsideCoverageDetail> | null;
    return d && d.code === "outside_coverage" ? (d as OutsideCoverageDetail) : null;
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  let res: Response;
  try {
    res = await fetch(`${BASE}/api/v1${path}`, {
      headers: { "Content-Type": "application/json" },
      ...init,
    });
  } catch {
    throw new ApiError("Cannot reach the pathos server. Is it running? (see README_1.md)");
  }
  if (!res.ok) {
    let message = `${res.status} ${res.statusText}`;
    let detail: unknown = null;
    try {
      const body = await res.json();
      detail = body.detail;
      if (typeof detail === "string") message = detail;
      else if (Array.isArray(detail)) message = detail.map((d: { msg: string }) => d.msg).join("; ");
      else if (detail && typeof (detail as { message?: unknown }).message === "string")
        message = (detail as { message: string }).message;
    } catch {
      /* keep status text */
    }
    throw new ApiError(message, res.status, detail);
  }
  return res.json() as Promise<T>;
}

const post = (body: unknown): RequestInit => ({ method: "POST", body: JSON.stringify(body) });

export const api = {
  metadata: () => request<GraphMetadata>("/graph/metadata"),

  compare: (source: Coordinate, destination: Coordinate, algorithms: Algorithm[], weight: Weight) =>
    request<RouteResponse>("/routes/compare", post({ source, destination, algorithms, weight, record_trace: true })),

  tour: (stops: Coordinate[], weight: Weight) => request<TourResponse>("/tours", post({ stops, weight })),

  traffic: (hour: number, minMultiplier = 1.3) =>
    request<TrafficResponse>(`/traffic?hour=${hour}&min_multiplier=${minMultiplier}`),

  mobilityRoutes: (source: Coordinate, destination: Coordinate, depart_hour: number, slack: number, alpha: number) =>
    request<TdRouteResponse>("/mobility/routes", post({ source, destination, depart_hour, slack, alpha })),

  replan: (
    source: Coordinate,
    destination: Coordinate,
    o: { depart_hour: number; type: "closure" | "incident"; duration_min: number; edges_ahead: number },
  ) => request<ReplanResponse>("/mobility/replan", post({ source, destination, ...o })),

  fleet: (body: FleetRequestBody) => request<FleetResponse>("/fleet/simulate", post(body)),

  graphs: () => request<GraphSummary[]>("/graphs"),

  selectGraph: (name: string) => request<GraphMetadata>("/graphs/select", post({ name })),

  /** Downloads OSM roads if this area is new; can take up to a minute. */
  loadArea: (target: { center: Coordinate } | { place: string }, radius_m: number) =>
    request<GraphMetadata>("/graphs/area", post({ ...target, radius_m })),
};

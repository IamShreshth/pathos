import type { Coordinate, GraphMetadata } from "./api/types";

export function haversineM(a: Coordinate, b: Coordinate): number {
  const r = Math.PI / 180;
  const h =
    Math.sin(((b.lat - a.lat) * r) / 2) ** 2 +
    Math.cos(a.lat * r) * Math.cos(b.lat * r) * Math.sin(((b.lon - a.lon) * r) / 2) ** 2;
  return 2 * 6371008.8 * Math.asin(Math.min(1, Math.sqrt(h)));
}

/** True when the point is inside the loaded graph's box (with a small margin for snapping). */
export function inCoverage(meta: GraphMetadata, c: Coordinate): boolean {
  const pad = meta.max_snap_distance_m / 111_320;
  const b = meta.bbox;
  return c.lat >= b.south - pad && c.lat <= b.north + pad && c.lon >= b.west - pad && c.lon <= b.east + pad;
}

export interface AreaPlan {
  /** The point that triggered the plan. */
  point: Coordinate;
  /** Points to keep after loading (all of them if they fit in one area). */
  keep: Coordinate[];
  center: Coordinate;
  radius_m: number;
  /** False when the existing points are too far from the new one to share an area. */
  fitsAll: boolean;
  spanKm: number;
}

/**
 * Smallest area (centre + radius) covering `points` (in order, including `point`).
 * If they are too spread out for one area, fall back to an area around `point` alone.
 */
export function planArea(points: Coordinate[], point: Coordinate, minRadius: number, maxRadius: number): AreaPlan {
  const lats = points.map((p) => p.lat);
  const lons = points.map((p) => p.lon);
  const center = {
    lat: (Math.min(...lats) + Math.max(...lats)) / 2,
    lon: (Math.min(...lons) + Math.max(...lons)) / 2,
  };
  const reach = Math.max(...points.map((p) => haversineM(center, p))) + 1000; // 1 km margin
  const radius = Math.max(minRadius, Math.ceil(reach / 500) * 500);
  const spanKm = (2 * (reach - 1000)) / 1000;
  if (radius <= maxRadius) return { point, keep: points, center, radius_m: radius, fitsAll: true, spanKm };
  return { point, keep: [point], center: point, radius_m: minRadius, fitsAll: false, spanKm };
}

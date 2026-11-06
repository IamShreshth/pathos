import type { Algorithm } from "./api/types";

export const ALGO_LABEL: Record<Algorithm, string> = {
  bfs: "BFS",
  dijkstra: "Dijkstra",
  astar: "A*",
};

export const ALGO_COLOR: Record<Algorithm, string> = {
  bfs: "#e8590c",
  dijkstra: "#1c7ed6",
  astar: "#2f9e44",
};

export const ALGO_HINT: Record<Algorithm, string> = {
  bfs: "fewest road segments",
  dijkstra: "shortest by weight",
  astar: "shortest, guided by straight-line distance",
};

export const TOUR_COLOR = "#7048e8";

export function km(m: number | null | undefined): string {
  if (m == null) return "—";
  return m >= 1000 ? `${(m / 1000).toFixed(2)} km` : `${Math.round(m)} m`;
}

export function minutes(s: number | null | undefined): string {
  if (s == null) return "—";
  return s >= 60 ? `${(s / 60).toFixed(1)} min` : `${Math.round(s)} s`;
}

export function ms(x: number): string {
  return x < 1 ? `${(x * 1000).toFixed(0)} µs` : `${x.toFixed(2)} ms`;
}

export function int(n: number): string {
  return n.toLocaleString();
}

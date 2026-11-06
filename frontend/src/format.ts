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

/** Seconds since midnight as HH:MM. */
export function clock(sec: number): string {
  const s = Math.max(0, Math.round(sec / 60) * 60);
  const h = Math.floor(s / 3600) % 24;
  const m = Math.floor((s % 3600) / 60);
  return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}`;
}

export function pct(x: number | null | undefined, digits = 0): string {
  return x == null ? "—" : `${(x * 100).toFixed(digits)}%`;
}

export function mmss(sec: number | null | undefined): string {
  if (sec == null) return "—";
  const s = Math.round(sec);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

export const STRATEGY_LABEL: Record<string, string> = {
  static: "Static",
  greedy: "Greedy repair",
  full: "Full recompute",
  selective: "Selective",
};

export const STRATEGY_COLOR: Record<string, string> = {
  static: "#868e96",
  greedy: "#f08c00",
  full: "#1c7ed6",
  selective: "#2f9e44",
};

export const STRATEGY_HINT: Record<string, string> = {
  static: "initial plan + cheapest insertion; blind to disruptions",
  greedy: "re-routes affected vehicles, keeps their stop order",
  full: "re-solves everything from scratch at every event",
  selective: "re-optimises only the affected vehicles (warm start)",
};

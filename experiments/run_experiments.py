#!/usr/bin/env python3
"""Reproducible experiments E1-E7 (+ optimality gap) for Pathos 2.0.

Runs the C++ engine's `bench` command on one or more road graphs, stores the
raw rows (JSON + CSV) in experiments/results/ and writes the summary tables to
docs/RESULTS.md.

    python3 experiments/run_experiments.py                       # all graphs, 5 seeds
    python3 experiments/run_experiments.py --quick               # a few minutes
    python3 experiments/run_experiments.py --only E4 --seeds 10
    python3 experiments/run_experiments.py --graphs data/processed/jaipur-v1.graph

Everything is seeded: the same command prints the same quality numbers
(runtimes naturally vary with the machine). Traffic is a *synthetic* daily
profile, not measured data.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import platform
import statistics as st
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "engine" / "build" / "pathos_engine"
RESULTS = ROOT / "experiments" / "results"
DEFAULT_GRAPHS = ["data/processed/jaipur-v1.graph", "data/processed/area_27.2106_77.4978_3000m.graph"]

# two-sided 95% Student-t critical values by degrees of freedom
T95 = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26, 10: 2.23,
       12: 2.18, 15: 2.13, 20: 2.09, 30: 2.04}


def tcrit(df: int) -> float:
    for k in sorted(T95):
        if df <= k:
            return T95[k]
    return 1.96


def mean_ci(xs: list[float]) -> tuple[float, float]:
    xs = [x for x in xs if x is not None]
    if not xs:
        return float("nan"), 0.0
    if len(xs) < 2:
        return xs[0], 0.0
    return st.mean(xs), tcrit(len(xs) - 1) * st.stdev(xs) / math.sqrt(len(xs))


def fmt(xs: list[float], digits: int = 1, scale: float = 1.0, ci: bool = True) -> str:
    m, h = mean_ci([x * scale for x in xs])
    if math.isnan(m):
        return "—"
    return f"{m:.{digits}f} ± {h:.{digits}f}" if ci and h > 0 else f"{m:.{digits}f}"


def table(headers: list[str], rows: list[list[str]]) -> str:
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(out) + "\n"


def run_bench(graph: Path, exp: str, **opts) -> dict:
    t0 = time.time()
    req = {"cmd": "bench", "exp": exp, **opts}
    out = subprocess.run([str(ENGINE), str(graph), json.dumps(req)], capture_output=True, text=True)
    try:
        data = json.loads(out.stdout)
    except json.JSONDecodeError:
        sys.exit(f"engine failed for {exp} on {graph}:\n{out.stdout[:400]}\n{out.stderr[:400]}")
    if not data.get("ok"):
        sys.exit(f"{exp}: {data.get('error')}")
    data["wall_s"] = round(time.time() - t0, 1)
    print(f"  {exp:<4} {len(data['rows']):>4} rows  {data['wall_s']:>6.1f} s", flush=True)
    return data


def save(name: str, data: dict) -> None:
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / f"{name}.json").write_text(json.dumps(data, indent=1))
    rows = data["rows"]
    keys = sorted({k for r in rows for k in r})
    with (RESULTS / f"{name}.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader()
        w.writerows(rows)


def col(rows: list[dict], key: str, **where) -> list[float]:
    return [r[key] for r in rows if all(r.get(k) == v for k, v in where.items()) and key in r]


# ----------------------------------------------------------------- summaries
def sum_e1(rows):
    out = []
    for h in sorted({r["depart_h"] for r in rows}):
        sel = [r for r in rows if r["depart_h"] == h]
        reg = [r["regret_pct"] for r in sel]
        out.append([f"{int(h):02d}:00", str(len(sel)), fmt([r["eta_error_pct"] for r in sel], 1),
                    fmt(reg, 2), f"{max(reg):.2f}", f"{100 * sum(1 for r in sel if not r['same_route']) / len(sel):.0f}%"])
    return table(["Departure", "Trips", "Free-flow ETA error % (mean ± CI)", "Regret of the free-flow route % (mean ± CI)",
                  "Worst regret %", "Trips where TD route differs"], out)


def sum_e2(rows):
    out = []
    for s in sorted({r["slack"] for r in rows}):
        sel = [r for r in rows if r["slack"] == s]
        out.append([f"{(s - 1) * 100:.0f}%", fmt([r["p_free"] for r in sel], 3), fmt([r["p_expected"] for r in sel], 3),
                    fmt([r["p_robust"] for r in sel], 3), fmt([r["p_reliable"] for r in sel], 3),
                    f"{100 * sum(r['feasible'] for r in sel) / len(sel):.0f}%",
                    fmt([r["mean_reliable_s"] / r["mean_expected_s"] * 100 - 100 for r in sel], 2)])
    return table(["Deadline slack", "P(on time) free-flow", "expected", "robust", "reliability-constrained (α=0.1)",
                  "Trips where P ≥ 0.9 is attainable", "Extra mean time of reliable vs expected %"], out)


def sum_e3(rows):
    out = []
    for ty in ("closure", "incident"):
        sel = [r for r in rows if r["type"] == ty and r["lookback"] == 5]
        out.append([ty, str(len(sel)), fmt([r["planned_s"] for r in sel], 0),
                    fmt([r["keep_s"] for r in sel], 0), fmt([r["full_s"] for r in sel], 0), fmt([r["selective_s"] for r in sel], 0)])
    return table(["Disruption (1 h, 8 segments ahead)", "Trips", "Planned trip time s", "Keep plan (wait it out) s",
                  "Full recomputation s", "Selective repair s"], out)


def sum_e4_single(rows):
    out = []
    for ty, lb in [(t, l) for t in ("closure", "incident") for l in sorted({r["lookback"] for r in rows})]:
        sel = [r for r in rows if r["type"] == ty and r["lookback"] == lb]
        gap = [(r["selective_s"] - r["full_s"]) / r["full_s"] * 100 for r in sel]
        saved = [(1 - r["selective_expanded"] / r["full_expanded"]) * 100 for r in sel if r["full_expanded"]]
        out.append([f"{ty}, lookback {lb}", fmt([r["full_ms"] for r in sel], 3), fmt([r["selective_ms"] for r in sel], 3),
                    fmt([r["full_expanded"] for r in sel], 0), fmt([r["selective_expanded"] for r in sel], 0),
                    fmt(saved, 1), fmt(gap, 2), f"{100 * sum(r['selective_escalated'] for r in sel) / len(sel):.0f}%"])
    return table(["Disruption, detour may leave the route this many edges early", "Full ms", "Selective ms", "Full nodes", "Selective nodes", "Nodes saved %",
                  "Arrival gap vs full % (selective later)", "Fell back to full"], out)


def sum_e4(rows):
    names = []
    for r in rows:
        if r["variant"] not in names:
            names.append(r["variant"])
    out = []
    for n in names:
        sel = [r for r in rows if r["variant"] == n]
        out.append([n, fmt([r["success_rate"] * 100 for r in sel], 1), fmt([r["unserved"] for r in sel], 1),
                    fmt([r["late"] for r in sel], 1), fmt([r["distance_m"] / 1000 for r in sel], 1),
                    fmt([r["total_latency_ms"] for r in sel], 0), fmt([r["max_latency_ms"] for r in sel], 0),
                    fmt([r["schedule_disruption"] for r in sel], 1), fmt([r["cost"] for r in sel], 0, ci=False)])
    return table(["Strategy", "On-time %", "Unserved", "Late", "Distance km", "Replan time ms (total)", "Max ms",
                  "Schedule disruption", "Cost"], out)


def sum_e5(rows):
    out = []
    for v in sorted({r["fleet"] for r in rows}):
        sel = [r for r in rows if r["fleet"] == v]
        out.append([str(v), fmt([r["success_rate"] * 100 for r in sel], 1), fmt([r["unserved"] for r in sel], 1),
                    fmt([r["vehicles_used"] for r in sel], 1), fmt([r["distance_m"] / 1000 for r in sel], 1),
                    fmt([r["makespan_s"] / 60 for r in sel], 0), fmt([r["cost"] for r in sel], 0, ci=False)])
    return table(["Fleet size", "On-time %", "Unserved", "Vehicles used", "Distance km", "Makespan min", "Cost"], out)


def sum_e6r(rows):
    out = []
    for a in sorted({r["alpha"] for r in rows}, reverse=True):
        sel = [r for r in rows if r["alpha"] == a]
        out.append([f"{a:.2f}", fmt([r["p_expected"] for r in sel], 3), fmt([r["p_reliable"] for r in sel], 3),
                    f"{100 * sum(r['feasible'] for r in sel) / len(sel):.0f}%",
                    fmt([r["mean_reliable_s"] / r["mean_expected_s"] * 100 - 100 for r in sel], 2),
                    fmt([r["p95_reliable_s"] / r["p95_expected_s"] * 100 - 100 for r in sel], 2),
                    f"{100 * sum(not r['same_as_expected'] for r in sel) / len(sel):.0f}%"])
    return table(["α", "P(on time) expected-time route", "reliability-constrained", "P ≥ 1-α attainable",
                  "Mean time change %", "p95 time change %", "Chose a different route"], out)


def sum_e6f(rows):
    out = []
    for k in sorted({r["kappa"] for r in rows}):
        sel = [r for r in rows if r["kappa"] == k]
        out.append([f"{k:.1f}", fmt([r["on_time_rate"] * 100 for r in sel], 1), fmt([r["on_time_p5"] * 100 for r in sel], 1),
                    fmt([r["planned_unassigned"] for r in sel], 1), fmt([r["vehicles_used"] for r in sel], 1),
                    fmt([r["mean_distance_m"] / 1000 for r in sel], 1), fmt([r["mean_makespan_s"] / 60 for r in sel], 0)])
    return table(["κ (buffer)", "On-time % (mean over days)", "On-time % (worst 5% of days)", "Requests refused at planning",
                  "Vehicles used", "Distance km", "Makespan min"], out)


def sum_e7(rows):
    q = [r for r in rows if r["kind"] == "query"][0]
    t1 = table(["Query (mean of 30 trips)", "Static Dijkstra", "Static A*", "Time-dependent Dijkstra", "Time-dependent A*"], [
        ["Time ms", f"{q['dijkstra_ms']:.3f}", f"{q['astar_ms']:.3f}", f"{q['td_dijkstra_ms']:.3f}", f"{q['td_astar_ms']:.3f}"],
        ["Nodes expanded", f"{q['dijkstra_expanded']:.0f}", f"{q['astar_expanded']:.0f}", f"{q['td_dijkstra_expanded']:.0f}", f"{q['td_astar_expanded']:.0f}"]])
    f = [r for r in rows if r["kind"] == "fleet"]
    t2 = table(["Requests", "Vehicles", "Matrix nodes", "Matrix searches", "Matrix ms", "ALNS ms", "ALNS iterations", "Unassigned", "Peak RSS MB"],
               [[str(r["requests"]), str(r["vehicles"]), str(r["matrix_nodes"]), str(r["matrix_searches"]), f"{r['matrix_ms']:.0f}",
                 f"{r['solve_ms']:.0f}", str(r["iterations"]), str(r["unassigned"]), f"{r['rss_mb']:.0f}"] for r in f])
    return t1 + "\n" + t2


def sum_gap(rows):
    ex = [r for r in rows if r["kind"] == "exact"]
    rf = [r for r in rows if r["kind"] == "reference"]
    s = ""
    if ex:
        s += table(["Exact (brute force, 7 requests, 2 vehicles)", "Instances", "ALNS gap % (mean ± CI)", "Worst gap %"],
                   [["ALNS, 400 iterations", str(len(ex)), fmt([r["gap_pct"] for r in ex], 2), f"{max(r['gap_pct'] for r in ex):.2f}"]])
    if rf:
        s += "\n" + table(["Reference (40 requests, 5 vehicles; 12 000-iteration ALNS)", "Instances", "Gap %"],
                          [["Greedy regret insertion only", str(len(rf)), fmt([r["greedy_gap_pct"] for r in rf], 2)],
                           ["ALNS, 1 200 iterations", str(len(rf)), fmt([r["gap_pct"] for r in rf], 2)]])
    return s


SUMMARIES = [
    ("E1", "E1 — static vs time-dependent traffic", "How much do changing edge costs alter route quality?", sum_e1, {"pairs": 40}),
    ("E2", "E2 — deterministic vs stochastic travel times", "How does uncertainty affect deadline reliability?", sum_e2, {"pairs": 20, "scenarios": 150}),
    ("E3", "E3 — normal network vs disruptions (single vehicle)", "How robust are routes to closures and incidents?", sum_e3, {"pairs": 40}),
    ("E3", "E4a — full recomputation vs selective local repair (single vehicle)", "Does repairing only the affected stretch reduce latency without hurting the route?", sum_e4_single, None),
    ("E4", "E4b — replanning strategies for a fleet", "Can selective, uncertainty-aware replanning beat static routing and complete recomputation?", sum_e4, {}),
    ("E5", "E5 — single vs multiple vehicles", "How does coordination affect system-wide cost?", sum_e5, {}),
    ("E6R", "E6a — expected-time vs reliability-aware routing (one trip)", "What is the efficiency-reliability trade-off?", sum_e6r, {"pairs": 20, "scenarios": 150}),
    ("E6F", "E6b — reliability buffer in fleet plans", "What does a safety buffer κ cost, and what does it buy?", sum_e6f, {"scenarios": 100}),
    ("E7", "E7 — scalability", "How does performance scale with graph and request size?", sum_e7, {}),
    ("GAP", "Optimality gap", "How far is the ALNS from an exact / strong reference solution?", sum_gap, {}),
]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--graphs", nargs="*", default=DEFAULT_GRAPHS)
    ap.add_argument("--seeds", type=int, default=5)
    ap.add_argument("--quick", action="store_true", help="fewer seeds and trips (a few minutes)")
    ap.add_argument("--only", default="", help="comma-separated experiments, e.g. E4,E7")
    ap.add_argument("--scale", default="60,120,220",
                    help="synthetic grid sizes (rows = cols) for the graph-size scaling table; empty to skip")
    ap.add_argument("--render-only", action="store_true",
                    help="rebuild the report from the raw JSON in experiments/results/ without running anything")
    ap.add_argument("--out", default=str(ROOT / "docs" / "RESULTS.md"))
    args = ap.parse_args()
    if not ENGINE.exists() and not args.render_only:
        sys.exit("engine not built: run engine/build.sh")
    only = {x.strip() for x in args.only.split(",") if x.strip()}
    seeds = 3 if args.quick else args.seeds
    scale = 0.5 if args.quick else 1.0

    sections: dict[str, list[str]] = {}
    graph_info = []
    for g in args.graphs:
        path = (ROOT / g) if not Path(g).is_absolute() else Path(g)
        print(f"== {path.name}")
        cache: dict[str, dict] = {}
        info = None
        for key, title, question, summarise, opts in SUMMARIES:
            if only and key not in only:
                continue
            if key not in cache:
                o = dict(opts or {})
                for k in ("pairs", "scenarios"):
                    if k in o:
                        o[k] = max(8, int(o[k] * scale))
                if key not in ("E1", "E3", "E7", "GAP") or key in ("GAP",):
                    o["seeds"] = seeds
                if args.render_only:
                    cache[key] = json.loads((RESULTS / f"{path.stem}_{key}.json").read_text())
                else:
                    cache[key] = run_bench(path, key, **o)
                    save(f"{path.stem}_{key}", cache[key])
            data = cache[key]
            info = info or data
            sections.setdefault(title, [question, summarise.__name__])
            sections[title].append(f"#### {path.stem}  ({data['nodes']:,} nodes, {data['edges']:,} edges)\n\n{summarise(data['rows'])}")
        if info:
            graph_info.append(f"`{path.name}`: {info['nodes']:,} nodes, {info['edges']:,} directed edges")

    if (not only or "E7" in only) and args.scale:
        title = "E7b — scaling with graph size (synthetic grids)"
        rows_md = []
        gdir = RESULTS / "graphs"
        gdir.mkdir(parents=True, exist_ok=True)
        for size in [int(x) for x in args.scale.split(",") if x.strip()]:
            name = f"grid-{size}"
            gpath = gdir / f"{name}.graph"
            if not gpath.exists() and not args.render_only:
                subprocess.run([sys.executable, str(ROOT / "scripts" / "make_demo_graph.py"), "--rows", str(size), "--cols", str(size),
                                "--name", name, "--out-dir", str(gdir)], check=True, capture_output=True)
            if args.render_only:
                d = json.loads((RESULTS / f"{name}_E7.json").read_text())
            else:
                d = run_bench(gpath, "E7", max_requests=40)
                save(name + "_E7", d)
            q = [r for r in d["rows"] if r["kind"] == "query"][0]
            f40 = [r for r in d["rows"] if r["kind"] == "fleet" and r["requests"] == 40][0]
            rows_md.append([f"{d['nodes']:,}", f"{d['edges']:,}", f"{q['dijkstra_ms']:.2f}", f"{q['astar_ms']:.2f}", f"{q['td_dijkstra_ms']:.2f}",
                            f"{q['td_astar_ms']:.2f}", f"{f40['matrix_ms'] / 1000:.1f}", f"{f40['solve_ms']:.0f}", f"{f40['rss_mb']:.0f}"])
        sections[title] = ["How do query time and fleet planning grow with the number of nodes? (40 requests, 5 vehicles)", "scale",
                           table(["Nodes", "Edges", "Dijkstra ms", "A* ms", "TD Dijkstra ms", "TD A* ms", "Matrix s (41 nodes × 9 buckets)",
                                  "ALNS ms (1200 it.)", "Peak RSS MB"], rows_md)]
    md = [
        "# Experimental results",
        "",
        "_Generated by `experiments/run_experiments.py`; do not edit by hand. Interpretation: [`experiments/commentary.md`](../experiments/commentary.md) is appended below._",
        "",
        f"- Machine: {platform.platform()}, {platform.machine()}; engine built with `c++ -O2`; run {time.strftime('%Y-%m-%d')}",
        f"- Graphs: {'; '.join(graph_info)}",
        f"- Seeds per configuration: {seeds}; intervals are 95% Student-t confidence intervals over seeds / trips",
        "- **Traffic is synthetic** (a daily curve with morning and evening peaks, heavier towards the centre, plus correlated "
        "lognormal noise). No measured traffic feed is used, so absolute times are illustrative; the comparisons between methods "
        "are what the experiments measure.",
        "- Runtimes are wall-clock on the machine above (single thread) and vary between machines; all quality numbers are reproducible.",
        "",
    ]
    for title, parts in sections.items():
        md += [f"## {title}", "", f"*{parts[0]}*", ""] + [p + "\n" for p in parts[2:]]
    commentary = ROOT / "experiments" / "commentary.md"
    if commentary.exists() and not only:
        md += ["", commentary.read_text()]
    Path(args.out).write_text("\n".join(md))
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()

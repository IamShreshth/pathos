import { useCallback, useEffect, useRef, useState } from "react";
import { ApiError, api } from "./api/client";
import type { Algorithm, Coordinate, GraphMetadata, GraphSummary, RouteResponse, TourResponse, Weight } from "./api/types";
import { AreaPanel, OutsideNotice } from "./components/AreaPanel";
import { RoutePanel } from "./components/RoutePanel";
import { TourPanel } from "./components/TourPanel";
import { ALGO_COLOR, ALGO_LABEL, TOUR_COLOR, km, minutes } from "./format";
import { type AreaPlan, inCoverage, planArea } from "./geo";
import type { MapController } from "./map/MapController";
import { MapView } from "./map/MapView";

type Mode = "route" | "tour";
const ALL: Algorithm[] = ["bfs", "dijkstra", "astar"];

export default function App() {
  const [ctrl, setCtrl] = useState<MapController | null>(null);
  const [meta, setMeta] = useState<GraphMetadata | null>(null);
  const [mode, setMode] = useState<Mode>("route");
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [weight, setWeight] = useState<Weight>("distance");

  // Route mode
  const [source, setSource] = useState<Coordinate | null>(null);
  const [destination, setDestination] = useState<Coordinate | null>(null);
  const [algorithms, setAlgorithms] = useState<Algorithm[]>(ALL);
  const [speed, setSpeed] = useState(30);
  const [route, setRoute] = useState<RouteResponse | null>(null);
  const [progress, setProgress] = useState<number[] | null>(null);

  // Tour mode
  const [stops, setStops] = useState<Coordinate[]>([]);
  const [tour, setTour] = useState<TourResponse | null>(null);

  // Map areas
  const [graphs, setGraphs] = useState<GraphSummary[]>([]);
  const [areaBusy, setAreaBusy] = useState<string | null>(null);
  const [outside, setOutside] = useState<AreaPlan | null>(null);
  const [radius, setRadius] = useState(3000);

  const requestId = useRef(0);

  // ----------------------------------------------------------- metadata
  const refreshGraphs = useCallback(() => {
    api.graphs().then(setGraphs).catch(() => setGraphs([]));
  }, []);
  useEffect(() => {
    api
      .metadata()
      .then(setMeta)
      .catch((e: Error) => setError(e.message));
    refreshGraphs();
  }, [refreshGraphs]);
  useEffect(() => {
    if (ctrl && meta) ctrl.showCoverage(meta);
  }, [ctrl, meta]);
  useEffect(() => {
    if (ctrl && outside) ctrl.previewArea(outside.center, outside.radius_m, outside.point);
  }, [ctrl, outside]);

  // ---------------------------------------------------------- map areas
  /** A new road graph is active: clear everything tied to the old node IDs, then restore points. */
  const applyGraph = useCallback(
    (m: GraphMetadata, keep: Coordinate[]) => {
      requestId.current++;
      ctrl?.clearAll();
      setOutside(null);
      setError(null);
      setLoading(false);
      setRoute(null);
      setProgress(null);
      setTour(null);
      setMeta(m);
      if (mode === "route") {
        setSource(keep[0] ?? null);
        setDestination(keep[1] ?? null);
      } else {
        setStops(keep);
      }
      refreshGraphs();
    },
    [ctrl, mode, refreshGraphs],
  );

  const runAreaTask = (message: string, task: Promise<GraphMetadata>, keep: Coordinate[]) => {
    setAreaBusy(message);
    setError(null);
    task
      .then((m) => applyGraph(m, keep))
      .catch((e: Error) => setError(e.message))
      .finally(() => setAreaBusy(null));
  };

  const loadPlannedArea = (plan: AreaPlan) =>
    runAreaTask(
      "Downloading roads from OpenStreetMap… this can take up to a minute.",
      api.loadArea({ center: plan.center }, plan.radius_m),
      plan.keep,
    );

  /** Turn an "outside the loaded map" answer from the server into a load-area offer. */
  const handleError = useCallback(
    (e: Error, points: Coordinate[]) => {
      const oc = e instanceof ApiError ? e.outsideCoverage : null;
      if (oc && meta && oc.distance_to_area_m > 0) {
        setError(null);
        setOutside(planArea(points, oc.point, radius, meta.max_area_radius_m));
      } else {
        setError(e.message);
      }
    },
    [meta, radius],
  );

  // ------------------------------------------------------------- clicks
  const onMapClick = useCallback(
    (c: Coordinate) => {
      if (areaBusy) return;
      setError(null);
      setOutside(null);
      if (meta && !inCoverage(meta, c)) {
        // Offer to download this area instead of sending a request that must fail.
        const points =
          mode === "route" ? (source && !destination ? [source, c] : [c]) : [...stops, c];
        setOutside(planArea(points, c, radius, meta.max_area_radius_m));
        return;
      }
      if (mode === "route") {
        if (!source || destination) {
          setSource(c);
          setDestination(null);
          setRoute(null);
        } else {
          setDestination(c);
        }
      } else {
        const max = meta?.max_tour_stops ?? 25;
        if (stops.length >= max) {
          setError(`At most ${max} stops are allowed.`);
          return;
        }
        setStops((s) => [...s, c]);
        setTour(null);
      }
    },
    [mode, source, destination, stops, meta, radius, areaBusy],
  );

  const switchMode = (m: Mode) => {
    requestId.current++;
    ctrl?.clearSearch();
    ctrl?.clearRoutes();
    setMode(m);
    setError(null);
    setLoading(false);
  };

  // ------------------------------------------------------- route search
  const playRoute = useCallback(
    (res: RouteResponse, eventsPerFrame: number) => {
      if (!ctrl) return;
      ctrl.clearRoutes();
      const n = res.results.length;
      // Overlapping routes are stacked: the first algorithm widest at the bottom.
      const draw = (i: number) => {
        const r = res.results[i];
        if (r.geometry)
          ctrl.drawRoute(r.geometry, ALGO_COLOR[r.algorithm], {
            width: 3 + 3 * (n - 1 - i),
            tooltip: `${ALGO_LABEL[r.algorithm]}: ${km(r.distance_m)}, ${minutes(r.time_s)}, ${r.segments} segments`,
          });
      };
      ctrl.animate(
        res.results.map((r, i) => ({ color: ALGO_COLOR[r.algorithm], segments: r.trace ?? [], onDone: () => draw(i) })),
        eventsPerFrame,
        setProgress,
        () => {
          // Redraw in a fixed order once every search has finished.
          ctrl.fadeSearch();
          ctrl.clearRoutes();
          res.results.forEach((_, i) => draw(i));
        },
      );
    },
    [ctrl],
  );

  const algoKey = algorithms.join(",");
  useEffect(() => {
    if (mode !== "route" || !source || !destination || algorithms.length === 0) return;
    const id = ++requestId.current;
    setLoading(true);
    setError(null);
    ctrl?.clearSearch();
    ctrl?.clearRoutes();
    api
      .compare(source, destination, algorithms, weight)
      .then((res) => {
        if (id !== requestId.current) return;
        setRoute(res);
        playRoute(res, speed);
        const lines = res.results.flatMap((r) => (r.geometry ? [r.geometry] : []));
        if (lines.length) ctrl?.fitTo(lines);
      })
      .catch((e: Error) => id === requestId.current && handleError(e, [source, destination]))
      .finally(() => id === requestId.current && setLoading(false));
    // `speed` is read when the search starts; changing it must not refetch.
    // `mode` is a dependency so switching back to this tab redraws the routes.
  }, [mode, source, destination, algoKey, weight, ctrl, playRoute, handleError]);

  // ---------------------------------------------------------- tour search
  const planTour = () => {
    const id = ++requestId.current;
    setLoading(true);
    setError(null);
    api
      .tour(stops, weight)
      .then((res) => {
        if (id !== requestId.current) return;
        setTour(res);
        if (res.status === "unreachable")
          setError("Some legs are unreachable because of one-way streets or disconnected roads (shown in red).");
      })
      .catch((e: Error) => id === requestId.current && handleError(e, stops))
      .finally(() => id === requestId.current && setLoading(false));
  };

  // ------------------------------------------------------------ drawing
  useEffect(() => {
    if (!ctrl) return;
    ctrl.clearMarkers();
    if (mode === "route") {
      if (source) ctrl.addPin(source, "A", ALGO_COLOR.astar, "source");
      if (destination) ctrl.addPin(destination, "B", "#c92a2a", "destination");
      if (route) {
        ctrl.addSnap(route.source, ALGO_COLOR.astar);
        ctrl.addSnap(route.destination, "#c92a2a");
      }
    } else {
      // Same labels as the tour panel: S for the start, then the stop number.
      const visit = new Map<number, number>();
      tour?.order.forEach((stop, pos) => visit.set(stop, pos));
      stops.forEach((s, i) => {
        const pos = visit.get(i);
        const tip = pos !== undefined && i !== 0 ? `stop ${i + 1} · visited ${pos}${ordinal(pos)}` : `stop ${i + 1}`;
        ctrl.addPin(s, i === 0 ? "S" : String(i + 1), i === 0 ? "#2b8a3e" : TOUR_COLOR, tip);
      });
      tour?.stops.forEach((p) => ctrl.addSnap(p, TOUR_COLOR));
    }
  }, [ctrl, mode, source, destination, route, stops, tour]);

  useEffect(() => {
    if (!ctrl || mode !== "tour") return;
    ctrl.clearSearch();
    ctrl.clearRoutes();
    if (!tour) return;
    tour.legs.forEach((leg, i) => {
      if (leg.geometry) {
        ctrl.drawRoute(leg.geometry, TOUR_COLOR, {
          tooltip: `leg ${i + 1}: stop ${leg.from_stop + 1} → stop ${leg.to_stop + 1} · ${km(leg.distance_m)} · ${minutes(leg.time_s)}`,
        });
      } else {
        const a = tour.stops[leg.from_stop];
        const b = tour.stops[leg.to_stop];
        ctrl.drawRoute(
          { type: "LineString", coordinates: [[a.lon, a.lat], [b.lon, b.lat]] },
          "#e03131",
          { dashed: true, tooltip: `leg ${i + 1} is unreachable` },
        );
      }
    });
    const lines = tour.legs.flatMap((l) => (l.geometry ? [l.geometry] : []));
    if (lines.length) ctrl.fitTo(lines);
  }, [ctrl, mode, tour]);

  const clearRoute = () => {
    requestId.current++;
    setSource(null);
    setDestination(null);
    setRoute(null);
    setProgress(null);
    setLoading(false);
    ctrl?.clearSearch();
    ctrl?.clearRoutes();
  };

  const clearTour = () => {
    requestId.current++;
    setStops([]);
    setTour(null);
    setLoading(false);
  };

  return (
    <div className="app">
      <aside className="sidebar">
        <header>
          <h1>pathos</h1>
          <p className="tagline">Fewer wrong turns, more right answers.</p>
        </header>

        <div className="tabs" role="tablist">
          <button role="tab" aria-selected={mode === "route"} className={mode === "route" ? "active" : ""} onClick={() => switchMode("route")}>
            Compare routes
          </button>
          <button role="tab" aria-selected={mode === "tour"} className={mode === "tour" ? "active" : ""} onClick={() => switchMode("tour")}>
            Multi-stop tour
          </button>
        </div>

        <label className="field">
          <span>Optimise for</span>
          <select value={weight} onChange={(e) => setWeight(e.target.value as Weight)}>
            <option value="distance">Shortest distance</option>
            <option value="time">Fastest (estimated free-flow time)</option>
          </select>
        </label>

        <AreaPanel
          meta={meta}
          graphs={graphs}
          busy={areaBusy}
          radius={radius}
          onRadius={setRadius}
          onLoadPlace={(place) =>
            runAreaTask(`Finding “${place}” and downloading its roads… this can take up to a minute.`, api.loadArea({ place }, radius), [])
          }
          onLoadMapCentre={() =>
            ctrl &&
            runAreaTask(
              "Downloading roads around the map centre… this can take up to a minute.",
              api.loadArea({ center: ctrl.center() }, radius),
              [],
            )
          }
          onSelect={(name) => runAreaTask(`Switching to ${name}…`, api.selectGraph(name), [])}
        />

        {areaBusy && <div className="instructions busy">{areaBusy}</div>}
        {outside && meta && !areaBusy && (
          <OutsideNotice
            plan={outside}
            graphVersion={meta.graph_version}
            busy={areaBusy !== null}
            onLoad={() => loadPlannedArea(outside)}
            onCancel={() => {
              setOutside(null);
              ctrl?.previewArea(null);
              ctrl?.showCoverage(meta);
            }}
          />
        )}
        {error && <div className="error">{error}</div>}

        {mode === "route" ? (
          <RoutePanel
            source={source}
            destination={destination}
            algorithms={algorithms}
            onToggleAlgorithm={(a) =>
              setAlgorithms((cur) => (cur.includes(a) ? cur.filter((x) => x !== a) : ALL.filter((x) => x === a || cur.includes(x))))
            }
            speed={speed}
            onSpeed={setSpeed}
            loading={loading}
            route={route}
            progress={progress}
            onReplay={() => route && playRoute(route, speed)}
            onClear={clearRoute}
          />
        ) : (
          <TourPanel
            stops={stops}
            maxStops={meta?.max_tour_stops ?? 25}
            loading={loading}
            tour={tour}
            onPlan={planTour}
            onUndo={() => {
              setStops((s) => s.slice(0, -1));
              setTour(null);
            }}
            onClear={clearTour}
          />
        )}

        <footer>
          {meta ? (
            <>
              Graph <code>{meta.graph_version}</code>: {meta.nodes.toLocaleString()} nodes, {meta.edges.toLocaleString()} directed edges
              {meta.source ? ` · ${meta.source}` : ""}
            </>
          ) : (
            "Loading graph…"
          )}
        </footer>
      </aside>
      <main>
        <MapView onReady={setCtrl} onClick={onMapClick} />
      </main>
    </div>
  );
}

function ordinal(n: number): string {
  const s = ["th", "st", "nd", "rd"];
  const v = n % 100;
  return s[(v - 20) % 10] ?? s[v] ?? s[0];
}

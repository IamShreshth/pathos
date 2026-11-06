import L from "leaflet";
import { useEffect, useState } from "react";
import { api } from "../api/client";
import type { Coordinate, FleetMetrics, FleetRequestBody, FleetResponse, GenerateSpec, Strategy } from "../api/types";
import { STRATEGY_COLOR, STRATEGY_HINT, STRATEGY_LABEL, km, ms, pct } from "../format";
import { type MapController, pinIcon } from "../map/MapController";
import { FleetPlayer } from "./FleetPlayer";

interface Props {
  ctrl: MapController | null;
  register: (h: ((c: Coordinate) => void) | null) => void;
  onError: (e: Error, points: Coordinate[]) => void;
  onBusy: (b: boolean) => void;
  maxStops: number;
}

const ALL: Strategy[] = ["static", "greedy", "full", "selective"];

export function FleetPanel({ ctrl, register, onError, onBusy }: Props) {
  const [source, setSource] = useState<"random" | "custom">("random");
  const [gen, setGen] = useState<GenerateSpec>({ seed: 1, vehicles: 3, requests: 40, dynamic_fraction: 0.4, window_min: 25, closures: 2, incidents: 1 });
  const [depot, setDepot] = useState<Coordinate | null>(null);
  const [stops, setStops] = useState<Coordinate[]>([]);
  const [custom, setCustom] = useState({ vehicles: 3, dynamic_fraction: 0.3, window_min: 30, closures: 2, incidents: 1, seed: 1 });
  const [strategies, setStrategies] = useState<Strategy[]>(ALL);
  const [kappa, setKappa] = useState(0);
  const [beta, setBeta] = useState(120);
  const [scenario, setScenario] = useState(1);
  const [mc, setMc] = useState(true);
  const [loading, setLoading] = useState(false);
  const [data, setData] = useState<FleetResponse | null>(null);
  const [view, setView] = useState<Strategy>("selective");

  useEffect(() => {
    if (source !== "custom") return register(null);
    register((c) => {
      if (!depot) setDepot(c);
      else setStops((s) => [...s, c]);
      setData(null);
    });
    return () => register(null);
  }, [register, source, depot]);

  useEffect(() => {
    if (!ctrl) return;
    ctrl.clearLayer("pins");
    if (data || source !== "custom") return;
    const g = ctrl.layer("pins");
    if (depot) L.marker([depot.lat, depot.lon], { icon: pinIcon("D", "#212529") }).addTo(g);
    stops.forEach((s, i) => L.marker([s.lat, s.lon], { icon: pinIcon(String(i + 1), "#7048e8") }).addTo(g));
  }, [ctrl, source, depot, stops, data]);

  useEffect(
    () => () => {
      ctrl?.clearLayers();
      ctrl?.clearRoutes();
      onBusy(false);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [ctrl],
  );

  const toggle = (s: Strategy) => setStrategies((cur) => (cur.includes(s) ? cur.filter((x) => x !== s) : ALL.filter((x) => x === s || cur.includes(x))));

  const run = () => {
    const body: FleetRequestBody = {
      strategies,
      kappa,
      beta,
      iters_per_request: 30,
      scenario,
      mc_scenarios: mc ? 100 : 0,
    };
    if (source === "random") body.generate = gen;
    else if (depot) body.custom = { depot, stops, ...custom };
    setLoading(true);
    onBusy(true);
    ctrl?.clearLayer("pins");
    api
      .fleet(body)
      .then((r) => {
        setData(r);
        setView(r.results.some((x) => x.strategy === "selective") ? "selective" : r.results[0].strategy);
      })
      .catch((e: Error) => onError(e, [...(depot ? [depot] : []), ...stops]))
      .finally(() => {
        setLoading(false);
        onBusy(false);
      });
  };

  const canRun = strategies.length > 0 && !loading && (source === "random" || (depot !== null && stops.length >= 2));
  const result = data?.results.find((r) => r.strategy === view) ?? data?.results[0];

  return (
    <section className="panel">
      <p className="instructions">
        A fleet serves pick-up requests with time windows and capacities. Some requests arrive during the day and roads close or have
        accidents. Four strategies react differently; the replay shows what actually happened under random travel-time noise.
      </p>

      <div className="tabs small" role="tablist">
        <button className={source === "random" ? "active" : ""} onClick={() => { setSource("random"); setData(null); }}>
          Random scenario
        </button>
        <button className={source === "custom" ? "active" : ""} onClick={() => { setSource("custom"); setData(null); }}>
          Place depot &amp; stops
        </button>
      </div>

      {source === "random" ? (
        <div className="two">
          <Num label="Vehicles" v={gen.vehicles} min={1} max={20} on={(v) => setGen({ ...gen, vehicles: v })} />
          <Num label="Requests" v={gen.requests} min={4} max={120} on={(v) => setGen({ ...gen, requests: v })} />
          <Num label="Window (min)" v={gen.window_min} min={10} max={120} on={(v) => setGen({ ...gen, window_min: v })} />
          <Num label="Late requests %" v={Math.round(gen.dynamic_fraction * 100)} min={0} max={100} on={(v) => setGen({ ...gen, dynamic_fraction: v / 100 })} />
          <Num label="Road closures" v={gen.closures} min={0} max={6} on={(v) => setGen({ ...gen, closures: v })} />
          <Num label="Accidents" v={gen.incidents} min={0} max={6} on={(v) => setGen({ ...gen, incidents: v })} />
          <Num label="Scenario seed" v={gen.seed} min={1} max={9999} on={(v) => setGen({ ...gen, seed: v })} />
        </div>
      ) : (
        <>
          <p className="muted">
            {!depot ? "Click the map to place the depot (D)." : `Depot placed. Click to add stops (${stops.length}). Windows and disruptions are generated.`}
          </p>
          <div className="two">
            <Num label="Vehicles" v={custom.vehicles} min={1} max={20} on={(v) => setCustom({ ...custom, vehicles: v })} />
            <Num label="Window (min)" v={custom.window_min} min={10} max={120} on={(v) => setCustom({ ...custom, window_min: v })} />
            <Num label="Late requests %" v={Math.round(custom.dynamic_fraction * 100)} min={0} max={100} on={(v) => setCustom({ ...custom, dynamic_fraction: v / 100 })} />
            <Num label="Road closures" v={custom.closures} min={0} max={6} on={(v) => setCustom({ ...custom, closures: v })} />
          </div>
          <div className="buttons">
            <button className="secondary" onClick={() => setStops((s) => s.slice(0, -1))} disabled={stops.length === 0}>
              Undo stop
            </button>
            <button className="secondary" onClick={() => { setDepot(null); setStops([]); setData(null); }}>
              Clear
            </button>
          </div>
        </>
      )}

      <fieldset className="algos">
        <legend>Replanning strategies</legend>
        {ALL.map((s) => (
          <label key={s} className="algo">
            <input type="checkbox" checked={strategies.includes(s)} onChange={() => toggle(s)} />
            <span className="swatch" style={{ background: STRATEGY_COLOR[s] }} />
            <strong>{STRATEGY_LABEL[s]}</strong>
            <span className="hint">{STRATEGY_HINT[s]}</span>
          </label>
        ))}
      </fieldset>

      <details>
        <summary>Advanced</summary>
        <div className="two">
          <label className="field">
            <span>Reliability buffer κ: {kappa.toFixed(1)}</span>
            <input type="range" min={0} max={3} step={0.5} value={kappa} onChange={(e) => setKappa(Number(e.target.value))} />
          </label>
          <label className="field">
            <span>Disruption penalty β: {beta}</span>
            <input type="range" min={0} max={600} step={30} value={beta} onChange={(e) => setBeta(Number(e.target.value))} />
          </label>
          <Num label="Realised day (random seed)" v={scenario} min={1} max={9999} on={setScenario} />
          <label className="check">
            <input type="checkbox" checked={mc} onChange={(e) => setMc(e.target.checked)} /> Reliability of the initial plan over 100 sampled days
          </label>
        </div>
      </details>

      <div className="buttons">
        <button onClick={run} disabled={!canRun}>
          {loading ? "Simulating…" : "Run simulation"}
        </button>
      </div>
      {loading && <p className="instructions">Planning and simulating the day for each strategy — this can take several seconds.</p>}

      {data && result && ctrl && (
        <>
          <Comparison data={data} view={view} onView={setView} />
          <FleetPlayer ctrl={ctrl} data={data} result={result} />
        </>
      )}
    </section>
  );
}

function Num({ label, v, min, max, on }: { label: string; v: number; min: number; max: number; on: (n: number) => void }) {
  return (
    <label className="field">
      <span>{label}</span>
      <input type="number" value={v} min={min} max={max} onChange={(e) => on(Math.min(max, Math.max(min, Number(e.target.value) || min)))} />
    </label>
  );
}

type Col = { key: string; label: string; get: (m: FleetMetrics) => number; fmt: (x: number) => string; better: "low" | "high" };
const COLS: Col[] = [
  { key: "success", label: "On time", get: (m) => m.success_rate, fmt: (x) => pct(x, 1), better: "high" },
  { key: "unserved", label: "Unserved", get: (m) => m.unserved, fmt: (x) => String(x), better: "low" },
  { key: "late", label: "Late", get: (m) => m.late, fmt: (x) => String(x), better: "low" },
  { key: "dist", label: "Distance", get: (m) => m.total_distance_m, fmt: (x) => km(x), better: "low" },
  { key: "lat", label: "Replan time", get: (m) => m.total_latency_ms, fmt: (x) => ms(x), better: "low" },
  { key: "dis", label: "Plan changes", get: (m) => m.schedule_disruption, fmt: (x) => x.toFixed(1), better: "low" },
  { key: "cost", label: "Cost", get: (m) => m.cost, fmt: (x) => Math.round(x).toLocaleString(), better: "low" },
];

function Comparison({ data, view, onView }: { data: FleetResponse; view: Strategy; onView: (s: Strategy) => void }) {
  const best = COLS.map((c) => {
    const vals = data.results.map((r) => c.get(r.metrics));
    return c.better === "low" ? Math.min(...vals) : Math.max(...vals);
  });
  return (
    <>
      <p className="muted">
        {data.requests.length} requests · {data.vehicles.length} vehicles · {data.events.length} disruptions · simulated in{" "}
        {(data.wall_ms / 1000).toFixed(1)} s. Click a row to replay it.
      </p>
      <div className="matrix-wrap">
        <table className="metrics small">
          <thead>
            <tr>
              <th>Strategy</th>
              {COLS.map((c) => (
                <th key={c.key}>{c.label}</th>
              ))}
            </tr>
          </thead>
          <tbody>
            {data.results.map((r) => (
              <tr key={r.strategy} className={`pick ${view === r.strategy ? "selected" : ""}`} onClick={() => onView(r.strategy)}>
                <td>
                  <span className="swatch" style={{ background: STRATEGY_COLOR[r.strategy] }} />
                  {STRATEGY_LABEL[r.strategy]}
                </td>
                {COLS.map((c, i) => {
                  const v = c.get(r.metrics);
                  return (
                    <td key={c.key} className={Math.abs(v - best[i]) < 1e-9 ? "good" : ""}>
                      {c.fmt(v)}
                    </td>
                  );
                })}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {data.mc && (
        <p className="muted">
          Reliability of the initial plan with no replanning, over {data.mc.scenarios} sampled days: on-time rate{" "}
          <strong>{pct(data.mc.on_time_rate, 1)}</strong> on average, <strong>{pct(data.mc.on_time_rate_p5, 1)}</strong> on the worst 5% of days.
        </p>
      )}
    </>
  );
}

import L from "leaflet";
import { useEffect, useRef, useState } from "react";
import { api } from "../api/client";
import type { Coordinate, TdRouteResponse, TrafficResponse } from "../api/types";
import { int, km, minutes, pct } from "../format";
import { type MapController, pinIcon } from "../map/MapController";
import { TrafficChart } from "./TrafficChart";

interface Props {
  ctrl: MapController | null;
  register: (h: ((c: Coordinate) => void) | null) => void;
  onError: (e: Error, points: Coordinate[]) => void;
  onBusy: (b: boolean) => void;
}

const ROLE_COLOR = { freeflow: "#868e96", expected: "#1c7ed6", robust: "#f08c00", reliable: "#2f9e44" } as const;
const ROLE_LABEL = {
  freeflow: "Free-flow (static)",
  expected: "Expected time",
  robust: "Robust (mean + z·σ)",
  reliable: "Reliability-constrained",
} as const;
type Role = keyof typeof ROLE_COLOR;
const LEVEL_COLOR: Record<string, string> = { slow: "#fcc419", heavy: "#f76707", severe: "#e03131", closed: "#212529" };

export function ReliablePanel({ ctrl, register, onError, onBusy }: Props) {
  const [src, setSrc] = useState<Coordinate | null>(null);
  const [dst, setDst] = useState<Coordinate | null>(null);
  const [hour, setHour] = useState(8);
  const [slack, setSlack] = useState(1.15);
  const [alpha, setAlpha] = useState(0.1);
  const [showTraffic, setShowTraffic] = useState(true);
  const [res, setRes] = useState<TdRouteResponse | null>(null);
  const [traffic, setTraffic] = useState<TrafficResponse | null>(null);
  const [loading, setLoading] = useState(false);
  const reqId = useRef(0);

  useEffect(() => {
    register((c) => {
      if (!src || dst) {
        setSrc(c);
        setDst(null);
        setRes(null);
      } else setDst(c);
    });
    return () => register(null);
  }, [register, src, dst]);

  // Congestion map + curve for the chosen hour.
  useEffect(() => {
    const id = window.setTimeout(() => {
      api
        .traffic(hour, showTraffic ? 1.3 : 50)
        .then(setTraffic)
        .catch(() => setTraffic(null));
    }, 200);
    return () => window.clearTimeout(id);
  }, [hour, showTraffic]);

  useEffect(() => {
    if (!ctrl) return;
    ctrl.clearLayer("traffic");
    if (!showTraffic || !traffic) return;
    const g = ctrl.layer("traffic");
    for (const lv of traffic.levels)
      if (lv.coordinates.length)
        L.polyline(
          lv.coordinates.map((l) => l.map(([lon, lat]) => [lat, lon] as [number, number])),
          { color: LEVEL_COLOR[lv.label], weight: lv.label === "closed" ? 5 : 3, opacity: 0.75, interactive: false },
        ).addTo(g);
  }, [ctrl, traffic, showTraffic]);

  // Plan whenever an input changes.
  useEffect(() => {
    if (!src || !dst) return;
    const id = ++reqId.current;
    const t = window.setTimeout(() => {
      setLoading(true);
      onBusy(true);
      api
        .mobilityRoutes(src, dst, hour, slack, alpha)
        .then((r) => id === reqId.current && setRes(r))
        .catch((e: Error) => id === reqId.current && onError(e, [src, dst]))
        .finally(() => {
          if (id === reqId.current) {
            setLoading(false);
            onBusy(false);
          }
        });
    }, 250);
    return () => window.clearTimeout(t);
  }, [src, dst, hour, slack, alpha, onError, onBusy]);

  // Pins and routes.
  useEffect(() => {
    if (!ctrl) return;
    ctrl.clearLayer("pins");
    const g = ctrl.layer("pins");
    if (src) L.marker([src.lat, src.lon], { icon: pinIcon("A", "#2b8a3e") }).addTo(g);
    if (dst) L.marker([dst.lat, dst.lon], { icon: pinIcon("B", "#c92a2a") }).addTo(g);
    ctrl.clearRoutes();
    if (!res) return;
    const order: Role[] = ["reliable", "robust", "expected", "freeflow"];
    const widths = [11, 8, 5, 3];
    order.forEach((role, i) => {
      const c = res.candidates[res[role]];
      ctrl.drawRoute(c.geometry, ROLE_COLOR[role], {
        width: widths[i],
        dashed: role === "freeflow",
        tooltip: `${ROLE_LABEL[role]}: ${minutes(c.mc_mean_s)} mean · P(on time) ${pct(c.p_on_time)}`,
      });
    });
    ctrl.fitTo(res.candidates.map((c) => c.geometry));
  }, [ctrl, src, dst, res]);

  useEffect(
    () => () => {
      reqId.current++;
      ctrl?.clearLayers();
      ctrl?.clearRoutes();
      onBusy(false);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [ctrl],
  );

  const rows = res ? roleRows(res) : [];
  const ff = res?.candidates[res.freeflow];
  const ex = res?.candidates[res.expected];

  return (
    <section className="panel">
      <p className="instructions">
        Click to place the start (A) and destination (B). The traffic model is <strong>synthetic</strong>: a daily curve with
        morning and evening peaks, heaviest near the centre, plus correlated random variation.
      </p>

      <label className="field">
        <span>Departure: {clockOfHour(hour)}</span>
        <input type="range" min={0} max={23.75} step={0.25} value={hour} onChange={(e) => setHour(Number(e.target.value))} />
      </label>
      {traffic && <TrafficChart profile={traffic.profile} hour={hour} />}
      <label className="check">
        <input type="checkbox" checked={showTraffic} onChange={(e) => setShowTraffic(e.target.checked)} /> Show congestion on the map
        <span className="legend">
          <i style={{ background: LEVEL_COLOR.slow }} />slow <i style={{ background: LEVEL_COLOR.heavy }} />heavy{" "}
          <i style={{ background: LEVEL_COLOR.severe }} />severe
        </span>
      </label>

      <div className="two">
        <label className="field">
          <span>Deadline: {Math.round((slack - 1) * 100)}% above the expected time</span>
          <input type="range" min={1.0} max={1.5} step={0.01} value={slack} onChange={(e) => setSlack(Number(e.target.value))} />
        </label>
        <label className="field">
          <span>Tolerated miss probability α</span>
          <select value={alpha} onChange={(e) => setAlpha(Number(e.target.value))}>
            {[0.3, 0.2, 0.1, 0.05, 0.01].map((a) => (
              <option key={a} value={a}>
                {pct(a)} (reliability {pct(1 - a)})
              </option>
            ))}
          </select>
        </label>
      </div>

      {loading && <p className="instructions">Evaluating candidate routes on sampled traffic scenarios…</p>}

      {res && ff && ex && (
        <>
          <table className="metrics">
            <thead>
              <tr>
                <th>Route</th>
                <th>Expected</th>
                <th>p95</th>
                <th>P(on time)</th>
                <th>Distance</th>
              </tr>
            </thead>
            <tbody>
              {rows.map(({ idx, roles }) => {
                const c = res.candidates[idx];
                return (
                  <tr key={idx}>
                    <td>
                      {roles.map((r) => (
                        <span key={r} className="chip" style={{ background: ROLE_COLOR[r] }}>
                          {ROLE_LABEL[r]}
                        </span>
                      ))}
                    </td>
                    <td>{minutes(c.expected_s)}</td>
                    <td>{minutes(c.mc_p95_s)}</td>
                    <td className={c.p_on_time >= 1 - res.alpha ? "good" : ""}>{pct(c.p_on_time)}</td>
                    <td>{km(c.distance_m)}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
          <ul className="insights">
            <li>
              Deadline <strong>{minutes(res.deadline_s)}</strong>. The free-flow plan predicts {minutes(ff.free_s)}, but in traffic at{" "}
              {clockOfHour(hour)} it takes <strong>{minutes(ff.expected_s)}</strong> (
              {(((ff.expected_s - ff.free_s) / ff.free_s) * 100).toFixed(0)}% longer).
            </li>
            <li>
              {res.reliable_feasible ? (
                <>
                  A route with P(on time) ≥ {pct(1 - res.alpha)} exists:{" "}
                  <span className="good">{pct(res.candidates[res.reliable].p_on_time)}</span> versus{" "}
                  {pct(ex.p_on_time)} for the fastest-expected route
                  {res.reliable === res.expected ? " (the same route)" : ""}.
                </>
              ) : (
                <>
                  <span className="bad">No candidate reaches {pct(1 - res.alpha)}</span>; the most reliable one gives{" "}
                  {pct(res.candidates[res.reliable].p_on_time)}. Loosen the deadline or α.
                </>
              )}
            </li>
            <li className="muted">
              {int(res.candidates.length)} distinct candidate routes evaluated on 300 shared scenarios in {res.duration_ms.toFixed(0)} ms.
            </li>
          </ul>
        </>
      )}
      <div className="buttons">
        <button
          className="secondary"
          onClick={() => {
            setSrc(null);
            setDst(null);
            setRes(null);
            ctrl?.clearRoutes();
          }}
        >
          Clear
        </button>
      </div>
    </section>
  );
}

function roleRows(res: TdRouteResponse): { idx: number; roles: Role[] }[] {
  const roles: Role[] = ["freeflow", "expected", "robust", "reliable"];
  const map = new Map<number, Role[]>();
  for (const r of roles) map.set(res[r], [...(map.get(res[r]) ?? []), r]);
  return [...map.entries()].map(([idx, rs]) => ({ idx, roles: rs }));
}

export function clockOfHour(h: number): string {
  const m = Math.round(h * 60);
  return `${String(Math.floor(m / 60) % 24).padStart(2, "0")}:${String(m % 60).padStart(2, "0")}`;
}

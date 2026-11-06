import L from "leaflet";
import { useEffect, useRef, useState } from "react";
import { api } from "../api/client";
import type { Coordinate, ReplanResponse, ReplanStrategyOut } from "../api/types";
import { int, km, mmss, ms } from "../format";
import { type MapController, pinIcon } from "../map/MapController";
import { clockOfHour } from "./ReliablePanel";

interface Props {
  ctrl: MapController | null;
  register: (h: ((c: Coordinate) => void) | null) => void;
  onError: (e: Error, points: Coordinate[]) => void;
  onBusy: (b: boolean) => void;
}

const COLORS = { original: "#868e96", static: "#c92a2a", full: "#1c7ed6", selective: "#2f9e44" } as const;

export function DisruptionPanel({ ctrl, register, onError, onBusy }: Props) {
  const [src, setSrc] = useState<Coordinate | null>(null);
  const [dst, setDst] = useState<Coordinate | null>(null);
  const [type, setType] = useState<"closure" | "incident">("closure");
  const [duration, setDuration] = useState(60);
  const [ahead, setAhead] = useState(10);
  const [hour, setHour] = useState(8);
  const [res, setRes] = useState<ReplanResponse | null>(null);
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

  useEffect(() => {
    if (!src || !dst) return;
    const id = ++reqId.current;
    const t = window.setTimeout(() => {
      setLoading(true);
      onBusy(true);
      api
        .replan(src, dst, { depart_hour: hour, type, duration_min: duration, edges_ahead: ahead })
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
  }, [src, dst, type, duration, ahead, hour, onError, onBusy]);

  useEffect(() => {
    if (!ctrl) return;
    ctrl.clearLayer("pins");
    const g = ctrl.layer("pins");
    if (src) L.marker([src.lat, src.lon], { icon: pinIcon("A", "#2b8a3e") }).addTo(g);
    if (dst) L.marker([dst.lat, dst.lon], { icon: pinIcon("B", "#c92a2a") }).addTo(g);
    ctrl.clearRoutes();
    if (!res) return;
    // Disrupted roads, then the routes stacked so overlapping ones stay visible.
    for (const line of res.disrupted)
      L.polyline(
        line.coordinates.map(([lon, lat]) => [lat, lon] as [number, number]),
        { color: res.type === "closure" ? "#212529" : "#e8590c", weight: 9, opacity: 0.9, dashArray: "2 8", interactive: false },
      ).addTo(g);
    L.marker([res.event_point.lat, res.event_point.lon], { icon: pinIcon("▲", "#e8590c") })
      .bindTooltip(`vehicle position when the ${res.type} is announced (${clockOfHour(res.event_s / 3600)})`)
      .addTo(g);
    ctrl.drawRoute(res.original.geometry, COLORS.original, { width: 11, tooltip: "original plan (and what 'static' keeps driving)" });
    ctrl.drawRoute(res.full.geometry, COLORS.full, { width: 7, tooltip: "full recomputation" });
    ctrl.drawRoute(res.selective.geometry, COLORS.selective, { width: 3.5, tooltip: "selective local repair" });
    ctrl.fitTo([res.original.geometry, res.full.geometry, res.selective.geometry]);
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

  return (
    <section className="panel">
      <p className="instructions">
        Click A and B. The vehicle sets off; shortly after, a road ahead on its route closes (or has an accident). Compare three
        responses: keep driving, recompute everything, or repair only the affected stretch.
      </p>
      <div className="two">
        <label className="field">
          <span>Disruption</span>
          <select value={type} onChange={(e) => setType(e.target.value as "closure" | "incident")}>
            <option value="closure">Road closure</option>
            <option value="incident">Accident (4× slower)</option>
          </select>
        </label>
        <label className="field">
          <span>Lasts {duration} min</span>
          <input type="range" min={15} max={180} step={15} value={duration} onChange={(e) => setDuration(Number(e.target.value))} />
        </label>
        <label className="field">
          <span>Hits {ahead} road segments ahead</span>
          <input type="range" min={3} max={40} value={ahead} onChange={(e) => setAhead(Number(e.target.value))} />
        </label>
        <label className="field">
          <span>Departure {clockOfHour(hour)}</span>
          <input type="range" min={0} max={23.5} step={0.5} value={hour} onChange={(e) => setHour(Number(e.target.value))} />
        </label>
      </div>
      {loading && <p className="instructions">Replanning…</p>}
      {res && <Results res={res} />}
      <div className="buttons">
        <button
          className="secondary"
          onClick={() => {
            setSrc(null);
            setDst(null);
            setRes(null);
          }}
        >
          Clear
        </button>
      </div>
    </section>
  );
}

function Results({ res }: { res: ReplanResponse }) {
  const rows: { key: keyof typeof COLORS; label: string; s: ReplanStrategyOut }[] = [
    { key: "original", label: "Original plan (no disruption)", s: res.original },
    { key: "static", label: "Keep the plan (wait it out)", s: res.static },
    { key: "full", label: "Full recomputation", s: res.full },
    { key: "selective", label: "Selective local repair", s: res.selective },
  ];
  const planned = res.original.travel_s ?? 0;
  const best = Math.min(res.static.travel_s ?? Infinity, res.full.travel_s ?? Infinity, res.selective.travel_s ?? Infinity);
  const saved = res.full.expanded_nodes > 0 ? 1 - res.selective.expanded_nodes / res.full.expanded_nodes : 0;
  const gap = (res.selective.travel_s ?? 0) - (res.full.travel_s ?? 0);
  return (
    <>
      <table className="metrics">
        <thead>
          <tr>
            <th>Strategy</th>
            <th>Trip time</th>
            <th>Delay</th>
            <th>Nodes searched</th>
            <th>Compute</th>
          </tr>
        </thead>
        <tbody>
          {rows.map(({ key, label, s }) => (
            <tr key={key}>
              <td>
                <span className="swatch" style={{ background: COLORS[key] }} />
                {label}
              </td>
              <td className={key !== "original" && s.travel_s === best ? "good" : ""}>{mmss(s.travel_s)}</td>
              <td>{key === "original" ? "—" : `+${mmss((s.travel_s ?? 0) - planned)}`}</td>
              <td>{key === "original" || key === "static" ? "—" : int(s.expanded_nodes)}</td>
              <td>{key === "original" || key === "static" ? "—" : ms(s.duration_ms)}</td>
            </tr>
          ))}
        </tbody>
      </table>
      <ul className="insights">
        <li>
          Distance: original {km(res.original.distance_m)}, recomputed {km(res.full.distance_m)}, repaired {km(res.selective.distance_m)}.
        </li>
        {res.full.expanded_nodes > 0 && (
          <li>
            Selective repair searched <strong>{(saved * 100).toFixed(0)}% {saved >= 0 ? "fewer" : "more"}</strong> nodes than full
            recomputation and arrived{" "}
            {Math.abs(gap) < 1 ? "at the same time" : gap > 0 ? `${mmss(gap)} later (the price of keeping the old route)` : `${mmss(-gap)} earlier`}.
          </li>
        )}
        {res.selective.escalated && <li className="bad">The local repair found no way back to the old route and fell back to a full search.</li>}
        {res.selective.affected === false && <li>The remaining route is not affected by the disruption: nothing was recomputed.</li>}
      </ul>
    </>
  );
}

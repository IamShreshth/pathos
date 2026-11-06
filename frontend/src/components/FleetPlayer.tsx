import L from "leaflet";
import { useEffect, useRef, useState } from "react";
import type { FleetResponse, FleetResult } from "../api/types";
import { STRATEGY_COLOR, clock, ms } from "../format";
import { type MapController, pinIcon } from "../map/MapController";

const VEHICLE_COLORS = ["#1c7ed6", "#e8590c", "#7048e8", "#0ca678", "#d6336c", "#f08c00", "#364fc7", "#2b8a3e", "#a61e4d", "#495057"];
export const vehicleColor = (i: number) => VEHICLE_COLORS[i % VEHICLE_COLORS.length];

interface Props {
  ctrl: MapController;
  data: FleetResponse;
  result: FleetResult;
}

/** Position of a vehicle at time t along its [lon, lat, t] trajectory. */
function positionAt(points: [number, number, number][], t: number): [number, number] {
  if (points.length === 0) return [0, 0];
  if (t <= points[0][2]) return [points[0][1], points[0][0]];
  const last = points[points.length - 1];
  if (t >= last[2]) return [last[1], last[0]];
  let lo = 0;
  let hi = points.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (points[mid][2] <= t) lo = mid;
    else hi = mid;
  }
  const [lon0, lat0, t0] = points[lo];
  const [lon1, lat1, t1] = points[hi];
  const f = t1 > t0 ? (t - t0) / (t1 - t0) : 0;
  return [lat0 + (lat1 - lat0) * f, lon0 + (lon1 - lon0) * f];
}

const STATE_COLOR = { future: "#ced4da", waiting: "#4dabf7", overdue: "#e03131", ontime: "#2f9e44", late: "#f08c00", missed: "#c92a2a" };

/** Animated replay of one strategy: vehicles drive, requests turn green when served, disruptions appear and expire. */
export function FleetPlayer({ ctrl, data, result }: Props) {
  const tEnd = Math.max(
    data.t_start_s + 3600,
    ...result.trajectories.map((t) => t.points[t.points.length - 1]?.[2] ?? 0),
    ...data.requests.map((r) => r.tw_end_s),
  );
  const [t, setT] = useState(data.t_start_s);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(120);
  const tRef = useRef(t);
  tRef.current = t;

  const vehicles = useRef<L.Marker[]>([]);
  const stops = useRef<L.CircleMarker[]>([]);
  const events = useRef<{ line: L.Polyline; from: number; until: number }[]>([]);

  // Static drawing: trails, depot, stops, disruptions, vehicle markers.
  useEffect(() => {
    ctrl.clearRoutes();
    ctrl.clearLayer("fleet");
    const g = ctrl.layer("fleet");
    result.trajectories.forEach((tr, i) =>
      L.polyline(
        tr.points.map(([lon, lat]) => [lat, lon] as [number, number]),
        { color: vehicleColor(i), weight: 3, opacity: 0.45, interactive: false },
      ).addTo(g),
    );
    const depot = data.vehicles[0];
    if (depot) L.marker([depot.lat, depot.lon], { icon: pinIcon("D", "#212529") }).bindTooltip("depot").addTo(g);

    stops.current = data.requests.map((r) =>
      L.circleMarker([r.lat, r.lon], { radius: 7, color: "#fff", weight: 2, fillColor: STATE_COLOR.future, fillOpacity: 1 })
        .bindTooltip(
          `request ${r.id}${r.release_s > data.t_start_s ? ` (arrives ${clock(r.release_s)})` : ""} · window ${clock(r.tw_start_s)}–${clock(r.tw_end_s)} · demand ${r.demand}`,
        )
        .addTo(g),
    );
    events.current = data.events.flatMap((e) =>
      e.lines.map((coords) => {
        const line = L.polyline(
          coords.map(([lon, lat]) => [lat, lon] as [number, number]),
          { color: e.type === "closure" ? "#212529" : "#e8590c", weight: 8, opacity: 0, dashArray: "2 7", interactive: false },
        )
          .bindTooltip(`${e.label} · ${clock(e.time_s)}–${clock(e.until_s)}`, { sticky: true })
          .addTo(g);
        return { line, from: e.time_s, until: e.until_s };
      }),
    );
    vehicles.current = result.trajectories.map((tr, i) => {
      const [lat, lon] = positionAt(tr.points, data.t_start_s);
      return L.marker([lat, lon], { icon: pinIcon(String(tr.vehicle + 1), vehicleColor(i)), zIndexOffset: 1000 }).addTo(g);
    });
    const all = [
      ...data.requests.map((r) => [r.lat, r.lon] as [number, number]),
      ...result.trajectories.flatMap((tr) => tr.points.map(([lon, lat]) => [lat, lon] as [number, number])),
    ];
    if (all.length) ctrl.map.fitBounds(L.latLngBounds(all).pad(0.1));
    setT(data.t_start_s);
    setPlaying(false);
    return () => ctrl.clearLayer("fleet");
  }, [ctrl, data, result]);

  // Time-dependent styling.
  useEffect(() => {
    result.trajectories.forEach((tr, i) => {
      const [lat, lon] = positionAt(tr.points, t);
      vehicles.current[i]?.setLatLng([lat, lon]);
    });
    data.requests.forEach((r, i) => {
      const o = result.outcomes[i];
      let state: keyof typeof STATE_COLOR = "future";
      if (t >= r.release_s) {
        if (o?.served && o.arrival_s <= t) state = o.on_time ? "ontime" : "late";
        else if (!o?.served && t > r.tw_end_s) state = "missed";
        else state = t > r.tw_end_s ? "overdue" : "waiting";
      }
      stops.current[i]?.setStyle({ fillColor: STATE_COLOR[state], radius: state === "future" ? 4 : 7, fillOpacity: state === "future" ? 0.6 : 1 });
    });
    for (const e of events.current) e.line.setStyle({ opacity: t >= e.from && t < e.until ? 0.9 : 0 });
  }, [t, data, result]);

  // Playback loop.
  useEffect(() => {
    if (!playing) return;
    let raf = 0;
    let last = performance.now();
    const step = (now: number) => {
      const dt = (now - last) / 1000;
      last = now;
      const next = Math.min(tEnd, tRef.current + dt * speed);
      setT(next);
      if (next >= tEnd) setPlaying(false);
      else raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  }, [playing, speed, tEnd]);

  const served = result.outcomes.filter((o) => o.served && o.arrival_s <= t).length;
  const late = result.outcomes.filter((o) => o.served && o.arrival_s <= t && !o.on_time).length;
  const log = result.log.filter((l) => l.kind !== "initial");

  return (
    <div className="player">
      <div className="player-bar">
        <button onClick={() => (t >= tEnd ? (setT(data.t_start_s), setPlaying(true)) : setPlaying(!playing))}>
          {playing ? "Pause" : t >= tEnd ? "Replay" : "Play"}
        </button>
        <strong className="clock">{clock(t)}</strong>
        <input
          type="range"
          min={data.t_start_s}
          max={tEnd}
          value={t}
          onChange={(e) => {
            setPlaying(false);
            setT(Number(e.target.value));
          }}
        />
      </div>
      <label className="field">
        <span>Replay speed: {speed}× real time</span>
        <input type="range" min={30} max={1200} step={30} value={speed} onChange={(e) => setSpeed(Number(e.target.value))} />
      </label>
      <p className="muted">
        Served {served}/{data.requests.length} so far ({late} late).{" "}
        <span className="dot" style={{ background: STATE_COLOR.waiting }} /> waiting <span className="dot" style={{ background: STATE_COLOR.ontime }} /> on time{" "}
        <span className="dot" style={{ background: STATE_COLOR.late }} /> late <span className="dot" style={{ background: STATE_COLOR.missed }} /> not served{" "}
        <span className="dot" style={{ background: STATE_COLOR.future }} /> not yet known. Dotted black/orange roads are closures/accidents.
      </p>
      {log.length > 0 && (
        <details>
          <summary>
            Replanning log for <strong style={{ color: STRATEGY_COLOR[result.strategy] }}>{result.strategy}</strong> ({log.length} events)
          </summary>
          <table className="metrics small">
            <thead>
              <tr>
                <th>Time</th>
                <th>Event</th>
                <th>Vehicles</th>
                <th>Latency</th>
                <th>Disruption</th>
              </tr>
            </thead>
            <tbody>
              {log.map((l, i) => (
                <tr key={i} className={t >= l.time ? "" : "future-row"}>
                  <td>{clock(l.time)}</td>
                  <td>{l.label}</td>
                  <td>{l.replanned ? `${l.vehicles}${l.escalated ? " (wide)" : ""}` : "—"}</td>
                  <td>{l.replanned ? ms(l.latency_ms) : "no change"}</td>
                  <td>{l.replanned ? l.disruption : "—"}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </details>
      )}
    </div>
  );
}

import { useState } from "react";
import type { GraphMetadata, GraphSummary } from "../api/types";
import type { AreaPlan } from "../geo";
import { int } from "../format";

const RADII = [1000, 2000, 3000, 5000, 8000];

interface Props {
  meta: GraphMetadata | null;
  graphs: GraphSummary[];
  busy: string | null;
  radius: number;
  onRadius: (r: number) => void;
  onLoadPlace: (place: string) => void;
  onLoadMapCentre: () => void;
  onSelect: (name: string) => void;
}

/** Shows which road map is loaded and lets the user switch or download another area. */
export function AreaPanel(p: Props) {
  const [place, setPlace] = useState("");
  const radii = RADII.filter((r) => r <= (p.meta?.max_area_radius_m ?? 8000));
  const disabled = p.busy !== null;

  return (
    <details className="area" open={!p.meta}>
      <summary>
        Map area: <code>{p.meta?.graph_version ?? "…"}</code>
        {p.meta && <span className="muted"> · {int(p.meta.nodes)} junctions</span>}
      </summary>

      <form
        className="area-form"
        onSubmit={(e) => {
          e.preventDefault();
          if (place.trim()) p.onLoadPlace(place.trim());
        }}
      >
        <label className="field">
          <span>Find a place</span>
          <input
            type="search"
            placeholder="e.g. Bharatpur, Rajasthan"
            value={place}
            onChange={(e) => setPlace(e.target.value)}
            disabled={disabled}
          />
        </label>
        <label className="field">
          <span>Radius</span>
          <select value={p.radius} onChange={(e) => p.onRadius(Number(e.target.value))} disabled={disabled}>
            {radii.map((r) => (
              <option key={r} value={r}>
                {r / 1000} km
              </option>
            ))}
          </select>
        </label>
        <div className="buttons">
          <button type="submit" disabled={disabled || !place.trim()}>
            Load place
          </button>
          <button type="button" className="secondary" onClick={p.onLoadMapCentre} disabled={disabled}>
            Load around map centre
          </button>
        </div>
      </form>

      {p.graphs.length > 1 && (
        <label className="field">
          <span>Saved areas (instant)</span>
          <select
            value={p.graphs.find((g) => g.active)?.name ?? ""}
            onChange={(e) => p.onSelect(e.target.value)}
            disabled={disabled}
          >
            {p.graphs.map((g) => (
              <option key={g.name} value={g.name}>
                {g.place ?? g.name}
                {g.nodes ? ` (${int(g.nodes)} junctions)` : ""}
              </option>
            ))}
          </select>
        </label>
      )}
      <p className="muted">New areas are downloaded from OpenStreetMap (10–60 s) and saved for next time.</p>
    </details>
  );
}

interface NoticeProps {
  plan: AreaPlan;
  graphVersion: string;
  busy: boolean;
  onLoad: () => void;
  onCancel: () => void;
}

/** Shown when the user clicks somewhere the loaded map does not cover. */
export function OutsideNotice({ plan, graphVersion, busy, onLoad, onCancel }: NoticeProps) {
  return (
    <div className="notice">
      <strong>That point is outside the loaded map</strong> (<code>{graphVersion}</code>).
      {plan.fitsAll ? (
        plan.keep.length > 1 ? (
          <p>Load a {plan.radius_m / 1000} km area that covers all your points (dashed circle)?</p>
        ) : (
          <p>Load the roads in a {plan.radius_m / 1000} km radius around it (dashed circle)?</p>
        )
      ) : (
        <p>
          Your points are about {plan.spanKm.toFixed(0)} km apart, more than one map area can hold. Load the roads around
          the new point instead? Your earlier points will be cleared.
        </p>
      )}
      <div className="buttons">
        <button onClick={onLoad} disabled={busy}>
          {busy ? "Loading…" : "Load roads here"}
        </button>
        <button className="secondary" onClick={onCancel} disabled={busy}>
          Cancel
        </button>
      </div>
    </div>
  );
}

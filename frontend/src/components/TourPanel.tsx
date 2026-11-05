import type { Coordinate, TourResponse } from "../api/types";
import { km, minutes, ms } from "../format";

interface Props {
  stops: Coordinate[];
  maxStops: number;
  loading: boolean;
  tour: TourResponse | null;
  onPlan: () => void;
  onUndo: () => void;
  onClear: () => void;
}

export function TourPanel(p: Props) {
  return (
    <section className="panel">
      <p className="instructions">
        Click the map to add stops ({p.stops.length}/{p.maxStops}). The first stop <strong>S</strong> is where the tour starts and ends.
      </p>

      <div className="buttons">
        <button onClick={p.onPlan} disabled={p.stops.length < 2 || p.loading}>
          {p.loading ? "Planning…" : "Plan tour"}
        </button>
        <button className="secondary" onClick={p.onUndo} disabled={p.stops.length === 0}>
          Undo stop
        </button>
        <button className="secondary" onClick={p.onClear}>
          Clear
        </button>
      </div>

      {p.tour && <TourResults tour={p.tour} />}
    </section>
  );
}

function TourResults({ tour }: { tour: TourResponse }) {
  const fmt = (c: number | null) => (tour.weight === "distance" ? km(c) : minutes(c));
  const label = (i: number) => (i === 0 ? "S" : String(i + 1));
  const improvement = tour.nn_cost && tour.cost != null ? (1 - tour.cost / tour.nn_cost) * 100 : null;
  const gap = tour.optimal_cost && tour.cost != null ? (tour.cost / tour.optimal_cost - 1) * 100 : null;

  return (
    <>
      <p className="order">
        {[...tour.order, tour.order[0]].map((s, i) => (
          <span key={i}>
            {i > 0 && " → "}
            <strong>{label(s)}</strong>
          </span>
        ))}
      </p>

      <table className="metrics kv">
        <tbody>
          <tr>
            <th>Nearest neighbour</th>
            <td>{fmt(tour.nn_cost)}</td>
          </tr>
          <tr>
            <th>After 2-opt</th>
            <td>
              {fmt(tour.cost)}
              {improvement != null && improvement > 0.05 && <span className="good"> −{improvement.toFixed(1)}%</span>}
              <span className="muted"> ({tour.two_opt_moves} moves)</span>
            </td>
          </tr>
          <tr>
            <th>Exact optimum</th>
            <td>
              {tour.optimal_cost != null ? (
                <>
                  {fmt(tour.optimal_cost)}
                  {gap != null && <span className="muted"> (gap {gap.toFixed(1)}%)</span>}
                </>
              ) : (
                <span className="muted">only computed for ≤ 12 stops</span>
              )}
            </td>
          </tr>
          <tr>
            <th>Total</th>
            <td>
              {km(tour.distance_m)} · {minutes(tour.time_s)}
            </td>
          </tr>
          <tr>
            <th>Compute</th>
            <td>
              matrix {ms(tour.matrix_ms)} · heuristics {ms(tour.heuristic_ms)}
            </td>
          </tr>
        </tbody>
      </table>

      <details>
        <summary>Legs ({tour.legs.length})</summary>
        <table className="metrics">
          <thead>
            <tr>
              <th>Leg</th>
              <th>Distance</th>
              <th>Time</th>
            </tr>
          </thead>
          <tbody>
            {tour.legs.map((l, i) => (
              <tr key={i}>
                <th>
                  {label(l.from_stop)} → {label(l.to_stop)}
                </th>
                {l.reachable ? (
                  <>
                    <td>{km(l.distance_m)}</td>
                    <td>{minutes(l.time_s)}</td>
                  </>
                ) : (
                  <td colSpan={2} className="unreachable">
                    unreachable
                  </td>
                )}
              </tr>
            ))}
          </tbody>
        </table>
      </details>

      <details>
        <summary>Shortest-path matrix</summary>
        <div className="matrix-wrap">
          <table className="matrix">
            <thead>
              <tr>
                <th>from \ to</th>
                {tour.stops.map((_, j) => (
                  <th key={j}>{label(j)}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {tour.matrix.map((row, i) => (
                <tr key={i}>
                  <th>{label(i)}</th>
                  {row.map((c, j) => (
                    <td key={j} className={c == null ? "unreachable" : undefined}>
                      {i === j ? "·" : c == null ? "∞" : tour.weight === "distance" ? (c / 1000).toFixed(2) : (c / 60).toFixed(1)}
                    </td>
                  ))}
                </tr>
              ))}
            </tbody>
          </table>
          <p className="muted">{tour.weight === "distance" ? "km" : "minutes"}; rows are origins. One-way streets make it asymmetric.</p>
        </div>
      </details>
    </>
  );
}

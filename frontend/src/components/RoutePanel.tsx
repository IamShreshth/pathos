import type { Algorithm, Coordinate, RouteResponse } from "../api/types";
import { ALGO_COLOR, ALGO_HINT, ALGO_LABEL, int, km, minutes, ms } from "../format";

const ALL: Algorithm[] = ["bfs", "dijkstra", "astar"];

interface Props {
  source: Coordinate | null;
  destination: Coordinate | null;
  algorithms: Algorithm[];
  onToggleAlgorithm: (a: Algorithm) => void;
  speed: number;
  onSpeed: (n: number) => void;
  loading: boolean;
  route: RouteResponse | null;
  progress: number[] | null;
  onReplay: () => void;
  onClear: () => void;
}

export function RoutePanel(p: Props) {
  const step = !p.source ? "Click the map to place the source (A)." : !p.destination ? "Now click to place the destination (B)." : null;

  return (
    <section className="panel">
      <fieldset className="algos">
        <legend>Algorithms</legend>
        {ALL.map((a) => (
          <label key={a} className="algo">
            <input type="checkbox" checked={p.algorithms.includes(a)} onChange={() => p.onToggleAlgorithm(a)} />
            <span className="swatch" style={{ background: ALGO_COLOR[a] }} />
            <strong>{ALGO_LABEL[a]}</strong>
            <span className="hint">{ALGO_HINT[a]}</span>
          </label>
        ))}
      </fieldset>

      <label className="field">
        <span>Animation speed: {p.speed} nodes / frame</span>
        <input type="range" min={1} max={300} value={p.speed} onChange={(e) => p.onSpeed(Number(e.target.value))} />
      </label>

      {step && <p className="instructions">{step}</p>}
      {p.loading && <p className="instructions">Searching…</p>}

      {p.route && <Results route={p.route} progress={p.progress} />}

      <div className="buttons">
        <button onClick={p.onReplay} disabled={!p.route}>
          Replay search
        </button>
        <button className="secondary" onClick={p.onClear}>
          Clear
        </button>
      </div>
    </section>
  );
}

function Results({ route, progress }: { route: RouteResponse; progress: number[] | null }) {
  const byAlgo = Object.fromEntries(route.results.map((r) => [r.algorithm, r]));
  const d = byAlgo.dijkstra;
  const a = byAlgo.astar;
  const b = byAlgo.bfs;

  return (
    <>
      <table className="metrics">
        <thead>
          <tr>
            <th />
            <th>Distance</th>
            <th>Time</th>
            <th>Segments</th>
            <th>Expanded</th>
            <th>Runtime</th>
          </tr>
        </thead>
        <tbody>
          {route.results.map((r, i) => {
            const shown = progress?.[i] ?? r.expanded_nodes;
            return (
              <tr key={r.algorithm}>
                <th>
                  <span className="swatch" style={{ background: ALGO_COLOR[r.algorithm] }} />
                  {ALGO_LABEL[r.algorithm]}
                </th>
                {r.status === "success" ? (
                  <>
                    <td>{km(r.distance_m)}</td>
                    <td>{minutes(r.time_s)}</td>
                    <td>{r.segments}</td>
                  </>
                ) : (
                  <td colSpan={3} className="unreachable">
                    unreachable
                  </td>
                )}
                <td title={r.trace_truncated ? "animation trace truncated" : undefined}>
                  {shown < r.expanded_nodes ? `${int(shown)}…` : int(r.expanded_nodes)}
                </td>
                <td>{ms(r.duration_ms)}</td>
              </tr>
            );
          })}
        </tbody>
      </table>

      <ul className="insights">
        {d && a && d.status === "success" && a.status === "success" && d.expanded_nodes > 0 && (
          <li>
            A* found a route of the same cost as Dijkstra while expanding{" "}
            <strong>{Math.round((1 - a.expanded_nodes / d.expanded_nodes) * 100)}% fewer nodes</strong>.
          </li>
        )}
        {b && d && b.status === "success" && d.status === "success" && b.segments < d.segments && (
          <li>
            BFS used {d.segments - b.segments} fewer segments but is{" "}
            {route.results[0].weight === "distance"
              ? `${km(b.distance_m - d.distance_m)} longer`
              : `${minutes(b.time_s - d.time_s)} slower`}{" "}
            — fewest hops is not the shortest route.
          </li>
        )}
        <li className="muted">
          Snapped to road nodes {route.source.node_id} → {route.destination.node_id} ({route.source.snap_distance_m.toFixed(0)} m /{" "}
          {route.destination.snap_distance_m.toFixed(0)} m away).
        </li>
      </ul>
    </>
  );
}

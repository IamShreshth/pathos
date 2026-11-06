interface Props {
  /** Congestion multiplier of an average road, every 30 minutes. */
  profile: number[];
  /** Hour of day marked on the curve. */
  hour: number;
}

/** The synthetic daily congestion curve with the chosen departure time marked. */
export function TrafficChart({ profile, hour }: Props) {
  if (profile.length < 2) return null;
  const w = 380;
  const h = 84;
  const pad = { l: 28, r: 6, t: 6, b: 18 };
  const max = Math.max(1.5, ...profile);
  const x = (i: number) => pad.l + (i / (profile.length - 1)) * (w - pad.l - pad.r);
  const y = (v: number) => pad.t + (1 - (v - 1) / (max - 1)) * (h - pad.t - pad.b);
  const line = profile.map((v, i) => `${x(i).toFixed(1)},${y(v).toFixed(1)}`).join(" ");
  const hx = x((hour / 24) * (profile.length - 1));
  return (
    <svg viewBox={`0 0 ${w} ${h}`} className="chart" role="img" aria-label="Daily congestion curve">
      <polyline points={`${pad.l},${y(1)} ${line} ${x(profile.length - 1)},${y(1)}`} fill="#d0ebff" stroke="none" />
      <polyline points={line} fill="none" stroke="#1c7ed6" strokeWidth={2} />
      <line x1={hx} x2={hx} y1={pad.t} y2={h - pad.b} stroke="#c92a2a" strokeWidth={1.5} strokeDasharray="3 3" />
      {[0, 6, 12, 18, 24].map((t) => (
        <text key={t} x={x((t / 24) * (profile.length - 1))} y={h - 4} fontSize={9} textAnchor="middle" fill="#6c757d">
          {t}h
        </text>
      ))}
      {[1, max].map((v) => (
        <text key={v} x={pad.l - 4} y={y(v) + 3} fontSize={9} textAnchor="end" fill="#6c757d">
          ×{v.toFixed(1)}
        </text>
      ))}
    </svg>
  );
}

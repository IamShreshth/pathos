## What the results say (and what they do not)

Numbers below are from the tables above (10 seeds, two real OSM networks, synthetic traffic). Intervals overlap in
several comparisons; where they do, the text says the difference is **not** established.

**Research question: can selective, uncertainty-aware replanning beat static routing and complete recomputation?**

- **Versus static routing: yes, clearly (E4b).** With the same initial plan, vehicles that never learn about
  disruptions deliver 86 % of requests on time (≈ 5 unserved of 45), insertion-only "greedy repair" 92 %, while both
  replanning strategies reach 96–97 % (≈ 1.0–1.5 unserved). The ordering static < greedy < full ≈ selective holds on both networks.
- **Versus complete recomputation: reliability is the same, the benefit is stability and a moderate saving in planning
  time.** Selective and full are statistically indistinguishable in on-time rate (96.4 % vs 96.2 % on Jaipur, 97.1 % vs 97.3 %
  on the Bharatpur area), unserved and late requests. Selective changes **7–9× fewer committed stops** (≈ 15 vs ≈ 103–133 moved or
  reordered stops per day) and spends **20–23 % less total replanning time** (1.7 s vs 2.1 s; 1.2 s vs 1.5 s). We therefore do **not**
  claim better reliability than full recomputation, and the efficiency gain is modest because the travel-time matrix, which every
  strategy needs, dominates the cost.
- **The knobs behave as designed (ablation).** Raising the disruption penalty β trades plan stability for service:
  β = 0 / 120 / 600 gives ≈ 33 / 15 / 9 changed stops and 97.1 / 96.4 / 95.3 % on time on Jaipur. Restricting a new request
  to the 2 nearest vehicles (k = 2) cuts replanning time by about a third at a small, not significant, loss of on-time rate.

**Where selectivity does *not* pay (E4a).** For a *single* vehicle, a full A\* recomputation costs about 0.1–0.2 ms on these graphs,
so local repair has nothing to save: with a detour allowed to leave the route 5 edges early it is within 1 % of the optimal arrival
time but searches only 5–7 % fewer nodes and is not faster; allowing the detour to start only at the blocked road searches ≈ 18 % fewer
nodes but arrives 26–68 % later after a closure. Selectivity helps at the *fleet* level, by deciding *which vehicles* to re-optimise.

**Time-dependent traffic (E1).** A free-flow router's ETA is 64 % too optimistic at the peaks and 19 % at midday (0 % at night),
but the free-flow *route* is nearly optimal in the time-dependent world: mean regret below 1 % at peak (worst trip 8.7–10.3 %), and a
different route is better for 30–38 % of peak trips. In this model time dependence matters much more for **ETAs and deadlines**
than for **which route is chosen**.

**Uncertainty (E2, E6a).** Random travel times make deadlines hard: a deadline 5 / 15 / 30 % above the expected time is met only about
62–65 / 81–84 / 95–97 % of the time on any route. The reliability-constrained route raises this by at most ≈ 1 point (e.g. 83.6 → 84.3 %) for
≈ 0.1 % more mean time, within the confidence intervals. The reason is structural in our model: much of the randomness is city-wide and hits
every route alike, so route choice cannot remove it. For α ≤ 0.1 and a 10 % deadline no candidate route satisfies the chance
constraint on any of the 20 sampled trips, so here the constraint works mainly as a *feasibility test* ("this deadline cannot be met reliably").

**Reliability by buffering the plan (E6b).** A safety buffer κ on edge times in the *fleet plan* is far more effective: κ = 1 lifts the
worst-5 %-of-days on-time rate from 95.0 / 93.6 % to 99.6 / 98.0 %, and κ = 2 to 100 %, for about 5 % more distance and an unchanged
makespan. κ = 3 is too conservative and starts refusing requests.

**Disruptions on a single route (E3).** A vehicle that keeps its plan through a one-hour closure arrives ≈ 4100 s instead of ≈ 690 s (it
waits for the road to reopen); adaptive replanning limits the loss to 4–9 % and an accident (4× slower road) to ≈ 3 %.

**Coordination (E5).** One vehicle serves about half of the requests on time, two vehicles 86 %, three or more all of them; beyond
three the optimiser simply leaves vehicles idle (cost flat at ≈ 11 000), so extra vehicles do not add distance.

**Scalability (E7).** Queries are linear in graph size (0.15–0.25 ms on the 4–7 thousand node maps, 1.9 ms at 46 thousand nodes);
time dependence adds only a few percent, but the free-flow A\* heuristic is weaker in a congested network (A\* expands 29–33 % fewer nodes than
Dijkstra here against 50–56 % statically). Planning cost splits into the travel-time matrix (linear in graph size and in
nodes × buckets: 0.15 s for 41 nodes on Jaipur, 1.1 s on a 46 thousand node grid) and ALNS (0.05–0.4 s for 40–80 requests, independent
of the graph). Memory stays below 20 MB. Larger graphs than the 46 thousand node synthetic grid were not measured.

**Solution quality (gap).** ALNS with 400 iterations is on average 1.3–1.5 % from the brute-force optimum on 7-request instances
(worst 3.7–8.0 %); with 1 200 iterations on 40 requests it is within 0.5–0.7 % of a 12 000-iteration run, whereas regret insertion
alone is 28–32 % worse. The "reference" is a long ALNS run, not a proven optimum.

### Threats to validity

- **Synthetic traffic and noise.** Conclusions on route choice (E1, E2, E6a) depend on how the congestion pattern and the
  correlation of noise are modelled; a real feed could differ. The comparisons between *methods* are the point of the study, not the absolute times.
- **Scenario generator.** Disruptions are placed on busy corridors from the depot and announced when they start; windows are
  25 minutes wide on average; 45 requests, 3 vehicles. Other regimes (looser windows, bigger fleets, more disruptions) were not swept.
- **Fixed effort.** Every ALNS call uses `30 × requests` iterations, so full recomputation gets more iterations than selective
  (larger problem). That is part of what "full" costs, but a time-matched comparison could narrow the quality gap in either direction.
- **Sample size.** 10 seeds per configuration on two ≈ 5 000-node maps; 95 % intervals are shown, and differences inside them are not claimed.
- **Timings** are single-threaded wall-clock on one machine.

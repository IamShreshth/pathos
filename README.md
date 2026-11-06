# pathos 2.0

*Fewer wrong turns, more right answers. Now adaptive to traffic, uncertainty and disruptions.*

**Course:** CSL 2120 Data Structures & Algorithms

pathos started as an interactive route and tour visualizer on real OpenStreetMap roads (BFS,
Dijkstra, A\*, nearest neighbour + 2-opt). **Pathos 2.0** is the harder follow-up: an *adaptive,
uncertainty-aware urban mobility optimiser* that keeps producing good multi-vehicle routes when
traffic changes during the day, travel times are random, and roads close unexpectedly.

> **Research question.** Can *selective*, uncertainty-aware replanning achieve better operational
> reliability and computational efficiency than static routing and complete recomputation when an
> urban road network is hit by unexpected disruptions?

The project answers it by building the system end to end and measuring it (experiments E1-E7,
[`docs/RESULTS.md`](docs/RESULTS.md)). It does **not** claim a new algorithm: time-dependent routing,
ALNS and incremental repair are established techniques; the contribution is the *selective
replanning strategy*, a fair experimental comparison, and an honest account of where it helps and
where it does not.

## What is in the box

| Capability | How |
|---|---|
| **Time-dependent roads** | Edge travel time depends on the departure time (synthetic morning/evening peaks, heaviest in the centre); FIFO-correct earliest-arrival Dijkstra and A\* |
| **Uncertainty** | Correlated lognormal travel-time scenarios; routes compared on expected time, robust (mean + z·σ) and **reliability-constrained** `P(arrive by D) ≥ 1 − α` |
| **Disruptions** | Road closures (vehicles wait or detour) and accidents (slowdowns), announced while vehicles are driving |
| **Incremental replanning** | Local route repair for one vehicle; **selective** re-optimisation of only the affected vehicles for a fleet |
| **Fleet routing** | VRPTW with capacities, service times, shift ends and late-arriving requests: cheapest insertion + **Adaptive Large Neighbourhood Search** (random / worst / Shaw / route removal, greedy / regret-2 repair, simulated-annealing acceptance) |
| **Simulation and evaluation** | Rolling-horizon simulator that executes plans on a sampled "real" day; four strategies; experiments E1-E7, optimality gap vs brute force and a long-run reference |
| **Interactive app** | Leaflet map with five tabs: Routes, Tour, Reliability, Disruption, Fleet (animated replay, replanning log) |

Everything algorithmic is **C++17** (`engine/`); FastAPI (`backend/`) validates, snaps points to
roads and formats results; React + Vite + Leaflet (`frontend/`) displays them.

> **Traffic is synthetic.** No measured traffic feed is used. The daily curve, its spatial pattern
> and the noise are modelled and clearly labelled as such in the app and in the results. The
> experiments test *how the methods compare*, not real-world absolute times. Details and other
> limitations: [Architecture.md, section 12](Architecture.md).

See **[README_1.md](README_1.md)** for the full user guide (setup, every tab, loading other cities,
troubleshooting) and **[Architecture.md](Architecture.md)** for the design, the mathematical model and
the algorithms.

## Quick start

Requirements: a C++17 compiler, Python 3.10-3.12, Node.js 18+ (CMake optional).

```bash
./run.sh
```

Then open http://localhost:8000. The script builds and tests the engine, creates `.venv`, makes
sure a graph exists, builds the frontend and starts the server.

## Step by step

```bash
# 1. C++ engine (+ unit tests)
engine/build.sh
engine/build/pathos_tests

# 2. Python environment
python3 -m venv .venv
.venv/bin/pip install -r backend/requirements.txt

# 3. Road graph (Jaipur and a Bharatpur area are already in data/processed/)
.venv/bin/python scripts/prepare_graph.py                       # 3 km around central Jaipur
.venv/bin/python scripts/prepare_graph.py --place "Connaught Place, New Delhi, India" --name delhi-v1
.venv/bin/python scripts/make_demo_graph.py                     # offline synthetic graph

# 4. API server (also serves frontend/dist if built)
.venv/bin/uvicorn app.main:app --app-dir backend --port 8000
#    choose another graph:  PATHOS_GRAPH=data/processed/delhi-v1.graph ...

# 5. Frontend
cd frontend && npm install
npm run dev        # http://localhost:5173 with hot reload (proxies /api to :8000)
npm run build      # or build once and use http://localhost:8000
```

API docs: http://localhost:8000/docs

## Using the app

- **Routes / Tour:** the original BFS vs Dijkstra vs A\* race and multi-stop tour (NN + 2-opt).
- **Reliability:** click A and B, drag the departure time and watch the synthetic congestion map
  change. Free-flow, expected-time, robust and reliability-constrained routes are drawn and
  tabulated with expected time, 95th-percentile time and the probability of meeting the deadline.
- **Disruption:** a closure or accident appears ahead of a driving vehicle. Compare *keep the plan*,
  *full recomputation* and *selective local repair* (trip time, nodes searched, compute time).
- **Fleet:** a random scenario, or a depot and stops you place. Four strategies (static, greedy
  repair, full recomputation, selective) run on the same day; click a row of the comparison table to
  replay it: vehicles drive, requests turn green / orange / red, closures appear and expire.

## Experiments

```bash
python3 experiments/run_experiments.py --quick        # a few minutes, 3 seeds
python3 experiments/run_experiments.py --seeds 10     # the full study (about 8 minutes)
python3 experiments/run_experiments.py --only E4      # one experiment
python3 experiments/run_experiments.py --render-only  # rebuild docs/RESULTS.md from saved raw results
```

| Id | Question |
|---|---|
| E1 | static vs time-dependent traffic: how much do changing edge costs alter route quality? |
| E2 | deterministic vs stochastic travel times: how does uncertainty affect reliability? |
| E3 | normal network vs disruptions: how robust are routes to closures and incidents? |
| E4 | full recomputation vs incremental / selective replanning: latency and schedule disruption |
| E5 | single vs multiple vehicles: effect of coordination on system-wide cost |
| E6 | expected-time vs reliability-aware routing: the efficiency-reliability trade-off |
| E7 | scalability with graph size and number of requests |
| gap | ALNS vs brute force (small) and vs a much longer run (larger) |

Raw rows are saved as CSV and JSON in `experiments/results/`; the report is
[`docs/RESULTS.md`](docs/RESULTS.md). All quality numbers are seeded and reproducible; runtimes depend
on the machine.

### Headline results

(10 seeds, two real OSM networks, **synthetic** traffic; full tables and caveats in [`docs/RESULTS.md`](docs/RESULTS.md))

| Question | Finding |
|---|---|
| Selective vs **static** routing (E4) | On-time requests 96-97 % vs 86 % (greedy path repair: 92 %) when roads close and requests arrive late |
| Selective vs **complete recomputation** (E4) | **Same reliability** (differences within the confidence intervals), **7-9× fewer changed stops**, about 20 % less replanning time. Not better reliability |
| Single vehicle, local repair vs full A\* (E4a) | No speed-up: A\* is already ≈ 0.2 ms; repair is within 1 % of optimal arrival but searches only 5-7 % fewer nodes |
| Static vs time-dependent traffic (E1) | Free-flow ETAs are 64 % too optimistic at the peaks, yet the free-flow *route* loses < 1 % on average |
| Reliability-aware routing (E2, E6) | Marginal at route level (≤ 1 point of on-time probability); a safety buffer κ = 1-2 in the fleet plan lifts the worst-5 %-of-days on-time rate from 94-95 % to 98-100 % for ≈ 5 % more distance |
| Coordination (E5) | 1 vehicle: ≈ 50 % on time, 2: 86 %, ≥ 3: all |
| Scale (E7) | Queries 0.15-0.25 ms on 4-7 k nodes, linear in size; ALNS 0.05-0.4 s for 40-80 requests; < 20 MB |
| Solution quality | ALNS within ≈ 1.5 % of brute force on tiny instances, ≈ 0.5-0.7 % of a 10× longer run on 40 requests (regret insertion alone: 28-32 % worse) |


## Tests

```bash
engine/build/pathos_tests                 # C++: algorithms, traffic, replanning, fleet (~736,000 checks)
cd backend && ../.venv/bin/pytest         # API + engine integration (21 tests)
cd frontend && npm run build              # type-check + build
```

The C++ tests include FIFO checks, time-dependent search against a reference relaxation, closure
detours, local repair bounds (never worse than waiting, never better than full recomputation), ALNS
against brute force on tiny instances, and simulator invariants. They also pass under AddressSanitizer
and UBSan.

## API

Interactive documentation at http://localhost:8000/docs. New in 2.0:

| Endpoint | Purpose |
|---|---|
| `GET /api/v1/traffic?hour=8` | daily congestion curve and the congested roads at that hour |
| `POST /api/v1/mobility/routes` | free-flow / expected / robust / reliability-constrained routes with `P(on time)` |
| `POST /api/v1/mobility/replan` | keep vs full recomputation vs selective repair after a closure or accident |
| `POST /api/v1/fleet/simulate` | multi-vehicle day with late requests and disruptions under several strategies |

The 1.0 endpoints (`/routes`, `/routes/compare`, `/tours`, `/graphs/...`) are unchanged.

## Repository layout

```
pathos/
├── engine/          C++17 engine + unit tests (traffic.*, tdroute.*, fleet.*, sim.*, commands.*, bench.*)
├── backend/         FastAPI app and integration tests
├── frontend/        React + Vite + Leaflet UI
├── experiments/     run_experiments.py, raw results, commentary
├── scripts/         OSMnx map preparation, demo graph, graph file format
├── data/processed/  road graphs (Jaipur, Bharatpur area, synthetic demo)
├── docs/            proposal, RESULTS.md (generated)
├── Architecture.md  design, model, algorithms, limitations
├── README_1.md      user guide
└── run.sh           build everything and start the server
```

Map data © OpenStreetMap contributors (ODbL).

## Team Members
* Shreshth Dhimole - B25EE1037
* Madhav Saxena - B25EE1019
* Moksh Sharma - B25EC1025

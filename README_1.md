# pathos: user guide

*Fewer wrong turns, more right answers.*

pathos shows how route-finding algorithms work on **real roads** from OpenStreetMap. You can:

- **Compare routes:** click two points and watch **BFS**, **Dijkstra** and **A\*** search the road
  network as an animated race, then compare their route length, travel time, nodes explored and
  running time.
- **Plan a multi-stop tour:** click several stops and get a short round trip that visits all of
  them (nearest neighbour + 2-opt), with the exact best tour for comparison when there are 12 stops
  or fewer.
- **Use any place:** start with central Jaipur, then load the roads of any town or area with one
  click.

**Pathos 2.0** adds three more tabs on top of those:

- **Reliability:** see how a *synthetic* daily traffic pattern changes travel times, and compare
  routes by how likely they are to arrive before a deadline.
- **Disruption:** close a road (or add an accident) ahead of a driving vehicle and compare keeping
  the plan, recomputing everything, and repairing only the affected stretch.
- **Fleet:** simulate several vehicles serving requests with time windows while new requests arrive
  and roads close, and compare four replanning strategies in an animated replay.

---

## 1. What you need

| Tool | Version | Check with |
|---|---|---|
| C++ compiler | C++17 (clang or g++) | `c++ --version` |
| Python | **3.10 – 3.12** (OSMnx does not support newer versions yet) | `python3.12 --version` |
| Node.js + npm | 18 or newer | `node --version` |
| Internet | only for loading new map areas and map tiles | |
| CMake | optional; plain `c++` is used if it is missing | `cmake --version` |

On macOS, install anything missing with Homebrew:

```bash
xcode-select --install          # C++ compiler
brew install python@3.12 node
```

On Ubuntu/Debian: `sudo apt install build-essential python3.12 python3.12-venv nodejs npm`.
On Windows, use WSL (Ubuntu) and follow the Linux steps.

---

## 2. Run it (one command)

```bash
cd pathos
PYTHON=python3.12 ./run.sh
```

Then open **http://localhost:8000** in your browser.

`run.sh` builds and tests the C++ engine, creates a Python environment in `.venv`, installs the
Python packages, makes sure a road map exists, builds the web app and starts the server. The first
run takes a few minutes; later runs take a few seconds.

Press **Ctrl + C** in the terminal to stop the server.

> **Important:** after updating the code, stop the old server (Ctrl + C) and start it again. A
> server that is still running keeps using the old code.

---

## 3. Run it step by step

Use this if `run.sh` fails, or to understand what it does. Run everything from the `pathos` folder.

```bash
# 1. Build the C++ routing engine and run its tests
engine/build.sh
engine/build/pathos_tests                 # should print "... checks passed"

# 2. Create the Python environment
python3.12 -m venv .venv
.venv/bin/pip install -r backend/requirements.txt

# 3. Build the web app
cd frontend
npm install
npm run build
cd ..

# 4. Start the server
.venv/bin/uvicorn app.main:app --app-dir backend --port 8000
```

Open **http://localhost:8000**.

### Development mode (for editing the frontend)

Run the server and the frontend dev server in two terminals:

```bash
# terminal 1
.venv/bin/uvicorn app.main:app --app-dir backend --port 8000 --reload

# terminal 2
cd frontend && npm run dev
```

Open **http://localhost:5173**. The page reloads every time you save a frontend file.

---

## 4. Using the app

### The map area

The **dashed rectangle** on the map is the area whose roads are loaded. The default map is a 3 km
radius around central Jaipur. You can only route between points inside it. The **Map area** panel
at the top of the sidebar shows which area is loaded.

**To route somewhere else**, use any of these:

1. **Click outside the dashed box.** pathos shows a yellow notice and a dashed circle around the
   area it will download. Click **Load roads here**. Your points are kept and the route runs
   automatically.
2. **Search a place:** open *Map area*, type a place such as `Bharatpur, Rajasthan`, choose a
   radius and click **Load place**.
3. **Load around map centre:** pan the map to where you want, then click **Load around map centre**.
4. **Saved areas:** every downloaded area is saved in `data/processed/` and listed under
   *Saved areas*, so switching back to it is instant.

A new area takes **10–60 seconds** to download, depending on the OpenStreetMap servers. One area
can be at most **8 km** in radius. If your two points are further apart than that, pathos loads
the area around the newest point and asks you to place the other point again.

The tabs are **Routes**, **Tour**, **Reliability**, **Disruption** and **Fleet**. The first two are
the original pathos; the last three are described below.

### Compare routes (tab *Routes*)

1. Choose **Shortest distance** or **Fastest** (estimated travel time from speed limits; this is
   not live traffic).
2. Click the map for the **source (A)**, then the **destination (B)**.
3. Watch the race: each colour shows the roads one algorithm explored. A\* usually finishes first.
4. Read the table:

| Column | Meaning |
|---|---|
| Distance / Time | length and estimated travel time of the route found |
| Segments | number of road pieces in the route (BFS minimises this) |
| Expanded | nodes the algorithm explored before finishing (smaller means more efficient) |
| Runtime | time spent inside the C++ engine |

- **BFS** (orange) finds the route with the fewest road segments, which is often *longer*.
- **Dijkstra** (blue) and **A\*** (green) both find the shortest route; A\* explores far fewer
  nodes because it heads toward the destination.

Use the **speed slider** and **Replay search** to watch again, and **Clear** to start over.
Clicking a third time starts a new route.

### Multi-stop tour (tab *Tour*)

1. Open the **Tour** tab.
2. Click up to 25 stops. The first stop **S** is where the tour starts and ends.
3. Click **Plan tour**.

You will see the visiting order (for example `S → 4 → 2 → 3 → S`), the tour length from nearest
neighbour and after 2-opt, the **exact optimum** and the gap (12 stops or fewer), each leg, and
the shortest-path matrix. The matrix is not symmetric because of one-way streets.

Use **Undo stop** to remove the last stop and **Clear** to remove all stops.

### Reliability (tab *Reliability*)

The traffic in Pathos 2.0 is **synthetic**: a daily curve with a morning peak around 08:00 and an
evening peak around 18:00, heaviest near the centre, plus random variation that is correlated across
roads (a bad day is bad everywhere). It is not measured data.

1. Click the **start (A)** and the **destination (B)**.
2. Move the **Departure** slider. The map shows slow / heavy / severe roads at that time and the
   curve below it marks the chosen hour.
3. Set the **deadline** (a percentage above the expected trip time) and the **tolerated miss
   probability α**.

Four routes are drawn (the widest is at the bottom):

| Route | How it is chosen |
|---|---|
| Free-flow (grey, dashed) | shortest on the *empty* network, what a static router would pick |
| Expected time (blue) | fastest on average in the traffic at your departure time |
| Robust (orange) | minimises mean + z·σ for every road (cautious) |
| Reliability-constrained (green) | the fastest route whose chance of arriving by the deadline is at least 1 − α |

The table lists expected time, 95th-percentile time, **P(on time)** and distance for each route
(identical routes share a row). If no route reaches 1 − α the panel says so; loosen the deadline or α.
Probabilities come from 300 sampled traffic scenarios evaluated for every candidate route.

### Disruption (tab *Disruption*)

1. Click **A** and **B**.
2. Choose **Road closure** or **Accident** (four times slower), how long it lasts, how many road
   segments ahead of the vehicle it hits, and the departure time.

The vehicle drives for a minute, then the disruption is announced. Three responses are compared:

| Strategy | What it does |
|---|---|
| Keep the plan | keep driving; wait at the closure until it reopens |
| Full recomputation | search a new route from the vehicle's position |
| Selective local repair | find a detour only around the affected stretch and rejoin the old route |

The table shows trip time, delay, nodes searched and compute time. The grey route is the original
plan, red/black dotted segments are the disrupted roads, blue is the recomputed route and green the
repaired one. Local repair is never worse than waiting and may be slightly longer than full
recomputation; it searches fewer nodes.

### Fleet (tab *Fleet*)

A fleet of vehicles leaves one depot at 07:30 to serve **pick-up requests**. Every request has a
**time window**, a demand (vehicles have a capacity), and a service time. Some requests arrive during
the day, roads close, and accidents slow traffic. Vehicles must be back at the depot before their
shift ends.

1. Choose **Random scenario** (set vehicles, requests, window width, share of late requests, closures,
   accidents and a seed) or **Place depot & stops** (click the depot, then the stops; windows and
   disruptions are generated).
2. Tick the strategies to compare:

| Strategy | Reaction to new requests | Reaction to disruptions |
|---|---|---|
| Static | cheapest insertion | none (vehicles wait at closures) |
| Greedy repair | cheapest insertion | new paths for the affected vehicles, same stop order |
| Full recompute | re-solve everything from scratch | re-solve everything from scratch |
| Selective | re-optimise only the few vehicles involved (warm start) | re-optimise only the affected vehicles |

3. Open **Advanced** for the reliability buffer κ (plan with safety margin on travel times), the
   disruption penalty β (how much changing existing plans costs), the realised random day, and a
   Monte Carlo estimate of the initial plan's reliability.
4. Press **Run simulation** (a few seconds).

The comparison table shows the on-time percentage, requests unserved or late, distance, total
replanning time, **plan changes** (stops moved to another vehicle or reordered) and cost; the best
value in each column is green. **Click a row to replay it**: vehicles drive along the roads, request
dots turn blue (waiting), green (served on time), orange (served late) or red (not served), and
closures appear and expire. The replanning log lists every event with how long replanning took.

---

## 5. Loading areas from the command line

You can prepare maps without the web app:

```bash
# 3 km around a place
.venv/bin/python scripts/prepare_graph.py --place "Bharatpur, Rajasthan, India" --name bharatpur

# 5 km around a coordinate
.venv/bin/python scripts/prepare_graph.py --point 27.21063,77.49784 --dist 5000 --name my-area

# offline demo map (synthetic grid, not real roads)
.venv/bin/python scripts/make_demo_graph.py
```

Start the server with a particular map:

```bash
PATHOS_GRAPH=data/processed/bharatpur.graph .venv/bin/uvicorn app.main:app --app-dir backend --port 8000
```

---

## 6. Troubleshooting

| Problem | Fix |
|---|---|
| *"The source (27.21063, 77.49784) is about 169 km outside the loaded map area ('jaipur-v1')"* | The point is not in the loaded map. Click **Load roads here**, or load the area from the *Map area* panel. If you see the older message *"…is more than 750 m from any road in graph 'jaipur-v1'"* with no button, your server is running old code; restart it. |
| *"…more than 750 m from any road"* for a point **inside** the dashed box | You clicked a park, field or river. Click closer to a road. |
| *"could not download map data from OpenStreetMap"* | The OpenStreetMap servers were slow or busy. Wait a minute and try again; the current map keeps working. |
| *"another map area is already being downloaded"* | Wait for the current download to finish. |
| *"could not find a place called …"* | Add the state or country, e.g. `Bharatpur, Rajasthan, India`. |
| `address already in use` when starting | A pathos server is already running. Stop it with Ctrl + C in its terminal, or find it with `lsof -i :8000`, or use another port: `PORT=8001 ./run.sh`. |
| *"Cannot reach the pathos server"* in the page | The server is not running; start it (section 2). |
| *"engine binary not found"* | Run `engine/build.sh`. |
| A fleet simulation is slow or reports *"engine did not answer within …"* | Use fewer requests or vehicles, or raise `PATHOS_FLEET_TIMEOUT_S`. About 40 requests and 3–5 vehicles take a few seconds. Roughly: bigger maps and more requests make the travel-time tables the slow part. |
| *Reliability* says no route reaches 1 − α | Normal: on a congested trip with a tight deadline no route is reliable enough. Raise the deadline slack or α. |
| *Fleet* shows many "not served" requests | The windows are too tight for the fleet. Add vehicles, widen the window or reduce the requests. |
| `npm` / `node` fails with `Library not loaded: …libsimdjson…` (macOS) | Homebrew updated a library under Node. Run `brew reinstall node`. |
| `pip install` fails building OSMnx / shapely | Use Python 3.12: `rm -rf .venv && PYTHON=python3.12 ./run.sh`. |
| The page shows old behaviour after updating | Rebuild the frontend (`cd frontend && npm run build`) and restart the server. |
| The map background is blank | Map tiles need internet access; routing still works. |

---

## 7. Configuration

Set these environment variables before starting the server:

| Variable | Default | Meaning |
|---|---|---|
| `PATHOS_GRAPH` | `data/processed/jaipur-v1.graph` | map loaded at start-up |
| `PATHOS_MAX_AREA_RADIUS_M` | `8000` | largest area that can be downloaded |
| `PATHOS_MAX_TOUR_STOPS` | `25` | stop limit for tours |
| `PATHOS_MAX_SNAP_M` | `750` | how far a click may be from the nearest road |
| `PATHOS_SNAP_MAIN_COMPONENT` | `1` | only snap to roads that can reach each other (avoids one-way dead ends) |
| `PATHOS_FLEET_TIMEOUT_S` | `180` | how long a fleet simulation may run |
| `PATHOS_MAX_FLEET_REQUESTS` | `120` | request limit for fleet simulations |
| `PATHOS_MAX_FLEET_VEHICLES` | `20` | vehicle limit for fleet simulations |
| `PORT` (run.sh only) | `8000` | server port |

Example: `PATHOS_MAX_TOUR_STOPS=40 PORT=8001 ./run.sh`

---

## 8. Running the tests

```bash
engine/build/pathos_tests                 # C++ algorithms, traffic, replanning, fleet (~736,000 checks)
cd backend && ../.venv/bin/pytest         # API + engine integration (21 tests)
cd frontend && npm run build              # TypeScript type-check + build
python3 experiments/run_experiments.py --quick   # experiments E1-E7 (a few minutes), see docs/RESULTS.md
```

---

## 9. API (for scripts)

Interactive documentation: **http://localhost:8000/docs**

```bash
# Load the roads around a point (or use {"place": "Bharatpur, India"})
curl -X POST localhost:8000/api/v1/graphs/area -H 'content-type: application/json' \
  -d '{"center": {"lat": 27.21063, "lon": 77.49784}, "radius_m": 3000}'

# Compare BFS, Dijkstra and A*
curl -X POST localhost:8000/api/v1/routes/compare -H 'content-type: application/json' \
  -d '{"source": {"lat": 27.21063, "lon": 77.49784}, "destination": {"lat": 27.218, "lon": 77.49},
       "weight": "distance", "record_trace": false}'

# Plan a tour
curl -X POST localhost:8000/api/v1/tours -H 'content-type: application/json' \
  -d '{"stops": [{"lat": 27.2106, "lon": 77.4978}, {"lat": 27.218, "lon": 77.49}, {"lat": 27.2, "lon": 77.51}]}'

# Pathos 2.0: congestion at 08:00, reliability-aware routes, a disruption, a fleet day
curl 'localhost:8000/api/v1/traffic?hour=8'
curl -X POST localhost:8000/api/v1/mobility/routes -H 'content-type: application/json' \
  -d '{"source": {"lat": 26.90, "lon": 75.77}, "destination": {"lat": 26.93, "lon": 75.81},
       "depart_hour": 8, "slack": 1.15, "alpha": 0.1}'
curl -X POST localhost:8000/api/v1/mobility/replan -H 'content-type: application/json' \
  -d '{"source": {"lat": 26.90, "lon": 75.77}, "destination": {"lat": 26.93, "lon": 75.81},
       "type": "closure", "duration_min": 60}'
curl -X POST localhost:8000/api/v1/fleet/simulate -H 'content-type: application/json' \
  -d '{"generate": {"seed": 1, "vehicles": 3, "requests": 40}, "strategies": ["static", "selective"],
       "mc_scenarios": 100}'

# List / switch saved areas
curl localhost:8000/api/v1/graphs
curl -X POST localhost:8000/api/v1/graphs/select -H 'content-type: application/json' -d '{"name": "jaipur-v1"}'
```

---

## 10. Project layout

```
pathos/
├── run.sh            one-command build + start
├── engine/           C++ engine: graph, BFS, Dijkstra, A*, TSP, traffic model, time-dependent and
│                     reliability-aware routing, replanning, VRPTW / ALNS, simulator + tests
├── experiments/      run_experiments.py (E1-E7), raw results, commentary
├── docs/             RESULTS.md (generated experiment report)
├── backend/          FastAPI server (snapping, map areas, engine adapter) + tests
├── frontend/         React + Vite + Leaflet web app
├── scripts/          OpenStreetMap download (prepare_graph.py), demo map, file format
├── data/processed/   saved road maps (*.graph + *.meta.json)
├── Architecture.md   how it works inside
└── README_1.md       this guide
```

Map data © OpenStreetMap contributors (ODbL).

**Team:** Shreshth Dhimole (B25EE1037) · Madhav Saxena (B25EE1019) · Moksh Sharma (B25EC1025)

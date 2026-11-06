# pathos

*Fewer wrong turns, more right answers.*

**Course:** CSL 2120 Data Structures & Algorithms

pathos is an interactive routing and Traveling Salesperson Problem visualizer that runs on real
OpenStreetMap roads. Click two points to compare **BFS**, **Dijkstra** and **A\*** (route, running
time, expanded nodes and length) while their searches replay as an animated race. Click several
stops to plan a short round trip using **nearest neighbour + 2-opt**, checked against the exact
optimum.

- **C++17 engine** (`engine/`): adjacency-list graph, BFS, Dijkstra, A*, TSP
- **FastAPI** (`backend/`): validation, coordinate snapping, engine integration
- **React + Vite + Leaflet** (`frontend/`): map, controls, animation
- **OSMnx + Pandas** (`scripts/`): road-network preparation

See **[README_1.md](README_1.md)** for the full user guide (setup, usage, loading other cities,
troubleshooting) and [Architecture.md](Architecture.md) for the design.

## Quick start

Requirements: a C++17 compiler, Python 3.10–3.12, Node.js 18+ (CMake optional).

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

# 3. Road graph (a Jaipur graph is already in data/processed/)
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

## Tests

```bash
engine/build/pathos_tests                 # C++ unit tests
cd backend && ../.venv/bin/pytest         # API + engine integration tests
cd frontend && npm run build              # type-check + build
```

## Using the app

- **Compare routes:** click the source (A), then the destination (B). Pick which algorithms to
  compare and whether to optimise for distance or estimated travel time. The table shows each
  route's length, travel time, number of segments, nodes expanded and engine runtime.
- **Multi-stop tour:** switch tabs, click up to 25 stops (the first is the start and end), then
  press *Plan tour*.

Map data © OpenStreetMap contributors (ODbL).

## Team Members
* Shreshth Dhimole - B25EE1037
* Madhav Saxena - B25EE1019
* Moksh Sharma - B25EC1025

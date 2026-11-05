# pathos
> *Fewer wrong turns, more right answers.*

**Course:** CSL 2120 – Data Structures & Algorithms  
**Team Members:**
- **Shreshth Dhimole** (B25EE1037)
- **Madhav Saxena** (B25EE1019)
- **Moksh Sharma** (B25EC1025)

---

## 1. Problem Statement

Pathfinding algorithms are often demonstrated on idealized 2D grids, but real-world road networks exhibit unequal segment lengths, irregular junctions, speed limits, and one-way restrictions. These synthetic grid examples fail to illustrate how graph algorithms behave when confronted with realistic urban topology. 

We seek to evaluate and compare pathfinding algorithms on real road networks and explore multi-stop tour optimization by planning short paths through user-selected stops.

---

## 2. Proposed Solution

### Abstract
We propose **pathos**, an interactive urban routing and Traveling Salesperson Problem (TSP) visualizer built around OpenStreetMap (OSM) data. Users can select arbitrary locations on a map, pick a routing algorithm, and visualize both the computed route and the frontier expansion/search animation in real time. The goal is to make graph algorithms intuitive and tangible through real road topologies rather than synthetic abstractions.

### Core Modules & Capabilities

- **Road Network Preparation:**
  - Python with **OSMnx** and **Pandas** extracts and cleans OpenStreetMap road data for selected regions.
- **Routing Engine (C++):**
  - Represents the road network efficiently as an adjacency list.
  - Implements **BFS** (minimizing edge/hop count), **Dijkstra** (finding exact shortest path distance), and **A\*** (using geographic/Haversine heuristic distance to guide exploration).
  - Collects and benchmarks metrics: execution time, total explored nodes, and path distance.
- **Multi-Stop Tour Optimization (TSP Heuristics):**
  - For multi-stop route queries, plans an approximate tour visiting every stop and returning to the origin.
  - Computes all-pairs shortest road distances between selected stops.
  - Generates an initial tour using the **Nearest Neighbor** greedy heuristic and refines it via **2-opt** local search.
  - Properly handles directed constraints (one-way streets) and unreachable nodes.
- **Interactive Visualization & Full-Stack Integration:**
  - A **FastAPI** service interfaces the C++ algorithmic engine with the web client.
  - The frontend, built with **React**, **Vite**, and **Leaflet.js** (augmented with **D3.js / Mapbox GL JS** for animations), renders step-by-step frontier expansions and highlights the finalized optimal path.

---

## 3. Technology Stack

| Layer | Technologies | Role |
| :--- | :--- | :--- |
| **Backend & Algorithms** | C++, CMake | Graph representation, pathfinding (BFS, Dijkstra, A*), TSP heuristics (Nearest Neighbor, 2-opt), performance benchmarking |
| **API & Service** | Python, FastAPI, Uvicorn | Bridge between the C++ engine and web client; request validation and serialization |
| **Map Data Processing** | OSMnx, Pandas | Extracting, filtering, and sanitizing OpenStreetMap graph data |
| **Frontend & UI** | React, Vite, npm | Responsive UI controls, user interaction, parameter tuning |
| **Mapping & Animation** | Leaflet.js, D3.js / Mapbox GL JS | Interactive map rendering, node selection, search frontier animations, and route overlay |

---

## 4. Team Responsibilities

| Member | Focus / Role | Primary Responsibilities |
| :---: | :--- | :--- |
| **A** | **Frontend & Visualization** | Interactive map integration, location picker, real-time search animation, responsive UI controls, and frontend end-to-end testing. |
| **B** | **Data Pipeline & API** | OpenStreetMap graph extraction via OSMnx, road data sanitation, FastAPI server development, C++ engine bindings/IPC, and error handling. |
| **C** | **Algorithms & Engine** | C++ graph representation, core pathfinding implementations (BFS, Dijkstra, A*), TSP heuristics (Nearest Neighbor + 2-opt), benchmarking, and unit testing. |

> **Note:** All members participate in system integration testing, performance evaluation, and project documentation.
# Architecture

## Project structure

- `algos/` contains algorithm implementations and their explanatory notes.
- `docs/` contains project documentation and the project PDF.
- `README.md` provides the project and team overview.

## Algorithms

The algorithm collection covers grid pathfinding and travelling-salesperson tour planning:

| Algorithm | Purpose | Status |
| --- | --- | --- |
| BFS | Finds a shortest path in an unweighted graph or grid. | Planned |
| Dijkstra | Finds shortest paths when edges have non-negative weights. | Planned |
| A* | Finds a grid path using accumulated cost and a distance heuristic. | Implemented in `algos/Astar.cpp` |
| TSP (bitmask DP) | Finds a minimum-cost tour using memoized visited-city states. | Implemented in `algos/TSP.cpp` |
| Nearest Neighbor | Builds a TSP tour by repeatedly visiting the closest unvisited city. | Planned |
| 2-opt | Improves a tour by removing crossing or costly edge pairs. | Planned |

The existing A* implementation uses an eight-direction grid with Euclidean distance as its heuristic. The TSP implementation uses a distance matrix and starts and ends its tour at city 0. Planned algorithms are listed here for the architecture only; no implementation files are included for them yet.

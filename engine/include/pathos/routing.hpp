// Point-to-point routing: BFS, Dijkstra and A*.
#pragma once

#include <vector>

#include "pathos/graph.hpp"
#include "pathos/types.hpp"

namespace pathos {

struct SearchOptions {
    Weight weight = Weight::Distance;
    bool record_trace = false;
    std::size_t max_trace_events = 400000;  // protects memory and response size
};

// Fewest road segments (ignores edge lengths). O(V + E).
RouteResult bfs(const Graph& g, NodeId source, NodeId target, const SearchOptions& opt);

// Minimum total weight with a binary-heap priority queue. O((V + E) log V).
RouteResult dijkstra(const Graph& g, NodeId source, NodeId target, const SearchOptions& opt);

// Dijkstra guided by a straight-line (haversine) lower bound: f = g + h.
RouteResult astar(const Graph& g, NodeId source, NodeId target, const SearchOptions& opt);

RouteResult run_route(Algorithm a, const Graph& g, NodeId source, NodeId target,
                      const SearchOptions& opt);

// One-to-all Dijkstra used by the TSP distance matrix. Stops early once every
// node in `targets` is settled. Fills `dist` and `parent` (size = node_count).
void dijkstra_to_targets(const Graph& g, NodeId source, const std::vector<NodeId>& targets,
                         Weight w, std::vector<double>& dist, std::vector<NodeId>& parent);

// Helpers shared by the algorithms.
std::vector<NodeId> reconstruct_path(const std::vector<NodeId>& parent, NodeId source,
                                     NodeId target);
// Fills distance_m / time_s of a path, choosing the cheapest parallel edge.
void measure_path(const Graph& g, const std::vector<NodeId>& path, Weight w,
                  double& distance_m, double& time_s);

}  // namespace pathos

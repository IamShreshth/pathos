// Path reconstruction and measurement shared by all routing algorithms.
#include <algorithm>

#include "pathos/routing.hpp"

namespace pathos {

std::vector<NodeId> reconstruct_path(const std::vector<NodeId>& parent, NodeId source, NodeId target) {
    std::vector<NodeId> path;
    for (NodeId v = target; v != kNoNode; v = parent[v]) {
        path.push_back(v);
        if (v == source) break;
    }
    if (path.empty() || path.back() != source) return {};
    std::reverse(path.begin(), path.end());
    return path;
}

void measure_path(const Graph& g, const std::vector<NodeId>& path, Weight w, double& distance_m,
                  double& time_s) {
    distance_m = 0.0;
    time_s = 0.0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const Edge* best = nullptr;
        for (const Edge& e : g.neighbors(path[i])) {
            if (e.to == path[i + 1] && (!best || Graph::weight_of(e, w) < Graph::weight_of(*best, w)))
                best = &e;
        }
        if (best) {
            distance_m += best->length_m;
            time_s += best->time_s;
        }
    }
}

RouteResult run_route(Algorithm a, const Graph& g, NodeId s, NodeId t, const SearchOptions& opt) {
    switch (a) {
        case Algorithm::BFS: return bfs(g, s, t, opt);
        case Algorithm::Dijkstra: return dijkstra(g, s, t, opt);
        case Algorithm::AStar: return astar(g, s, t, opt);
    }
    return {};
}

}  // namespace pathos

// A* search: Dijkstra ordered by f(n) = g(n) + h(n), where h(n) is the
// straight-line geographic distance (or that distance at the fastest road
// speed when minimising travel time). Both are admissible and consistent, so
// A* returns the same optimal cost as Dijkstra while expanding fewer nodes.
#include <chrono>
#include <functional>
#include <queue>
#include <tuple>

#include "pathos/routing.hpp"

namespace pathos {

RouteResult astar(const Graph& g, NodeId source, NodeId target, const SearchOptions& opt) {
    const auto t0 = std::chrono::steady_clock::now();
    RouteResult r;
    r.algorithm = Algorithm::AStar;
    r.weight = opt.weight;

    const std::size_t n = g.node_count();
    std::vector<double> gscore(n, kInf);
    std::vector<NodeId> parent(n, kNoNode);
    std::vector<char> closed(n, 0);

    using QItem = std::tuple<double, double, NodeId>;  // (f, g, node); ties broken by smaller g
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> open;

    gscore[source] = 0.0;
    open.push({g.heuristic(source, target, opt.weight), 0.0, source});
    while (!open.empty()) {
        const auto [f, gu, u] = open.top();
        open.pop();
        if (closed[u]) continue;
        closed[u] = 1;
        ++r.expanded;
        if (opt.record_trace) {
            if (r.trace.size() < opt.max_trace_events) r.trace.push_back({u, parent[u]});
            else r.trace_truncated = true;
        }
        if (u == target) {
            r.found = true;
            break;
        }
        for (const Edge& e : g.neighbors(u)) {
            if (closed[e.to]) continue;
            const double ng = gu + Graph::weight_of(e, opt.weight);
            if (ng < gscore[e.to]) {
                gscore[e.to] = ng;
                parent[e.to] = u;
                open.push({ng + g.heuristic(e.to, target, opt.weight), ng, e.to});
            }
        }
    }

    if (r.found) {
        r.cost = gscore[target];
        r.path = reconstruct_path(parent, source, target);
        measure_path(g, r.path, opt.weight, r.distance_m, r.time_s);
    }
    r.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

}  // namespace pathos

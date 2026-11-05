// Breadth-first search: the route with the fewest road segments.
#include <chrono>
#include <queue>

#include "pathos/routing.hpp"

namespace pathos {

RouteResult bfs(const Graph& g, NodeId source, NodeId target, const SearchOptions& opt) {
    const auto t0 = std::chrono::steady_clock::now();
    RouteResult r;
    r.algorithm = Algorithm::BFS;
    r.weight = opt.weight;

    const std::size_t n = g.node_count();
    std::vector<NodeId> parent(n, kNoNode);
    std::vector<char> seen(n, 0);
    std::queue<NodeId> q;

    seen[source] = 1;
    q.push(source);
    while (!q.empty()) {
        const NodeId u = q.front();
        q.pop();
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
            if (!seen[e.to]) {
                seen[e.to] = 1;
                parent[e.to] = u;
                q.push(e.to);
            }
        }
    }

    if (r.found) {
        r.path = reconstruct_path(parent, source, target);
        r.cost = static_cast<double>(r.path.size() - 1);  // number of segments
        measure_path(g, r.path, opt.weight, r.distance_m, r.time_s);
    }
    r.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

}  // namespace pathos

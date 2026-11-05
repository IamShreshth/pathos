// Dijkstra's algorithm with a binary min-heap and lazy deletion.
#include <chrono>
#include <functional>
#include <queue>
#include <utility>

#include "pathos/routing.hpp"

namespace pathos {

namespace {
using QItem = std::pair<double, NodeId>;  // (distance, node)
using MinHeap = std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>>;
}  // namespace

RouteResult dijkstra(const Graph& g, NodeId source, NodeId target, const SearchOptions& opt) {
    const auto t0 = std::chrono::steady_clock::now();
    RouteResult r;
    r.algorithm = Algorithm::Dijkstra;
    r.weight = opt.weight;

    const std::size_t n = g.node_count();
    std::vector<double> dist(n, kInf);
    std::vector<NodeId> parent(n, kNoNode);
    std::vector<char> settled(n, 0);
    MinHeap pq;

    dist[source] = 0.0;
    pq.push({0.0, source});
    while (!pq.empty()) {
        const auto [d, u] = pq.top();
        pq.pop();
        if (settled[u]) continue;  // stale heap entry
        settled[u] = 1;
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
            const double nd = d + Graph::weight_of(e, opt.weight);
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                parent[e.to] = u;
                pq.push({nd, e.to});
            }
        }
    }

    if (r.found) {
        r.cost = dist[target];
        r.path = reconstruct_path(parent, source, target);
        measure_path(g, r.path, opt.weight, r.distance_m, r.time_s);
    }
    r.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

void dijkstra_to_targets(const Graph& g, NodeId source, const std::vector<NodeId>& targets, Weight w,
                         std::vector<double>& dist, std::vector<NodeId>& parent) {
    const std::size_t n = g.node_count();
    dist.assign(n, kInf);
    parent.assign(n, kNoNode);
    std::vector<char> settled(n, 0);
    std::vector<char> is_target(n, 0);
    std::size_t remaining = 0;
    for (NodeId t : targets) {
        if (!is_target[t]) {
            is_target[t] = 1;
            ++remaining;
        }
    }

    MinHeap pq;
    dist[source] = 0.0;
    pq.push({0.0, source});
    while (!pq.empty() && remaining > 0) {
        const auto [d, u] = pq.top();
        pq.pop();
        if (settled[u]) continue;
        settled[u] = 1;
        if (is_target[u]) --remaining;
        for (const Edge& e : g.neighbors(u)) {
            const double nd = d + Graph::weight_of(e, w);
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                parent[e.to] = u;
                pq.push({nd, e.to});
            }
        }
    }
}

}  // namespace pathos

// Directed, weighted road graph stored as an adjacency list.
#pragma once

#include <string>
#include <vector>

#include "pathos/types.hpp"

namespace pathos {

// Great-circle distance between two coordinates, in metres.
double haversine_m(double lat1, double lon1, double lat2, double lon2);

class Graph {
public:
    Graph() = default;

    // Load a graph exported by scripts/prepare_graph.py (format documented
    // in Architecture.md). Throws std::runtime_error on malformed input.
    static Graph load(const std::string& path);

    NodeId add_node(double lat, double lon, std::int64_t osm_id = -1);
    // Adds a directed edge u -> v. A non-positive time is derived from the
    // length at a default urban speed.
    void add_edge(NodeId u, NodeId v, double length_m, double time_s = 0.0);

    std::size_t node_count() const { return nodes_.size(); }
    std::size_t edge_count() const { return edge_count_; }
    bool valid(NodeId u) const { return u >= 0 && static_cast<std::size_t>(u) < nodes_.size(); }

    const Node& node(NodeId u) const { return nodes_[u]; }
    const std::vector<Edge>& neighbors(NodeId u) const { return adj_[u]; }

    static double weight_of(const Edge& e, Weight w) {
        return w == Weight::Distance ? e.length_m : e.time_s;
    }

    // Fastest speed on any edge (m/s); used to keep the A* time heuristic admissible.
    double max_speed_mps() const { return max_speed_mps_; }

    // Lower bound on the remaining cost from u to t under weight w.
    double heuristic(NodeId u, NodeId t, Weight w) const;

    const std::string& version() const { return version_; }
    void set_version(std::string v) { version_ = std::move(v); }

private:
    std::vector<Node> nodes_;
    std::vector<std::vector<Edge>> adj_;
    std::size_t edge_count_ = 0;
    double max_speed_mps_ = 0.0;
    std::string version_ = "unversioned";
};

}  // namespace pathos

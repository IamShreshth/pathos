// Core value types shared by every part of the pathos engine.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace pathos {

// Compact, zero-based node index (0 .. node_count-1).
using NodeId = std::int32_t;
constexpr NodeId kNoNode = -1;

constexpr double kInf = std::numeric_limits<double>::infinity();

struct Node {
    double lat = 0.0;
    double lon = 0.0;
    std::int64_t osm_id = -1;  // original OpenStreetMap ID, -1 for synthetic graphs
};

// A directed road segment. One-way roads only exist in their permitted
// direction; two-way roads are stored as two separate edges.
struct Edge {
    NodeId to = kNoNode;
    double length_m = 0.0;  // physical length in metres
    double time_s = 0.0;    // estimated free-flow travel time in seconds
};

// Which edge cost the weighted searches minimise.
enum class Weight { Distance, Time };

enum class Algorithm { BFS, Dijkstra, AStar };

// One step of a search, recorded when a node is expanded. `parent` is the
// node it was reached from, so the UI can draw the growing search tree.
struct SearchEvent {
    NodeId node = kNoNode;
    NodeId parent = kNoNode;
};

struct RouteResult {
    Algorithm algorithm = Algorithm::Dijkstra;
    Weight weight = Weight::Distance;
    bool found = false;
    double cost = kInf;        // objective value: hops for BFS, metres or seconds otherwise
    double distance_m = 0.0;   // physical length of the returned path
    double time_s = 0.0;       // estimated travel time of the returned path
    std::vector<NodeId> path;  // source .. destination, empty if unreachable
    std::size_t expanded = 0;  // nodes removed from the frontier and expanded
    double duration_ms = 0.0;  // wall-clock time of the search itself
    std::vector<SearchEvent> trace;
    bool trace_truncated = false;
};

std::string to_string(Algorithm a);
std::string to_string(Weight w);
bool parse_algorithm(const std::string& s, Algorithm& out);
bool parse_weight(const std::string& s, Weight& out);

}  // namespace pathos

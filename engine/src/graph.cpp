#include "pathos/graph.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace pathos {

namespace {
constexpr double kEarthRadiusM = 6371008.8;
constexpr double kDefaultSpeedMps = 30.0 / 3.6;  // fallback when no travel time is given
constexpr double kPi = 3.14159265358979323846;

[[noreturn]] void fail(const std::string& path, const std::string& msg) {
    throw std::runtime_error("graph file '" + path + "': " + msg);
}

void expect_keyword(std::istream& in, const std::string& path, const std::string& kw) {
    std::string tok;
    if (!(in >> tok) || tok != kw) fail(path, "expected '" + kw + "', got '" + tok + "'");
}
}  // namespace

double haversine_m(double lat1, double lon1, double lat2, double lon2) {
    const double r = kPi / 180.0;
    const double dlat = (lat2 - lat1) * r;
    const double dlon = (lon2 - lon1) * r;
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(lat1 * r) * std::cos(lat2 * r) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(a)));
}

NodeId Graph::add_node(double lat, double lon, std::int64_t osm_id) {
    nodes_.push_back({lat, lon, osm_id});
    adj_.emplace_back();
    return static_cast<NodeId>(nodes_.size() - 1);
}

void Graph::add_edge(NodeId u, NodeId v, double length_m, double time_s) {
    if (!valid(u) || !valid(v)) throw std::out_of_range("edge endpoint out of range");
    if (length_m < 0 || !std::isfinite(length_m)) throw std::invalid_argument("edge length must be >= 0");
    if (time_s <= 0 || !std::isfinite(time_s)) time_s = length_m / kDefaultSpeedMps;
    adj_[u].push_back({v, length_m, time_s});
    ++edge_count_;
    if (time_s > 0) max_speed_mps_ = std::max(max_speed_mps_, length_m / time_s);
}

double Graph::heuristic(NodeId u, NodeId t, Weight w) const {
    // Scaled down slightly: OSM edge lengths are measured along the road
    // geometry with a slightly different earth model, and the heuristic must
    // never overestimate for A* to stay optimal.
    const double d = 0.999 * haversine_m(nodes_[u].lat, nodes_[u].lon, nodes_[t].lat, nodes_[t].lon);
    if (w == Weight::Distance) return d;
    return max_speed_mps_ > 0 ? d / max_speed_mps_ : 0.0;
}

// File format (whitespace separated, '#' lines ignored before the header):
//   pathos-graph 1
//   version <name>
//   nodes <N>
//   <lat> <lon> <osm_id>                       (N lines, line i = node i)
//   edges <M>
//   <u> <v> <length_m> <time_s> <k> [lat lon]*k   (M lines; k shape points
//                                                 are for drawing only)
Graph Graph::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) fail(path, "cannot open");

    // Skip leading comment lines.
    std::string line;
    std::streampos start = in.tellg();
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] != '#') break;
        start = in.tellg();
    }
    in.clear();
    in.seekg(start);

    Graph g;
    expect_keyword(in, path, "pathos-graph");
    int fmt = 0;
    if (!(in >> fmt) || fmt != 1) fail(path, "unsupported format version");
    expect_keyword(in, path, "version");
    if (!(in >> g.version_)) fail(path, "missing version name");

    expect_keyword(in, path, "nodes");
    long long n = 0;
    if (!(in >> n) || n < 0) fail(path, "bad node count");
    g.nodes_.reserve(n);
    g.adj_.reserve(n);
    for (long long i = 0; i < n; ++i) {
        double lat, lon;
        long long osm;
        if (!(in >> lat >> lon >> osm)) fail(path, "bad node line " + std::to_string(i));
        g.add_node(lat, lon, osm);
    }

    expect_keyword(in, path, "edges");
    long long m = 0;
    if (!(in >> m) || m < 0) fail(path, "bad edge count");
    for (long long i = 0; i < m; ++i) {
        long long u, v;
        int k;
        double len, t;
        if (!(in >> u >> v >> len >> t >> k) || k < 0) fail(path, "bad edge line " + std::to_string(i));
        for (int j = 0; j < 2 * k; ++j) {
            double skip;
            if (!(in >> skip)) fail(path, "bad shape points on edge " + std::to_string(i));
        }
        if (u < 0 || v < 0 || u >= n || v >= n) fail(path, "edge " + std::to_string(i) + " out of range");
        g.add_edge(static_cast<NodeId>(u), static_cast<NodeId>(v), len, t);
    }
    return g;
}

std::string to_string(Algorithm a) {
    switch (a) {
        case Algorithm::BFS: return "bfs";
        case Algorithm::Dijkstra: return "dijkstra";
        case Algorithm::AStar: return "astar";
    }
    return "unknown";
}

std::string to_string(Weight w) { return w == Weight::Distance ? "distance" : "time"; }

bool parse_algorithm(const std::string& s, Algorithm& out) {
    if (s == "bfs") out = Algorithm::BFS;
    else if (s == "dijkstra") out = Algorithm::Dijkstra;
    else if (s == "astar") out = Algorithm::AStar;
    else return false;
    return true;
}

bool parse_weight(const std::string& s, Weight& out) {
    if (s == "distance") out = Weight::Distance;
    else if (s == "time") out = Weight::Time;
    else return false;
    return true;
}

}  // namespace pathos

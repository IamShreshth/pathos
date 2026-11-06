// pathos_engine — command-line entry point of the C++ routing engine.
//
// Usage:
//   pathos_engine <graph-file> serve           long-running: one command per stdin line
//   pathos_engine <graph-file> <command...>    run a single command and exit
//
// Commands (whitespace separated; one JSON object is printed per command):
//   info
//   route <bfs|dijkstra|astar> <distance|time> <source> <target> [trace 0|1]
//   tour <distance|time> <stop1> <stop2> ... <stopK>
//   quit
//   {"cmd": ...}                              Pathos 2.0 JSON commands (traffic, td_route,
//                                              replan_demo, fleet, bench); see commands.hpp
//
// Node arguments are compact node indices (0 .. N-1) from the graph file.
// In serve mode the engine prints {"ok":true,"ready":true,...} once the graph
// is loaded, so the caller knows it can start sending commands.
#include <cmath>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "pathos/commands.hpp"
#include "pathos/graph.hpp"
#include "pathos/json.hpp"
#include "pathos/routing.hpp"
#include "pathos/traffic.hpp"
#include "pathos/tsp.hpp"

using namespace pathos;

namespace {

constexpr int kMaxTourStops = 200;

std::string error_json(const std::string& msg) {
    JsonWriter j;
    j.begin_object().field("ok", false).field("error", msg).end_object();
    return j.str();
}

void write_info(JsonWriter& j, const Graph& g) {
    j.field("graph_version", g.version())
        .field("nodes", static_cast<unsigned long long>(g.node_count()))
        .field("edges", static_cast<unsigned long long>(g.edge_count()))
        .field("max_speed_mps", g.max_speed_mps());
}

NodeId parse_node(const Graph& g, const std::string& tok) {
    std::size_t pos = 0;
    long long v = -1;
    try {
        v = std::stoll(tok, &pos);
    } catch (const std::exception&) {
        pos = 0;
    }
    if (pos != tok.size() || v < 0 || v >= static_cast<long long>(g.node_count()))
        throw std::invalid_argument("invalid node id '" + tok + "'");
    return static_cast<NodeId>(v);
}

std::string cmd_route(const Graph& g, const std::vector<std::string>& a) {
    if (a.size() < 5 || a.size() > 6)
        return error_json("usage: route <bfs|dijkstra|astar> <distance|time> <source> <target> [trace]");
    Algorithm algo;
    Weight w;
    if (!parse_algorithm(a[1], algo)) return error_json("unknown algorithm '" + a[1] + "'");
    if (!parse_weight(a[2], w)) return error_json("unknown weight '" + a[2] + "'");
    const NodeId s = parse_node(g, a[3]);
    const NodeId t = parse_node(g, a[4]);
    SearchOptions opt;
    opt.weight = w;
    opt.record_trace = a.size() == 6 && a[5] == "1";

    const RouteResult r = run_route(algo, g, s, t, opt);

    JsonWriter j;
    j.begin_object()
        .field("ok", true)
        .field("algorithm", to_string(r.algorithm))
        .field("weight", to_string(r.weight))
        .field("status", r.found ? "success" : "unreachable")
        .field("cost", r.cost)
        .field("distance_m", r.distance_m)
        .field("time_s", r.time_s)
        .field("segments", r.found ? static_cast<int>(r.path.size()) - 1 : 0)
        .field("expanded_nodes", static_cast<unsigned long long>(r.expanded))
        .field("duration_ms", r.duration_ms);
    j.key("path").array(r.path);
    if (opt.record_trace) {
        // Flattened [node, parent, node, parent, ...] to keep the payload small.
        j.key("trace").begin_array();
        for (const SearchEvent& e : r.trace) j.value(e.node).value(e.parent);
        j.end_array();
        j.field("trace_truncated", r.trace_truncated);
    }
    j.end_object();
    return j.str();
}

std::string cmd_tour(const Graph& g, const std::vector<std::string>& a) {
    if (a.size() < 4) return error_json("usage: tour <distance|time> <stop1> <stop2> ...");
    Weight w;
    if (!parse_weight(a[1], w)) return error_json("unknown weight '" + a[1] + "'");
    std::vector<NodeId> stops;
    for (std::size_t i = 2; i < a.size(); ++i) stops.push_back(parse_node(g, a[i]));
    if (static_cast<int>(stops.size()) > kMaxTourStops)
        return error_json("too many stops (max " + std::to_string(kMaxTourStops) + ")");

    const TourResult r = solve_tour(g, stops, w);
    auto finite_or_null = [](double v) { return v < kUnreachablePenalty ? v : kInf; };

    JsonWriter j;
    j.begin_object()
        .field("ok", true)
        .field("weight", to_string(r.weight))
        .field("status", r.feasible ? "success" : "unreachable")
        .field("cost", r.cost)
        .field("nn_cost", finite_or_null(r.nn_cost))
        .field("distance_m", r.distance_m)
        .field("time_s", r.time_s)
        .field("two_opt_moves", r.two_opt_moves)
        .field("matrix_ms", r.matrix_ms)
        .field("heuristic_ms", r.heuristic_ms);
    j.key("optimal_cost");
    if (r.optimal_cost) j.value(*r.optimal_cost);
    else j.null();
    j.key("order").array(r.order);
    j.key("nn_order").array(r.nn_order);
    j.key("matrix").begin_array();
    for (const auto& row : r.matrix) j.array(row);
    j.end_array();
    j.key("legs").begin_array();
    for (const TourLeg& l : r.legs) {
        j.begin_object()
            .field("from", l.from_stop)
            .field("to", l.to_stop)
            .field("reachable", l.reachable)
            .field("cost", l.cost)
            .field("distance_m", l.distance_m)
            .field("time_s", l.time_s);
        j.key("path").array(l.path);
        j.end_object();
    }
    j.end_array();
    j.end_object();
    return j.str();
}

std::string dispatch(const Graph& g, const std::vector<std::string>& a) {
    if (a.empty()) return error_json("empty command");
    try {
        if (a[0] == "info") {
            JsonWriter j;
            j.begin_object().field("ok", true);
            write_info(j, g);
            j.end_object();
            return j.str();
        }
        if (a[0] == "route") return cmd_route(g, a);
        if (a[0] == "tour") return cmd_tour(g, a);
        return error_json("unknown command '" + a[0] + "'");
    } catch (const std::exception& e) {
        return error_json(e.what());
    }
}

std::vector<std::string> split(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    for (std::string tok; in >> tok;) out.push_back(tok);
    return out;
}

// The traffic model is built once per graph; JSON commands may override its parameters per request.
const EngineContext* g_ctx = nullptr;

std::string run_line(const Graph& g, const std::string& line) {
    std::size_t i = line.find_first_not_of(" \t\r");
    if (i != std::string::npos && line[i] == '{') return handle_json_command(*g_ctx, line.substr(i));
    return dispatch(g, split(line));
}


}  // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <graph-file> serve | <command...>\n";
        return 2;
    }

    Graph g;
    try {
        g = Graph::load(argv[1]);
    } catch (const std::exception& e) {
        std::cout << error_json(e.what()) << std::endl;
        return 1;
    }

    TrafficModel traffic(g);
    EngineContext ctx{g, traffic};
    g_ctx = &ctx;

    const std::string mode = argv[2];
    if (mode != "serve") {
        std::string joined;
        for (int i = 2; i < argc; ++i) joined += std::string(i > 2 ? " " : "") + argv[i];
        const std::string out = run_line(g, joined);
        std::cout << out << std::endl;
        return out.rfind("{\"ok\":true", 0) == 0 ? 0 : 1;
    }

    {
        JsonWriter j;
        j.begin_object().field("ok", true).field("ready", true);
        write_info(j, g);
        j.end_object();
        std::cout << j.str() << std::endl;
    }
    for (std::string line; std::getline(std::cin, line);) {
        const auto args = split(line);
        if (args.empty()) continue;
        if (args[0] == "quit") break;
        std::cout << run_line(g, line) << std::endl;  // endl flushes: the caller waits per line
    }
    return 0;
}

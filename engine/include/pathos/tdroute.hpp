// Time-dependent and uncertainty-aware point-to-point routing, plus
// incremental route repair after a disruption.
#pragma once

#include <string>
#include <vector>

#include "pathos/traffic.hpp"
#include "pathos/types.hpp"

namespace pathos {

// A route as nodes plus, for every hop, the index of the edge inside the tail
// node's adjacency list (so parallel edges stay distinguishable).
struct Path {
    std::vector<NodeId> nodes;
    std::vector<int> ks;
    bool empty() const { return nodes.empty(); }
    std::size_t edges() const { return ks.size(); }
};

struct TdQuery {
    NodeId source = kNoNode;
    double depart = 0.0;
    EvalMode mode;
    NodeId target = kNoNode;                       // stop once settled (kNoNode: no single target)
    const std::vector<NodeId>* targets = nullptr;  // stop once all are settled
    bool astar = false;                            // needs a single target; ignored for scenario mode
    bool trace = false;
    std::size_t max_trace = 400000;
};

struct TdSearch {
    std::vector<double> arr;  // earliest arrival time at each node (kInf = not reached)
    std::vector<NodeId> parent;
    std::vector<int> pk;      // edge index inside parent's adjacency list
    std::size_t expanded = 0;
    std::vector<SearchEvent> trace;
    bool trace_truncated = false;
};

// Earliest-arrival search on the time-dependent network (Dijkstra, or A* with a
// free-flow straight-line bound). Correct because edge times are FIFO.
void td_search(const Net& net, const TdQuery& q, TdSearch& out);

Path extract_path(const TdSearch& s, NodeId source, NodeId target);

struct TdResult {
    bool found = false;
    double depart = 0.0;
    double arrival = kInf;
    double travel_s = kInf;
    double distance_m = 0.0;
    Path path;
    std::size_t expanded = 0;
    double duration_ms = 0.0;
    std::vector<SearchEvent> trace;
    bool trace_truncated = false;
};

TdResult td_route(const Net& net, NodeId source, NodeId target, double depart, const EvalMode& mode,
                  bool astar = true, bool trace = false);

// Arrival time after driving nodes[from_idx..] of `p`, starting at time t.
double walk_path(const Net& net, const Path& p, std::size_t from_idx, double t, const EvalMode& mode);
double path_length_m(const Graph& g, const Path& p);
// Index of the cheapest edge u -> v, or -1.
int find_edge(const Graph& g, NodeId u, NodeId v);
Path path_from_nodes(const Graph& g, const std::vector<NodeId>& nodes);

// ---------------------------------------------------------------- uncertainty
struct RouteStats {
    std::string label;
    double kappa = 0.0;            // robustness parameter used to generate it (-1: scenario optimum)
    Path path;
    double distance_m = 0.0;
    double free_s = 0.0;           // time on the empty network
    double expected_s = 0.0;       // time-dependent expected time (departure to arrival)
    double mc_mean = 0.0, mc_std = 0.0, mc_p50 = 0.0, mc_p95 = 0.0, mc_worst = 0.0;
    double p_on_time = 0.0;        // empirical P(T <= deadline)
};

struct ReliabilityQuery {
    NodeId source = kNoNode, target = kNoNode;
    double depart = hours(8);
    double deadline_s = 0.0;       // allowed trip duration; <= 0: 1.15 x the expected fastest time
    double alpha = 0.1;            // tolerated probability of missing the deadline
    int scenarios = 300;           // Monte Carlo scenarios used to evaluate every candidate
    int candidate_scenarios = 12;  // scenario-optimal candidates added to the pool
};

struct ReliabilityResult {
    std::vector<RouteStats> candidates;
    int freeflow = -1, expected = -1, robust = -1, reliable = -1;
    bool reliable_feasible = false;  // some candidate met P(T <= D) >= 1 - alpha
    double deadline_s = 0.0;
    double duration_ms = 0.0;
};

ReliabilityResult reliability_routes(const Net& net, const ReliabilityQuery& q);

// Standard normal quantile (Acklam's approximation).
double normal_quantile(double p);

// ------------------------------------------------------------------ replanning
struct RepairResult {
    bool affected = false;   // the remaining route touches a changed edge
    bool repaired = false;   // a replacement was produced
    bool escalated = false;  // local repair failed; fell back to a full search
    Path path;               // new remaining route starting at the vehicle's node
    double arrival = kInf;
    std::size_t expanded = 0;
    double duration_ms = 0.0;
};

// Selective replanning of one vehicle. `old` is the remaining route (old.nodes[0]
// is the vehicle's node, reached at time t_now). Only if the route crosses an
// edge in `fresh` (the newly announced disruptions) is anything recomputed, and
// then only the affected stretch: a search from the first affected node to any
// later node of the old route, spliced back into it.
//
// `lookback`: the detour may leave the old route up to this many edges before the first affected
// edge (0: only at the affected edge itself; large: anywhere ahead of the vehicle). A larger value
// finds better detours and searches more.
RepairResult local_repair(const Net& net, const Disruptions& fresh, const Path& old, double t_now, int lookback = 5);

// One vehicle, one disruption announced while it drives: compares keeping the plan,
// complete recomputation and selective local repair (the single-vehicle E3/E4 case).
struct ReplanOptions {
    NodeId source = kNoNode, target = kNoNode;
    double depart = hours(8);
    double event_after_s = 60;   // the disruption is announced this long after departure
    int edges_ahead = 10;        // it hits the road this many edges ahead of the vehicle
    double duration_s = 3600;
    bool closure = true;
    double factor = 4.0;         // slowdown factor for incidents
    int repeat = 1;              // timing repetitions (averaged)
    int lookback = 5;            // see local_repair
    std::vector<std::pair<NodeId, NodeId>> roads;  // explicit roads; empty: pick from the route
};

struct ReplanStrategy {
    Path path;                   // whole trip: driven prefix + new remainder
    double arrival = kInf, distance_m = 0, duration_ms = 0;
    std::size_t expanded = 0;
    bool changed = false, affected = false, escalated = false;
};

struct ReplanOutcome {
    bool ok = false;
    Path original;
    double planned_arrival = kInf, planned_travel = kInf, planned_distance = 0;
    double t_event = 0;
    NodeId event_node = kNoNode;
    std::vector<std::pair<NodeId, NodeId>> roads;
    ReplanStrategy keep, full, selective;
};

ReplanOutcome run_replan_case(const Graph& g, const TrafficModel& tm, const ReplanOptions& o);

}  // namespace pathos

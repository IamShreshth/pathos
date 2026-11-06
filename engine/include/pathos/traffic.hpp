// Time-dependent, uncertain traffic on top of the static road graph.
//
//   tau_e(t)      = free_flow_e * congestion_e(t) * incident_e(t)          (expected time)
//   T_e(t, omega) = tau_e(t) * exp(cv_e(t) * Z_e(omega) - cv_e(t)^2 / 2)    (scenario time, mean tau_e)
//
// congestion_e(t) = 1 + intensity * sensitivity_e * profile(t) where profile is a
// synthetic daily curve with morning and evening peaks (explicitly synthetic:
// no real traffic feed is used) and sensitivity_e grows towards the city
// centre and on faster roads. Z_e is standard normal, correlated across edges
// through a city-wide and a regional component, so route times do not average
// out unrealistically. Scenarios are deterministic functions of (seed, id):
// every routing policy can be evaluated on identical "common random numbers".
//
// A road closure is an interval during which an edge cannot be entered; a
// vehicle that reaches it waits for the interval to end (FIFO is preserved).
// A slowdown multiplies the edge time inside an interval (accidents, works).
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "pathos/graph.hpp"

namespace pathos {

constexpr double kDay = 86400.0;
inline double hours(double h) { return h * 3600.0; }

struct TrafficParams {
    double intensity = 1.0;  // 0 disables time dependence (free-flow network)
    double base_cv = 0.08;   // travel-time coefficient of variation at free flow
    double cv_slope = 0.12;  // extra variability per unit of congestion
    std::uint64_t seed = 42;
};

struct Span {
    double from = 0.0;
    double to = 0.0;
    double factor = kInf;  // kInf means closed
};

// Closures and slowdowns indexed by edge id (see TrafficModel::edge_id).
class Disruptions {
public:
    explicit Disruptions(std::size_t edges = 0) : flagged_(edges, 0) {}
    void resize(std::size_t edges) { flagged_.assign(edges, 0); spans_.clear(); }
    void add_closure(int eid, double from, double to) { add(eid, {from, to, kInf}); }
    void add_slowdown(int eid, double from, double to, double factor) { add(eid, {from, to, factor}); }
    bool flagged(int eid) const { return flagged_[eid] != 0; }
    bool empty() const { return spans_.empty(); }
    std::size_t edge_count() const { return spans_.size(); }
    // Earliest time >= t at which the edge can be entered (kInf if never).
    double open_time(int eid, double t) const;
    // Product of the slowdown factors active at time t.
    double slowdown(int eid, double t) const;
    const std::vector<Span>* spans(int eid) const;
    std::vector<int> edges() const;
    void merge(const Disruptions& other);

private:
    void add(int eid, Span s);
    std::vector<char> flagged_;
    std::unordered_map<int, std::vector<Span>> spans_;
};

class TrafficModel {
public:
    TrafficModel(const Graph& g, TrafficParams p = {});

    std::size_t edge_count() const { return sens_.size(); }
    int first_edge(NodeId u) const { return first_[u]; }
    // Locate the edge id's tail node (binary search over the prefix sums).
    NodeId tail_of(int eid) const;

    static double profile(double t);                  // daily congestion curve in [0, 1]
    double congestion(int eid, double t) const;       // >= 1
    double cv(int eid, double t) const;               // coefficient of variation of the edge time
    double noise(int eid, std::uint32_t scenario, double cv) const;  // lognormal factor with mean 1
    const TrafficParams& params() const { return p_; }

private:
    TrafficParams p_;
    std::vector<int> first_;
    std::vector<float> sens_;
    std::vector<float> cv_scale_;
    std::vector<std::uint32_t> region_;
};

// How an edge time is computed.
struct EvalMode {
    bool freeflow = false;  // ignore traffic entirely (the "static" network)
    double kappa = 0.0;     // robust: mean * (1 + kappa * cv)
    int scenario = -1;      // >= 0: one realised scenario of the uncertain times
};

// The network a query runs on: graph + traffic model + known disruptions.
struct Net {
    const Graph* g = nullptr;
    const TrafficModel* tm = nullptr;
    const Disruptions* dis = nullptr;

    // Time at which a vehicle that stands at u at time t has crossed edge k of u
    // (kInf when the edge never reopens).
    double arrive(NodeId u, int k, double t, const EvalMode& m) const;
    // Lower bound on the edge time in any mode except `scenario` (used by A*).
    double max_speed() const { return g->max_speed_mps(); }
};

// Deterministic 64-bit mixing used for hash-based randomness.
std::uint64_t mix64(std::uint64_t x);
double hash_unit(std::uint64_t x);            // uniform in (0, 1)
double hash_gauss(std::uint64_t x);           // standard normal

}  // namespace pathos

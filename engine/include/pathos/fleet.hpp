// Multi-vehicle routing with time windows and capacities on the time-dependent
// network: time-bucketed travel-time matrices, route evaluation, insertion
// heuristics and Adaptive Large Neighbourhood Search (ALNS).
//
// Model ("pickup and return"): every request must be visited by one vehicle
// inside its time window, adds `demand` to the vehicle load (capacity), takes
// `service_s` seconds, and every vehicle returns to its depot before its shift
// ends. Requests that cannot be served feasibly stay unassigned and cost
// `unassigned_penalty`.
#pragma once

#include <cstdint>
#include <vector>

#include "pathos/tdroute.hpp"

namespace pathos {

struct Vehicle {
    int id = 0;
    NodeId depot = kNoNode;
    double capacity = 10;
    double shift_start = hours(7.5);
    double shift_end = hours(13);
};

struct Request {
    int id = 0;
    NodeId node = kNoNode;
    double demand = 1;
    double tw_start = 0;  // earliest start of service (seconds since midnight)
    double tw_end = 0;    // latest arrival
    double service_s = 120;
    double release_s = 0;  // time the request becomes known (dynamic requests)
};

// Objective  J = w_time*travel + w_dist*dist + w_energy*energy + w_late*lateness
//              + w_risk*risk + w_wait*wait + vehicle_fixed*(vehicles used)
//              + unassigned_penalty*(unserved) + beta*(schedule disruption)
struct FleetParams {
    double w_time = 1.0;           // per second driven
    double w_dist = 0.01;          // per metre
    double w_energy = 30.0;        // per kWh
    double w_late = 4.0;           // per second late (only with max_late_s > 0)
    double w_risk = 90.0;          // per unit of window-miss risk (see risk_margin_s)
    double w_wait = 0.1;           // per second of waiting
    double vehicle_fixed = 240.0;  // cost of using a vehicle that has not left the depot
    double unassigned_penalty = 20000.0;
    double beta = 0.0;             // per unit of schedule disruption (replanning only)
    double max_late_s = 0.0;       // planned lateness tolerated (soft windows)
    double risk_margin_s = 300.0;  // slack below this counts as risky
    double kwh_per_km = 0.15;      // empty vehicle; +60 % at full load
    double kappa = 0.0;            // reliability buffer: plan with mean*(1 + kappa*cv) edge times
    std::uint64_t seed = 1;
};

// Travel time / distance between a fixed set of nodes as a function of the
// departure time, sampled at time buckets and interpolated linearly.
class TdMatrix {
public:
    void build(const Net& net, const std::vector<NodeId>& nodes, double t0, double horizon_s, int max_buckets,
               const EvalMode& mode);
    double time(int i, int j, double t) const;
    double dist(int i, int j, double t) const;
    int size() const { return k_; }
    int searches = 0;
    double build_ms = 0.0;

private:
    double interp(const std::vector<double>& a, int i, int j, double t) const;
    int k_ = 0, b_ = 0;
    double t0_ = 0, step_ = 1;
    std::vector<double> t_, d_;  // [bucket][i][j]
};

struct SubVehicle {
    int vid = 0;
    NodeId pos = kNoNode;
    double avail = 0;  // time at which it can leave `pos`
    NodeId depot = kNoNode;
    double cap_total = 0, cap_left = 0;
    double shift_end = 0;
    bool pays_fixed = true;
};

struct SubRequest {
    int rid = 0;
    NodeId node = kNoNode;
    double demand = 0, tw_start = 0, tw_end = 0, service = 0;
};

// A (sub)problem: some vehicles, each with its current state, and the
// requests that may be (re)assigned among them.
struct Problem {
    double now = 0;
    std::vector<SubVehicle> veh;
    std::vector<SubRequest> req;
    std::vector<std::vector<int>> warm;  // previous routes (local request indices) per vehicle
    std::vector<int> prev_veh;           // previous local vehicle of each request, -1 if new
    std::vector<int> prev_rank;          // previous position in that vehicle's route
    // filled by build_matrix:
    std::vector<int> pos_idx, depot_idx, req_idx;
};

struct RouteEval {
    bool ok = false;
    double travel = 0, dist = 0, energy = 0, late = 0, risk = 0, wait = 0, finish = 0, cost = 0;
};

struct Solution {
    std::vector<std::vector<int>> routes;  // local request indices per vehicle
    std::vector<RouteEval> ev;
    std::vector<int> veh_of;               // local vehicle of each request, -1 if unassigned
    double cost = 0;                       // objective including penalties and disruption
    double disruption = 0;
    int unassigned() const;
};

struct PlanStats {
    int iterations = 0, accepted = 0, improved = 0;
    double initial_cost = 0;
    double matrix_ms = 0, solve_ms = 0;
    int matrix_searches = 0;
    int matrix_nodes = 0;
};

enum class PlanMode {
    Scratch,     // build from nothing, then ALNS (complete recomputation)
    Warm,        // start from the previous routes, then ALNS (selective replanning)
    InsertOnly,  // keep previous routes; insert only the new requests (cheapest insertion)
};

void build_matrix(const Net& net, Problem& P, const FleetParams& fp, TdMatrix& M);
RouteEval eval_route(const Problem& P, const TdMatrix& M, const FleetParams& fp, int v, const std::vector<int>& seq,
                     std::vector<double>* arrivals = nullptr);
// Schedule disruption of a solution relative to Problem::prev_*: 1 per request moved to another
// vehicle, 0.5 per request that stays but changes order among the requests that stay.
double schedule_disruption(const Problem& P, const std::vector<std::vector<int>>& routes);
double objective(const Problem& P, const FleetParams& fp, const Solution& s);

// Builds the matrix, solves and returns the best solution found.
Solution plan_problem(const Net& net, Problem& P, const FleetParams& fp, PlanMode mode, int iterations,
                      PlanStats* stats = nullptr, double time_budget_ms = 0.0);

// Evaluates a given assignment (used by tests and for brute-force comparison).
Solution make_solution(const Problem& P, const TdMatrix& M, const FleetParams& fp,
                       const std::vector<std::vector<int>>& routes);

// Exhaustive optimum for tiny instances (<= 8 requests): every assignment and ordering.
Solution brute_force(const Problem& P, const TdMatrix& M, const FleetParams& fp);

// Convenience: all vehicles at their depots at shift start and all requests, as one Problem.
Problem make_problem(const std::vector<Vehicle>& vehicles, const std::vector<Request>& requests, double now);

}  // namespace pathos

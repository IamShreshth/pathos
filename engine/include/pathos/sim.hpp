// Rolling-horizon simulation of a fleet on the time-dependent network.
//
// A discrete-event loop executes the vehicles on the *real* world (all
// disruptions, one sampled scenario of uncertain travel times) while the
// planner only knows what has been announced. Events are request arrivals and
// disruptions; four replanning strategies are compared:
//
//   Static     initial plan only; late requests are cheapest-inserted; blind to disruptions
//   Greedy     as Static for requests; on a disruption only the paths of affected vehicles are recomputed
//   Full       complete recomputation (all vehicles, from scratch) at every event
//   Selective  only the affected vehicles are re-optimised, warm-started, with a disruption penalty
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "pathos/fleet.hpp"

namespace pathos {

struct DisruptionEvent {
    double time = 0, until = 0;
    bool closure = true;
    double factor = 1.0;  // slowdown factor for incidents
    std::vector<std::pair<NodeId, NodeId>> roads;
    std::string label;
};

struct World {
    double t_start = hours(7.5);
    std::vector<Vehicle> vehicles;
    std::vector<Request> requests;
    std::vector<DisruptionEvent> events;
};

struct WorldConfig {
    int seed = 1;
    int vehicles = 4;
    int requests = 40;
    double dynamic_fraction = 0.4;  // share of requests that arrive after the start
    double window_min = 30;         // mean time-window width in minutes
    int closures = 2;
    int incidents = 1;
    double t_start = hours(7.5);
    double shift_hours = 5.5;
    double radius_m = 0;            // 0: whole graph
};

World generate_world(const Graph& g, const TrafficModel& tm, const WorldConfig& cfg);
// For a hand-made world: assign time windows / release times to every request (`windows`) and/or add random
// road closures and accidents on the busiest corridors from the first vehicle's depot (`events`).
void finish_world(const Graph& g, const TrafficModel& tm, World& w, const WorldConfig& cfg, bool windows, bool events);
// Build the disruption set (edge ids) of the events that start at or before `until_time`.
Disruptions build_disruptions(const Graph& g, const TrafficModel& tm, const std::vector<DisruptionEvent>& ev,
                              double until_time = kInf);

enum class Strategy { Static, Greedy, Full, Selective };
const char* to_string(Strategy s);
bool parse_strategy(const std::string& s, Strategy& out);

struct SimConfig {
    Strategy strategy = Strategy::Selective;
    FleetParams fp;
    int iters_per_request = 30;  // ALNS iterations = this x requests in the (sub)problem
    int initial_iterations = 0;  // 0: iters_per_request * requests
    double beta = 120.0;         // disruption weight used by Selective
    int selective_k = 3;         // vehicles considered for a new request
    int scenario = 0;            // realised travel-time scenario; < 0: expected times
};

struct ReplanLog {
    double time = 0;
    std::string kind;   // "initial" | "request" | "disruption"
    std::string label;
    int vehicles = 0;   // vehicles re-planned
    int requests = 0;   // requests in the re-planned problem
    double latency_ms = 0, matrix_ms = 0, solve_ms = 0, legs_ms = 0;
    double disruption = 0;
    bool escalated = false;
    bool replanned = false;
};

struct Trajectory {
    int vid = 0;
    std::vector<NodeId> nodes;
    std::vector<double> times;
};

struct RequestOutcome {
    int id = 0;
    bool served = false;
    bool on_time = false;
    int vehicle = -1;
    double arrival = 0, lateness = 0, response = 0;
};

struct SimMetrics {
    int requests = 0, on_time = 0, late = 0, unserved = 0;
    double success_rate = 0;        // on-time / all requests
    double avg_response_s = 0, p95_response_s = 0;
    double total_distance_m = 0, driving_s = 0, energy_kwh = 0, lateness_s = 0;
    int vehicles_used = 0;
    double cost = 0;                // realised objective
    int replans = 0, escalations = 0;
    double total_latency_ms = 0, mean_latency_ms = 0, max_latency_ms = 0;
    double schedule_disruption = 0;
    double initial_ms = 0;
    double makespan_s = 0;
};

struct SimResult {
    Strategy strategy = Strategy::Selective;
    SimMetrics m;
    std::vector<std::vector<int>> initial_routes;  // request ids per vehicle after the first plan
    double initial_cost = 0;
    int initial_unassigned = 0;
    PlanStats initial_stats;
    std::vector<RequestOutcome> outcomes;
    std::vector<ReplanLog> log;
    std::vector<Trajectory> trajectories;
};

SimResult simulate(const Graph& g, const TrafficModel& tm, const World& w, const SimConfig& cfg);

// Reliability of a fixed plan: the plan made at t_start (no replanning) is executed under many
// sampled scenarios (and the disruptions of the world), returning on-time statistics.
struct McPlanStats {
    double on_time_rate = 0;       // mean over scenarios of served-on-time / requests
    double on_time_rate_p5 = 0;    // 5th percentile over scenarios (the bad days)
    double mean_makespan_s = 0, p95_makespan_s = 0;
    double mean_distance_m = 0;
    std::vector<double> per_request_on_time;  // probability per request (same order as World::requests)
    int scenarios = 0;
};
McPlanStats evaluate_plan_mc(const Graph& g, const TrafficModel& tm, const World& w, const SimConfig& cfg,
                             int scenarios, bool include_disruptions);

}  // namespace pathos

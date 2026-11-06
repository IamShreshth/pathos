// JSON command layer of the engine: one JSON object in, one JSON object out.
//
//   {"cmd":"traffic", ...}      daily congestion curve / congestion map at a time
//   {"cmd":"td_route", ...}     static vs time-dependent vs robust vs reliability-constrained routes
//   {"cmd":"replan_demo", ...}  one vehicle, one disruption: keep vs full recomputation vs local repair
//   {"cmd":"fleet", ...}        multi-vehicle VRPTW world: replanning strategies and plan reliability
//   {"cmd":"bench", ...}        experiments E1..E7 (see bench.cpp)
#pragma once

#include <string>

#include "pathos/graph.hpp"
#include "pathos/fleet.hpp"
#include "pathos/jsonparse.hpp"
#include "pathos/traffic.hpp"

namespace pathos {

struct EngineContext {
    const Graph& g;
    const TrafficModel& tm;  // default traffic model; a request may override its parameters
};

std::string handle_json_command(const EngineContext& ctx, const std::string& line);

// Experiments; returns {"ok":true,"experiment":...,"rows":[...]}.
std::string run_bench(const EngineContext& ctx, const JsonValue& req);

TrafficParams parse_traffic(const JsonValue* v, TrafficParams base);
FleetParams parse_fleet_params(const JsonValue* v, FleetParams base);
Disruptions parse_disruptions(const Graph& g, const TrafficModel& tm, const JsonValue* v);

}  // namespace pathos

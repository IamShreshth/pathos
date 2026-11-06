#include "pathos/commands.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

#include "pathos/json.hpp"
#include "pathos/sim.hpp"
#include "pathos/tdroute.hpp"

namespace pathos {

namespace {

using Clock = std::chrono::steady_clock;

std::string error_json(const std::string& msg) {
    JsonWriter j;
    j.begin_object().field("ok", false).field("error", msg).end_object();
    return j.str();
}

NodeId need_node(const Graph& g, const JsonValue& v, const std::string& key) {
    const JsonValue* x = v.get(key);
    if (!x || !x->is_num()) throw std::invalid_argument("missing node '" + key + "'");
    const long long id = static_cast<long long>(x->n);
    if (id < 0 || id >= static_cast<long long>(g.node_count())) throw std::invalid_argument("invalid node id for '" + key + "'");
    return static_cast<NodeId>(id);
}

NodeId node_of(const Graph& g, const JsonValue& x) {
    if (!x.is_num() || x.n < 0 || x.n >= static_cast<double>(g.node_count())) throw std::invalid_argument("invalid node id");
    return static_cast<NodeId>(x.n);
}

void write_path(JsonWriter& j, const std::string& key, const Path& p) { j.key(key).array(p.nodes); }

// ------------------------------------------------------------ traffic params
}  // namespace

TrafficParams parse_traffic(const JsonValue* v, TrafficParams base) {
    if (!v || !v->is_obj()) return base;
    base.intensity = v->num("intensity", base.intensity);
    base.base_cv = v->num("base_cv", base.base_cv);
    base.cv_slope = v->num("cv_slope", base.cv_slope);
    base.seed = static_cast<std::uint64_t>(v->integer("seed", static_cast<long long>(base.seed)));
    return base;
}

FleetParams parse_fleet_params(const JsonValue* v, FleetParams b) {
    if (!v || !v->is_obj()) return b;
    b.w_time = v->num("w_time", b.w_time);
    b.w_dist = v->num("w_dist", b.w_dist);
    b.w_energy = v->num("w_energy", b.w_energy);
    b.w_late = v->num("w_late", b.w_late);
    b.w_risk = v->num("w_risk", b.w_risk);
    b.w_wait = v->num("w_wait", b.w_wait);
    b.vehicle_fixed = v->num("vehicle_fixed", b.vehicle_fixed);
    b.unassigned_penalty = v->num("unassigned_penalty", b.unassigned_penalty);
    b.max_late_s = v->num("max_late_s", b.max_late_s);
    b.risk_margin_s = v->num("risk_margin_s", b.risk_margin_s);
    b.kappa = v->num("kappa", b.kappa);
    b.seed = static_cast<std::uint64_t>(v->integer("seed", static_cast<long long>(b.seed)));
    return b;
}

Disruptions parse_disruptions(const Graph& g, const TrafficModel& tm, const JsonValue* v) {
    Disruptions d(tm.edge_count());
    if (!v || !v->is_arr()) return d;
    for (const JsonValue& e : v->items()) {
        const double from = e.num("from", 0), to = e.num("to", kInf);
        const bool closure = e.str("type", "closure") != "incident";
        const double factor = e.num("factor", 3.0);
        const JsonValue* roads = e.get("roads");
        if (!roads) continue;
        for (const JsonValue& r : roads->items()) {
            if (r.items().size() != 2) continue;
            const NodeId u = node_of(g, r.items()[0]), w = node_of(g, r.items()[1]);
            const int k = find_edge(g, u, w);
            if (k < 0) continue;
            if (closure) d.add_closure(tm.first_edge(u) + k, from, to);
            else d.add_slowdown(tm.first_edge(u) + k, from, to, factor);
        }
    }
    return d;
}

namespace {

// ------------------------------------------------------------------ traffic
std::string cmd_traffic(const EngineContext& ctx, const JsonValue& req) {
    JsonWriter j;
    j.begin_object().field("ok", true);
    j.key("profile").begin_array();
    for (int h = 0; h < 48; ++h) j.value(1.0 + ctx.tm.params().intensity * TrafficModel::profile(h * 1800.0));
    j.end_array();
    if (req.has("t_s")) {
        const double t = req.num("t_s", hours(8));
        const double min_mult = req.num("min_multiplier", 1.3);
        const Disruptions dis = parse_disruptions(ctx.g, ctx.tm, req.get("disruptions"));
        // flat [u, v, multiplier*100, ...] for the edges that are clearly slower than free flow
        j.key("edges").begin_array();
        for (std::size_t u = 0; u < ctx.g.node_count(); ++u) {
            const auto& adj = ctx.g.neighbors(static_cast<NodeId>(u));
            for (std::size_t k = 0; k < adj.size(); ++k) {
                const int eid = ctx.tm.first_edge(static_cast<NodeId>(u)) + static_cast<int>(k);
                double m = ctx.tm.congestion(eid, t);
                if (dis.flagged(eid)) {
                    if (std::isinf(dis.open_time(eid, t)) || dis.open_time(eid, t) > t) m = 99.0;  // closed now
                    else m *= dis.slowdown(eid, t);
                }
                if (m >= min_mult) j.value(static_cast<int>(u)).value(adj[k].to).value(static_cast<int>(std::lround(std::min(m, 99.0) * 100)));
            }
        }
        j.end_array();
    }
    j.end_object();
    return j.str();
}

// ----------------------------------------------------------------- td_route
void write_stats(JsonWriter& j, const RouteStats& s) {
    j.begin_object()
        .field("label", s.label)
        .field("kappa", s.kappa)
        .field("distance_m", s.distance_m)
        .field("free_s", s.free_s)
        .field("expected_s", s.expected_s)
        .field("mc_mean_s", s.mc_mean)
        .field("mc_std_s", s.mc_std)
        .field("mc_p50_s", s.mc_p50)
        .field("mc_p95_s", s.mc_p95)
        .field("mc_worst_s", s.mc_worst)
        .field("p_on_time", s.p_on_time);
    write_path(j, "path", s.path);
    j.end_object();
}

std::string cmd_td_route(const EngineContext& ctx, const JsonValue& req) {
    const NodeId s = need_node(ctx.g, req, "source"), t = need_node(ctx.g, req, "target");
    std::unique_ptr<TrafficModel> own;
    const TrafficModel* tm = &ctx.tm;
    if (req.has("traffic")) {
        own = std::make_unique<TrafficModel>(ctx.g, parse_traffic(req.get("traffic"), ctx.tm.params()));
        tm = own.get();
    }
    const Disruptions dis = parse_disruptions(ctx.g, *tm, req.get("disruptions"));
    const Net net{&ctx.g, tm, &dis};
    ReliabilityQuery q;
    q.source = s;
    q.target = t;
    q.depart = req.num("depart_s", hours(8));
    q.alpha = std::min(0.5, std::max(0.001, req.num("alpha", 0.1)));
    q.scenarios = static_cast<int>(std::min(2000.0, std::max(20.0, req.num("scenarios", 300))));
    q.candidate_scenarios = static_cast<int>(std::min(60.0, req.num("candidate_scenarios", 12)));
    q.deadline_s = req.num("deadline_s", 0.0);
    if (q.deadline_s <= 0 && req.has("slack")) {
        const TdResult ex = td_route(net, s, t, q.depart, EvalMode{}, true);
        if (ex.found) q.deadline_s = ex.travel_s * req.num("slack", 1.15);
    }
    const ReliabilityResult r = reliability_routes(net, q);
    if (r.candidates.empty()) {
        JsonWriter j;
        j.begin_object().field("ok", true).field("status", "unreachable").end_object();
        return j.str();
    }
    JsonWriter j;
    j.begin_object()
        .field("ok", true)
        .field("status", "success")
        .field("depart_s", q.depart)
        .field("deadline_s", r.deadline_s)
        .field("alpha", q.alpha)
        .field("reliable_feasible", r.reliable_feasible)
        .field("freeflow", r.freeflow)
        .field("expected", r.expected)
        .field("robust", r.robust)
        .field("reliable", r.reliable)
        .field("duration_ms", r.duration_ms);
    j.key("candidates").begin_array();
    for (const RouteStats& c : r.candidates) write_stats(j, c);
    j.end_array();
    j.end_object();
    return j.str();
}

// -------------------------------------------------------------- replan_demo
std::string cmd_replan(const EngineContext& ctx, const JsonValue& req) {
    ReplanOptions o;
    o.source = need_node(ctx.g, req, "source");
    o.target = need_node(ctx.g, req, "target");
    o.depart = req.num("depart_s", hours(8));
    o.event_after_s = req.num("event_after_s", 60);
    o.edges_ahead = static_cast<int>(req.num("edges_ahead", 10));
    o.duration_s = req.num("duration_s", 3600);
    o.closure = req.str("type", "closure") != "incident";
    o.factor = req.num("factor", 4.0);
    o.repeat = static_cast<int>(std::min(50.0, std::max(1.0, req.num("repeat", 5))));
    if (const JsonValue* roads = req.get("roads"))
        for (const JsonValue& r : roads->items())
            if (r.items().size() == 2) o.roads.push_back({node_of(ctx.g, r.items()[0]), node_of(ctx.g, r.items()[1])});
    const ReplanOutcome r = run_replan_case(ctx.g, ctx.tm, o);
    JsonWriter j;
    if (!r.ok) {
        j.begin_object().field("ok", true).field("status", "unreachable").end_object();
        return j.str();
    }
    j.begin_object().field("ok", true).field("status", "success");
    j.field("depart_s", o.depart).field("event_s", r.t_event).field("event_node", r.event_node)
        .field("duration_s", o.duration_s).field("type", o.closure ? "closure" : "incident");
    j.key("roads").begin_array();
    for (const auto& [u, v] : r.roads) j.begin_array().value(u).value(v).end_array();
    j.end_array();
    j.key("original").begin_object().field("arrival_s", r.planned_arrival).field("travel_s", r.planned_travel)
        .field("distance_m", r.planned_distance);
    write_path(j, "path", r.original);
    j.end_object();
    auto strat = [&](const char* name, const ReplanStrategy& s) {
        j.key(name).begin_object().field("arrival_s", s.arrival).field("travel_s", s.arrival - o.depart)
            .field("distance_m", s.distance_m).field("expanded_nodes", static_cast<unsigned long long>(s.expanded))
            .field("duration_ms", s.duration_ms).field("changed", s.changed).field("affected", s.affected)
            .field("escalated", s.escalated);
        write_path(j, "path", s.path);
        j.end_object();
    };
    strat("static", r.keep);
    strat("full", r.full);
    strat("selective", r.selective);
    j.end_object();
    return j.str();
}

// -------------------------------------------------------------------- fleet
World parse_world(const EngineContext& ctx, const TrafficModel& tm, const JsonValue& req) {
    World w;
    const JsonValue* gen = req.get("generate");
    if (gen && gen->is_obj()) {
        WorldConfig c;
        c.seed = static_cast<int>(gen->integer("seed", 1));
        c.vehicles = static_cast<int>(gen->integer("vehicles", 4));
        c.requests = static_cast<int>(gen->integer("requests", 40));
        c.dynamic_fraction = gen->num("dynamic_fraction", 0.4);
        c.window_min = gen->num("window_min", 30);
        c.closures = static_cast<int>(gen->integer("closures", 2));
        c.incidents = static_cast<int>(gen->integer("incidents", 1));
        c.t_start = gen->num("t_start", hours(7.5));
        c.shift_hours = gen->num("shift_hours", 5.5);
        c.radius_m = gen->num("radius_m", 0);
        if (c.vehicles < 1 || c.vehicles > 40) throw std::invalid_argument("vehicles must be 1..40");
        if (c.requests < 1 || c.requests > 300) throw std::invalid_argument("requests must be 1..300");
        return generate_world(ctx.g, tm, c);
    }
    w.t_start = req.num("t_start", hours(7.5));
    const JsonValue* vs = req.get("vehicles");
    const JsonValue* rs = req.get("requests");
    if (!vs || !rs) throw std::invalid_argument("give 'generate' or both 'vehicles' and 'requests'");
    int id = 0;
    for (const JsonValue& v : vs->items()) {
        Vehicle x;
        x.id = id++;
        x.depot = need_node(ctx.g, v, "depot");
        x.capacity = v.num("capacity", 10);
        x.shift_start = v.num("shift_start", w.t_start);
        x.shift_end = v.num("shift_end", w.t_start + hours(5.5));
        w.vehicles.push_back(x);
    }
    id = 0;
    for (const JsonValue& r : rs->items()) {
        Request x;
        x.id = id++;
        x.node = need_node(ctx.g, r, "node");
        x.demand = r.num("demand", 1);
        x.tw_start = r.num("tw_start", w.t_start);
        x.tw_end = r.num("tw_end", w.t_start + hours(4));
        x.service_s = r.num("service_s", 120);
        x.release_s = r.num("release_s", w.t_start);
        w.requests.push_back(x);
    }
    // Optional automatic windows / disruptions for hand-made worlds.
    if (const JsonValue* au = req.get("auto")) {
        WorldConfig c;
        c.seed = static_cast<int>(au->integer("seed", 1));
        c.window_min = au->num("window_min", 30);
        c.dynamic_fraction = au->num("dynamic_fraction", 0.0);
        c.closures = static_cast<int>(au->integer("closures", 0));
        c.incidents = static_cast<int>(au->integer("incidents", 0));
        finish_world(ctx.g, tm, w, c, au->boolean("windows", true), c.closures + c.incidents > 0);
    }
    if (const JsonValue* ev = req.get("events")) {
        for (const JsonValue& e : ev->items()) {
            DisruptionEvent d;
            d.time = e.num("time", w.t_start + 1800);
            d.until = e.num("until", d.time + 3600);
            d.closure = e.str("type", "closure") != "incident";
            d.factor = e.num("factor", 3.0);
            d.label = e.str("label", d.closure ? "road closure" : "incident");
            if (const JsonValue* roads = e.get("roads"))
                for (const JsonValue& r : roads->items())
                    if (r.items().size() == 2) d.roads.push_back({node_of(ctx.g, r.items()[0]), node_of(ctx.g, r.items()[1])});
            w.events.push_back(d);
        }
        std::sort(w.events.begin(), w.events.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    }
    return w;
}

void write_metrics(JsonWriter& j, const SimMetrics& m) {
    j.begin_object()
        .field("requests", m.requests).field("on_time", m.on_time).field("late", m.late).field("unserved", m.unserved)
        .field("success_rate", m.success_rate)
        .field("avg_response_s", m.avg_response_s).field("p95_response_s", m.p95_response_s)
        .field("total_distance_m", m.total_distance_m).field("driving_s", m.driving_s)
        .field("energy_kwh", m.energy_kwh).field("lateness_s", m.lateness_s)
        .field("vehicles_used", m.vehicles_used).field("cost", m.cost)
        .field("replans", m.replans).field("escalations", m.escalations)
        .field("total_latency_ms", m.total_latency_ms).field("mean_latency_ms", m.mean_latency_ms)
        .field("max_latency_ms", m.max_latency_ms).field("schedule_disruption", m.schedule_disruption)
        .field("initial_ms", m.initial_ms).field("makespan_s", m.makespan_s)
        .end_object();
}

std::string cmd_fleet(const EngineContext& ctx, const JsonValue& req) {
    std::unique_ptr<TrafficModel> own;
    const TrafficModel* tm = &ctx.tm;
    if (req.has("traffic")) {
        own = std::make_unique<TrafficModel>(ctx.g, parse_traffic(req.get("traffic"), ctx.tm.params()));
        tm = own.get();
    }
    const World w = parse_world(ctx, *tm, req);
    if (w.vehicles.empty() || w.requests.empty()) throw std::invalid_argument("need at least one vehicle and one request");
    if (w.requests.size() > 300 || w.vehicles.size() > 40) throw std::invalid_argument("instance too large");

    SimConfig cfg;
    const JsonValue* par = req.get("params");
    cfg.fp = parse_fleet_params(par, FleetParams{});
    if (par) {
        cfg.iters_per_request = static_cast<int>(par->integer("iters_per_request", cfg.iters_per_request));
        cfg.beta = par->num("beta", cfg.beta);
        cfg.selective_k = static_cast<int>(par->integer("selective_k", cfg.selective_k));
        cfg.scenario = static_cast<int>(par->integer("scenario", cfg.scenario));
    }
    std::vector<Strategy> strategies;
    if (const JsonValue* sv = req.get("strategies")) {
        for (const JsonValue& x : sv->items()) {
            Strategy s;
            if (!x.is_str() || !parse_strategy(x.s, s)) throw std::invalid_argument("unknown strategy");
            strategies.push_back(s);
        }
    }
    if (strategies.empty()) strategies = {Strategy::Static, Strategy::Greedy, Strategy::Full, Strategy::Selective};

    JsonWriter j;
    j.begin_object().field("ok", true);
    j.field("t_start", w.t_start);
    j.key("vehicles").begin_array();
    for (const Vehicle& v : w.vehicles)
        j.begin_object().field("id", v.id).field("depot", v.depot).field("capacity", v.capacity)
            .field("shift_start", v.shift_start).field("shift_end", v.shift_end).end_object();
    j.end_array();
    j.key("requests").begin_array();
    for (const Request& r : w.requests)
        j.begin_object().field("id", r.id).field("node", r.node).field("demand", r.demand).field("tw_start", r.tw_start)
            .field("tw_end", r.tw_end).field("service_s", r.service_s).field("release_s", r.release_s).end_object();
    j.end_array();
    j.key("events").begin_array();
    for (const DisruptionEvent& e : w.events) {
        j.begin_object().field("time", e.time).field("until", e.until).field("type", e.closure ? "closure" : "incident")
            .field("factor", e.factor).field("label", e.label);
        j.key("roads").begin_array();
        for (const auto& [u, v] : e.roads) j.begin_array().value(u).value(v).end_array();
        j.end_array().end_object();
    }
    j.end_array();

    j.key("results").begin_array();
    for (Strategy s : strategies) {
        cfg.strategy = s;
        const SimResult r = simulate(ctx.g, *tm, w, cfg);
        j.begin_object().field("strategy", to_string(s));
        j.key("metrics");
        write_metrics(j, r.m);
        j.field("initial_cost", r.initial_cost).field("initial_unassigned", r.initial_unassigned);
        j.key("initial_routes").begin_array();
        for (const auto& rt : r.initial_routes) j.array(rt);
        j.end_array();
        j.key("outcomes").begin_array();
        for (const RequestOutcome& o : r.outcomes)
            j.begin_object().field("id", o.id).field("served", o.served).field("on_time", o.on_time).field("vehicle", o.vehicle)
                .field("arrival_s", o.arrival).field("lateness_s", o.lateness).end_object();
        j.end_array();
        j.key("log").begin_array();
        for (const ReplanLog& l : r.log)
            j.begin_object().field("time", l.time).field("kind", l.kind).field("label", l.label)
                .field("vehicles", l.vehicles).field("requests", l.requests).field("latency_ms", l.latency_ms)
                .field("matrix_ms", l.matrix_ms).field("solve_ms", l.solve_ms).field("legs_ms", l.legs_ms)
                .field("disruption", l.disruption).field("escalated", l.escalated).field("replanned", l.replanned).end_object();
        j.end_array();
        j.key("trajectories").begin_array();
        for (const Trajectory& t : r.trajectories) {
            j.begin_object().field("vehicle", t.vid);
            j.key("nodes").array(t.nodes);
            j.key("times").begin_array();
            for (double x : t.times) j.value(std::round(x));
            j.end_array().end_object();
        }
        j.end_array().end_object();
    }
    j.end_array();

    const JsonValue* mc = req.get("mc");
    if (mc && mc->is_obj() && mc->num("scenarios", 0) > 0) {
        cfg.strategy = Strategy::Static;
        const int n = static_cast<int>(std::min(1000.0, mc->num("scenarios", 100)));
        const McPlanStats st = evaluate_plan_mc(ctx.g, *tm, w, cfg, n, mc->boolean("disruptions", false));
        j.key("mc").begin_object().field("scenarios", st.scenarios).field("on_time_rate", st.on_time_rate)
            .field("on_time_rate_p5", st.on_time_rate_p5).field("mean_makespan_s", st.mean_makespan_s)
            .field("p95_makespan_s", st.p95_makespan_s).field("mean_distance_m", st.mean_distance_m);
        j.key("per_request_on_time").array(st.per_request_on_time);
        j.end_object();
    }
    j.end_object();
    return j.str();
}

}  // namespace

std::string handle_json_command(const EngineContext& ctx, const std::string& line) {
    try {
        const JsonValue req = parse_json(line);
        if (!req.is_obj()) return error_json("request must be a JSON object");
        const std::string cmd = req.str("cmd", "");
        if (cmd == "traffic") return cmd_traffic(ctx, req);
        if (cmd == "td_route") return cmd_td_route(ctx, req);
        if (cmd == "replan_demo") return cmd_replan(ctx, req);
        if (cmd == "fleet") return cmd_fleet(ctx, req);
        if (cmd == "bench") return run_bench(ctx, req);
        return error_json("unknown cmd '" + cmd + "'");
    } catch (const std::exception& e) {
        return error_json(e.what());
    }
}

}  // namespace pathos

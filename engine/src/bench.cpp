// Experiments E1..E7 of the Pathos 2.0 evaluation plan. Every experiment returns raw rows (one JSON
// object per measurement); experiments/run_experiments.py aggregates them into tables.
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <random>

#include "pathos/commands.hpp"
#include "pathos/json.hpp"
#include "pathos/routing.hpp"
#include "pathos/sim.hpp"

namespace pathos {

namespace {

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Nodes that can both reach and be reached from a central node (so any pair is routable).
std::vector<NodeId> mutual_nodes(const Graph& g) {
    const std::size_t n = g.node_count();
    std::vector<std::vector<NodeId>> radj(n);
    for (std::size_t u = 0; u < n; ++u)
        for (const Edge& e : g.neighbors(static_cast<NodeId>(u))) radj[static_cast<std::size_t>(e.to)].push_back(static_cast<NodeId>(u));
    double s = 90, w = 180, nn = -90, e = -180;
    for (std::size_t i = 0; i < n; ++i) {
        const Node& nd = g.node(static_cast<NodeId>(i));
        s = std::min(s, nd.lat); nn = std::max(nn, nd.lat); w = std::min(w, nd.lon); e = std::max(e, nd.lon);
    }
    NodeId best = 0;
    double bd = kInf;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = haversine_m(g.node(static_cast<NodeId>(i)).lat, g.node(static_cast<NodeId>(i)).lon, (s + nn) / 2, (w + e) / 2);
        if (d < bd) { bd = d; best = static_cast<NodeId>(i); }
    }
    auto reach = [&](bool fwd) {
        std::vector<char> seen(n, 0);
        std::vector<NodeId> st{best};
        seen[static_cast<std::size_t>(best)] = 1;
        while (!st.empty()) {
            const NodeId u = st.back();
            st.pop_back();
            if (fwd) {
                for (const Edge& ed : g.neighbors(u))
                    if (!seen[static_cast<std::size_t>(ed.to)]) { seen[static_cast<std::size_t>(ed.to)] = 1; st.push_back(ed.to); }
            } else {
                for (NodeId v : radj[static_cast<std::size_t>(u)])
                    if (!seen[static_cast<std::size_t>(v)]) { seen[static_cast<std::size_t>(v)] = 1; st.push_back(v); }
            }
        }
        return seen;
    };
    const auto f = reach(true), b = reach(false);
    std::vector<NodeId> out;
    for (std::size_t i = 0; i < n; ++i)
        if (f[i] && b[i]) out.push_back(static_cast<NodeId>(i));
    return out;
}

std::vector<std::pair<NodeId, NodeId>> sample_pairs(const Graph& g, int count, int seed, double min_m, double max_m) {
    const std::vector<NodeId> nodes = mutual_nodes(g);
    std::vector<std::pair<NodeId, NodeId>> out;
    if (nodes.size() < 2) return out;
    std::mt19937_64 rng(static_cast<std::uint64_t>(seed) * 104729 + 7);
    std::uniform_int_distribution<std::size_t> pick(0, nodes.size() - 1);
    for (int tries = 0; tries < 20000 && static_cast<int>(out.size()) < count; ++tries) {
        const NodeId a = nodes[pick(rng)], b = nodes[pick(rng)];
        const double d = haversine_m(g.node(a).lat, g.node(a).lon, g.node(b).lat, g.node(b).lon);
        if (d >= min_m && d <= max_m) out.push_back({a, b});
    }
    return out;
}

// Pair distance range that fits the graph's extent.
std::pair<double, double> od_range(const Graph& g) {
    double s = 90, w = 180, nn = -90, e = -180;
    for (std::size_t i = 0; i < g.node_count(); ++i) {
        const Node& nd = g.node(static_cast<NodeId>(i));
        s = std::min(s, nd.lat); nn = std::max(nn, nd.lat); w = std::min(w, nd.lon); e = std::max(e, nd.lon);
    }
    const double diag = haversine_m(s, w, nn, e);
    return {0.25 * diag, 0.7 * diag};
}

double rss_mb() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0);
#else
    return static_cast<double>(ru.ru_maxrss) / 1024.0;
#endif
}

struct Rows {
    JsonWriter j;
    int n = 0;
    Rows() { j.begin_object().field("ok", true); }
};

void metrics_fields(JsonWriter& j, const SimMetrics& m) {
    j.field("requests", m.requests).field("on_time", m.on_time).field("late", m.late).field("unserved", m.unserved)
        .field("success_rate", m.success_rate).field("avg_response_s", m.avg_response_s)
        .field("p95_response_s", m.p95_response_s).field("distance_m", m.total_distance_m).field("driving_s", m.driving_s)
        .field("energy_kwh", m.energy_kwh).field("lateness_s", m.lateness_s).field("vehicles_used", m.vehicles_used)
        .field("cost", m.cost).field("replans", m.replans).field("escalations", m.escalations)
        .field("total_latency_ms", m.total_latency_ms).field("mean_latency_ms", m.mean_latency_ms)
        .field("max_latency_ms", m.max_latency_ms).field("schedule_disruption", m.schedule_disruption)
        .field("initial_ms", m.initial_ms).field("makespan_s", m.makespan_s);
}

WorldConfig base_world(int seed, const JsonValue& o) {
    WorldConfig c;
    c.seed = seed;
    c.vehicles = static_cast<int>(o.integer("vehicles", 3));
    c.requests = static_cast<int>(o.integer("requests", 45));
    c.dynamic_fraction = o.num("dynamic_fraction", 0.4);
    c.window_min = o.num("window_min", 25);
    c.closures = static_cast<int>(o.integer("closures", 3));
    c.incidents = static_cast<int>(o.integer("incidents", 2));
    return c;
}

}  // namespace

std::string run_bench(const EngineContext& ctx, const JsonValue& req) {
    const std::string exp = req.str("exp", "");
    const int seeds = static_cast<int>(req.integer("seeds", 5));
    const Graph& g = ctx.g;
    const TrafficModel& tm = ctx.tm;
    const auto [dmin, dmax] = od_range(g);
    Rows R;
    JsonWriter& j = R.j;
    j.field("experiment", exp).field("graph", g.version()).field("nodes", static_cast<unsigned long long>(g.node_count()))
        .field("edges", static_cast<unsigned long long>(g.edge_count()));
    j.key("rows").begin_array();
    const Disruptions none(tm.edge_count());
    const Net clean{&g, &tm, &none};

    if (exp == "E1") {  // static vs time-dependent traffic
        const int pairs = static_cast<int>(req.integer("pairs", 40));
        const auto ods = sample_pairs(g, pairs, 1, dmin, dmax);
        for (const auto& [s, t] : ods) {
            for (double dep : {hours(3), hours(8), hours(13), hours(18)}) {
                EvalMode ffm;
                ffm.freeflow = true;
                const TdResult ff = td_route(clean, s, t, dep, ffm, true);
                const TdResult td = td_route(clean, s, t, dep, EvalMode{}, true);
                if (!ff.found || !td.found) continue;
                const double actual = walk_path(clean, ff.path, 0, dep, EvalMode{}) - dep;
                j.begin_object().field("depart_h", dep / 3600.0).field("free_pred_s", ff.travel_s).field("free_actual_s", actual)
                    .field("td_opt_s", td.travel_s).field("regret_pct", 100.0 * (actual - td.travel_s) / td.travel_s)
                    .field("eta_error_pct", 100.0 * (actual - ff.travel_s) / ff.travel_s)
                    .field("same_route", ff.path.nodes == td.path.nodes).field("dist_ff_m", ff.distance_m)
                    .field("dist_td_m", td.distance_m).end_object();
                ++R.n;
            }
        }
    } else if (exp == "E2" || exp == "E6R") {  // uncertainty: deadline reliability (E2) and the alpha trade-off (E6)
        const int pairs = static_cast<int>(req.integer("pairs", 20));
        const auto ods = sample_pairs(g, pairs, 2, dmin, dmax);
        const std::vector<double> slacks = exp == "E2" ? std::vector<double>{1.05, 1.15, 1.30} : std::vector<double>{1.10};
        const std::vector<double> alphas = exp == "E2" ? std::vector<double>{0.1} : std::vector<double>{0.3, 0.2, 0.1, 0.05, 0.01};
        for (const auto& [s, t] : ods) {
            const TdResult ex = td_route(clean, s, t, hours(8), EvalMode{}, true);
            if (!ex.found) continue;
            for (double slack : slacks)
                for (double alpha : alphas) {
                    ReliabilityQuery q;
                    q.source = s; q.target = t; q.depart = hours(8); q.alpha = alpha;
                    q.deadline_s = slack * ex.travel_s;
                    q.scenarios = static_cast<int>(req.integer("scenarios", 150));
                    q.candidate_scenarios = 8;
                    const ReliabilityResult r = reliability_routes(clean, q);
                    if (r.candidates.empty()) continue;
                    const RouteStats& e = r.candidates[static_cast<std::size_t>(r.expected)];
                    const RouteStats& b = r.candidates[static_cast<std::size_t>(r.robust)];
                    const RouteStats& rl = r.candidates[static_cast<std::size_t>(r.reliable)];
                    const RouteStats& f = r.candidates[static_cast<std::size_t>(r.freeflow)];
                    j.begin_object().field("slack", slack).field("alpha", alpha).field("feasible", r.reliable_feasible)
                        .field("p_free", f.p_on_time).field("p_expected", e.p_on_time).field("p_robust", b.p_on_time)
                        .field("p_reliable", rl.p_on_time).field("mean_expected_s", e.mc_mean).field("mean_robust_s", b.mc_mean)
                        .field("mean_reliable_s", rl.mc_mean).field("p95_expected_s", e.mc_p95).field("p95_robust_s", b.mc_p95)
                        .field("p95_reliable_s", rl.mc_p95).field("dist_expected_m", e.distance_m).field("dist_reliable_m", rl.distance_m)
                        .field("candidates", static_cast<unsigned long long>(r.candidates.size()))
                        .field("same_as_expected", rl.path.nodes == e.path.nodes).end_object();
                    ++R.n;
                }
        }
    } else if (exp == "E3") {  // normal network vs disruption; single vehicle, plus the selective-vs-full comparison (E4a)
        const int pairs = static_cast<int>(req.integer("pairs", 40));
        const auto ods = sample_pairs(g, pairs, 3, dmin, dmax);
        for (const auto& [s, t] : ods) {
            for (int kind = 0; kind < 2; ++kind) {
                for (int lookback : {0, 5, 15}) {  // how far before the disruption a detour may leave the route
                    ReplanOptions o;
                    o.source = s; o.target = t; o.closure = kind == 0; o.factor = 4.0;
                    o.edges_ahead = 8; o.duration_s = 3600; o.repeat = 5; o.lookback = lookback;
                    const ReplanOutcome r = run_replan_case(g, tm, o);
                    if (!r.ok || r.original.edges() < 25) continue;
                    j.begin_object().field("type", kind == 0 ? "closure" : "incident").field("lookback", lookback)
                        .field("route_edges", static_cast<unsigned long long>(r.original.edges()))
                        .field("planned_s", r.planned_travel)
                        .field("keep_s", r.keep.arrival - o.depart).field("full_s", r.full.arrival - o.depart)
                        .field("selective_s", r.selective.arrival - o.depart)
                        .field("full_ms", r.full.duration_ms).field("selective_ms", r.selective.duration_ms)
                        .field("full_expanded", static_cast<unsigned long long>(r.full.expanded))
                        .field("selective_expanded", static_cast<unsigned long long>(r.selective.expanded))
                        .field("selective_affected", r.selective.affected).field("selective_escalated", r.selective.escalated)
                        .field("full_changed", r.full.changed).field("selective_changed", r.selective.changed).end_object();
                    ++R.n;
                }
            }
        }
    } else if (exp == "E4") {  // fleet replanning strategies (the research question), with ablations
        struct Variant { std::string name; Strategy s; double beta; int k; };
        const std::vector<Variant> variants = {
            {"static", Strategy::Static, 0, 3},        {"greedy", Strategy::Greedy, 0, 3},
            {"full", Strategy::Full, 0, 3},            {"selective", Strategy::Selective, 120, 3},
            {"selective_beta0", Strategy::Selective, 0, 3}, {"selective_beta600", Strategy::Selective, 600, 3},
            {"selective_k2", Strategy::Selective, 120, 2}};
        for (int seed = 1; seed <= seeds; ++seed) {
            const WorldConfig wc = base_world(seed, req);
            const World w = generate_world(g, tm, wc);
            for (const Variant& v : variants) {
                if (req.boolean("core_only", false) && v.name.rfind("selective_", 0) == 0) continue;
                SimConfig c;
                c.strategy = v.s; c.beta = v.beta; c.selective_k = v.k; c.scenario = seed;
                c.iters_per_request = static_cast<int>(req.integer("iters_per_request", 25));
                const SimResult r = simulate(g, tm, w, c);
                j.begin_object().field("seed", seed).field("variant", v.name).field("events", static_cast<unsigned long long>(w.events.size()));
                metrics_fields(j, r.m);
                j.end_object();
                ++R.n;
            }
        }
    } else if (exp == "E5") {  // single vs multiple vehicles
        for (int seed = 1; seed <= seeds; ++seed)
            for (int nv : {1, 2, 3, 4, 6, 8}) {
                WorldConfig wc = base_world(seed, req);
                wc.vehicles = nv; wc.dynamic_fraction = 0; wc.closures = 0; wc.incidents = 0;
                wc.requests = static_cast<int>(req.integer("requests", 36));
                const World w = generate_world(g, tm, wc);
                SimConfig c;
                c.strategy = Strategy::Static; c.scenario = -1;
                c.iters_per_request = static_cast<int>(req.integer("iters_per_request", 40));
                const SimResult r = simulate(g, tm, w, c);
                j.begin_object().field("seed", seed).field("fleet", nv);
                metrics_fields(j, r.m);
                j.end_object();
                ++R.n;
            }
    } else if (exp == "E6F") {  // fleet plans with a reliability buffer kappa
        for (int seed = 1; seed <= seeds; ++seed)
            for (double kappa : {0.0, 1.0, 2.0, 3.0}) {
                WorldConfig wc = base_world(seed, req);
                wc.dynamic_fraction = 0; wc.closures = 0; wc.incidents = 0;
                wc.requests = static_cast<int>(req.integer("requests", 30));
                const World w = generate_world(g, tm, wc);
                SimConfig c;
                c.strategy = Strategy::Static;
                c.fp.kappa = kappa;
                c.iters_per_request = static_cast<int>(req.integer("iters_per_request", 40));
                const McPlanStats mc = evaluate_plan_mc(g, tm, w, c, static_cast<int>(req.integer("scenarios", 100)), false);
                const SimResult plan = simulate(g, tm, w, c);
                j.begin_object().field("seed", seed).field("kappa", kappa).field("planned_unassigned", plan.initial_unassigned)
                    .field("plan_cost", plan.initial_cost).field("vehicles_used", plan.m.vehicles_used)
                    .field("on_time_rate", mc.on_time_rate).field("on_time_p5", mc.on_time_rate_p5)
                    .field("mean_makespan_s", mc.mean_makespan_s).field("mean_distance_m", mc.mean_distance_m).end_object();
                ++R.n;
            }
    } else if (exp == "E7") {  // scalability
        const auto ods = sample_pairs(g, 30, 7, dmin, dmax);
        double dij = 0, ast = 0, tdd = 0, tda = 0, ex_d = 0, ex_a = 0, ex_td = 0, ex_ta = 0;
        int cnt = 0;
        for (const auto& [s, t] : ods) {
            SearchOptions so;
            so.weight = Weight::Time;
            const RouteResult a = dijkstra(g, s, t, so), b = astar(g, s, t, so);
            const TdResult c = td_route(clean, s, t, hours(8), EvalMode{}, false), d = td_route(clean, s, t, hours(8), EvalMode{}, true);
            if (!a.found || !c.found) continue;
            dij += a.duration_ms; ast += b.duration_ms; tdd += c.duration_ms; tda += d.duration_ms;
            ex_d += static_cast<double>(a.expanded); ex_a += static_cast<double>(b.expanded);
            ex_td += static_cast<double>(c.expanded); ex_ta += static_cast<double>(d.expanded);
            ++cnt;
        }
        cnt = std::max(cnt, 1);
        j.begin_object().field("kind", "query").field("pairs", cnt).field("dijkstra_ms", dij / cnt).field("astar_ms", ast / cnt)
            .field("td_dijkstra_ms", tdd / cnt).field("td_astar_ms", tda / cnt).field("dijkstra_expanded", ex_d / cnt)
            .field("astar_expanded", ex_a / cnt).field("td_dijkstra_expanded", ex_td / cnt).field("td_astar_expanded", ex_ta / cnt)
            .field("rss_mb", rss_mb()).end_object();
        ++R.n;
        for (int nreq : {10, 20, 40, 80}) {
            if (nreq > req.integer("max_requests", 80)) break;
            WorldConfig wc;
            wc.seed = 1; wc.requests = nreq; wc.vehicles = std::max(2, nreq / 8); wc.dynamic_fraction = 0; wc.closures = 0; wc.incidents = 0;
            const World w = generate_world(g, tm, wc);
            Problem P = make_problem(w.vehicles, w.requests, w.t_start);
            FleetParams fp;
            PlanStats st;
            const auto t0 = Clock::now();
            const Solution sol = plan_problem(clean, P, fp, PlanMode::Scratch, nreq * 30, &st);
            j.begin_object().field("kind", "fleet").field("requests", nreq).field("vehicles", static_cast<int>(w.vehicles.size()))
                .field("matrix_nodes", st.matrix_nodes).field("matrix_searches", st.matrix_searches).field("matrix_ms", st.matrix_ms)
                .field("solve_ms", st.solve_ms).field("total_ms", ms_since(t0)).field("iterations", st.iterations)
                .field("unassigned", sol.unassigned()).field("rss_mb", rss_mb()).end_object();
            ++R.n;
        }
    } else if (exp == "GAP") {  // optimality gap: brute force on tiny instances, long ALNS on larger ones
        for (int seed = 1; seed <= seeds; ++seed) {
            WorldConfig wc;
            wc.seed = seed; wc.requests = 7; wc.vehicles = 2; wc.dynamic_fraction = 0; wc.closures = 0; wc.incidents = 0; wc.window_min = 120;
            const World w = generate_world(g, tm, wc);
            Problem P = make_problem(w.vehicles, w.requests, w.t_start);
            FleetParams fp;
            TdMatrix M;
            build_matrix(clean, P, fp, M);
            const Solution opt = brute_force(P, M, fp);
            PlanStats st;
            const Solution heur = plan_problem(clean, P, fp, PlanMode::Scratch, 400, &st);
            if (opt.cost < kInf)
                j.begin_object().field("kind", "exact").field("seed", seed).field("optimal", opt.cost).field("alns", heur.cost)
                    .field("greedy", st.initial_cost).field("gap_pct", 100.0 * (heur.cost - opt.cost) / opt.cost).end_object();
            ++R.n;
        }
        for (int seed = 1; seed <= seeds; ++seed) {
            WorldConfig wc;
            wc.seed = seed; wc.requests = 40; wc.vehicles = 5; wc.dynamic_fraction = 0; wc.closures = 0; wc.incidents = 0;
            const World w = generate_world(g, tm, wc);
            FleetParams fp;
            double cost[3] = {0, 0, 0};
            const int iters[3] = {0, 1200, 12000};
            for (int k = 0; k < 3; ++k) {
                Problem P = make_problem(w.vehicles, w.requests, w.t_start);
                PlanStats st;
                const Solution s = plan_problem(clean, P, fp, PlanMode::Scratch, iters[k], &st);
                cost[k] = s.cost;
            }
            j.begin_object().field("kind", "reference").field("seed", seed).field("greedy", cost[0]).field("alns", cost[1])
                .field("reference", cost[2]).field("gap_pct", 100.0 * (cost[1] - cost[2]) / cost[2])
                .field("greedy_gap_pct", 100.0 * (cost[0] - cost[2]) / cost[2]).end_object();
            ++R.n;
        }
    } else {
        j.end_array().end_object();
        JsonWriter e;
        e.begin_object().field("ok", false).field("error", "unknown experiment '" + exp + "' (E1 E2 E3 E4 E5 E6R E6F E7 GAP)").end_object();
        return e.str();
    }
    j.end_array().end_object();
    return j.str();
}

}  // namespace pathos

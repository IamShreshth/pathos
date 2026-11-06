// Unit tests for the pathos engine (no external framework needed).
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <string>

#include "pathos/commands.hpp"
#include "pathos/fleet.hpp"
#include "pathos/graph.hpp"
#include "pathos/jsonparse.hpp"
#include "pathos/routing.hpp"
#include "pathos/sim.hpp"
#include "pathos/tdroute.hpp"
#include "pathos/traffic.hpp"
#include "pathos/tsp.hpp"

using namespace pathos;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failures;                                                             \
            std::cerr << __FILE__ << ":" << __LINE__ << "  CHECK failed: " #cond "\n"; \
        }                                                                             \
    } while (0)

#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 1e-6 * std::max(1.0, std::fabs(b)))

// Small graph laid out on a ~100 m lattice so the haversine heuristic is meaningful.
//
//   0 --100-- 1 --100-- 2
//   |                   |
//  100                 100
//   |                   |
//   3 --------500------ 4     plus a long direct edge 0 -> 4 of 1000 m
static Graph diamond() {
    Graph g;
    const double d = 0.0009;  // ~100 m of latitude
    g.add_node(0, 0);        // 0
    g.add_node(0, d);        // 1
    g.add_node(0, 2 * d);    // 2
    g.add_node(-d, 0);       // 3
    g.add_node(-d, 2 * d);   // 4
    auto both = [&](int u, int v, double w) { g.add_edge(u, v, w); g.add_edge(v, u, w); };
    both(0, 1, 100);
    both(1, 2, 100);
    both(2, 4, 100);
    both(0, 3, 100);
    both(3, 4, 500);
    g.add_edge(0, 4, 1000);  // one-way shortcut in hops, long in metres
    return g;
}

static void test_basic_routes() {
    Graph g = diamond();
    SearchOptions opt;

    RouteResult b = bfs(g, 0, 4, opt);
    CHECK(b.found);
    CHECK(b.path.size() == 2);  // direct one-way edge: fewest segments
    CHECK_NEAR(b.cost, 1.0);
    CHECK_NEAR(b.distance_m, 1000.0);

    RouteResult d = dijkstra(g, 0, 4, opt);
    CHECK(d.found);
    CHECK_NEAR(d.cost, 300.0);
    CHECK((d.path == std::vector<NodeId>{0, 1, 2, 4}));

    RouteResult a = astar(g, 0, 4, opt);
    CHECK(a.found);
    CHECK_NEAR(a.cost, 300.0);
    CHECK(a.path == d.path);
    CHECK(a.expanded <= d.expanded);
}

static void test_one_way_and_unreachable() {
    Graph g = diamond();
    SearchOptions opt;
    // 4 -> 0 must not use the one-way 0 -> 4 shortcut.
    RouteResult r = dijkstra(g, 4, 0, opt);
    CHECK(r.found);
    CHECK_NEAR(r.cost, 300.0);

    // Add an isolated node: unreachable from everything.
    g.add_node(1, 1);
    for (Algorithm algo : {Algorithm::BFS, Algorithm::Dijkstra, Algorithm::AStar}) {
        RouteResult u = run_route(algo, g, 0, 5, opt);
        CHECK(!u.found);
        CHECK(u.path.empty());
    }
}

static void test_source_equals_target() {
    Graph g = diamond();
    SearchOptions opt;
    for (Algorithm algo : {Algorithm::BFS, Algorithm::Dijkstra, Algorithm::AStar}) {
        RouteResult r = run_route(algo, g, 2, 2, opt);
        CHECK(r.found);
        CHECK(r.path.size() == 1);
        CHECK_NEAR(r.cost, 0.0);
    }
}

static void test_trace() {
    Graph g = diamond();
    SearchOptions opt;
    opt.record_trace = true;
    RouteResult r = dijkstra(g, 0, 4, opt);
    CHECK(r.trace.size() == r.expanded);
    CHECK(r.trace.front().node == 0 && r.trace.front().parent == kNoNode);
    CHECK(r.trace.back().node == 4);
    opt.max_trace_events = 2;
    RouteResult t = dijkstra(g, 0, 4, opt);
    CHECK(t.trace.size() == 2);
    CHECK(t.trace_truncated);
}

// Random directed geometric graph: Dijkstra and A* must agree on every query,
// and BFS must never use more segments than they do.
static void test_random_agreement() {
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> coord(0.0, 0.05);
    Graph g;
    const int n = 400;
    for (int i = 0; i < n; ++i) g.add_node(26.9 + coord(rng), 75.8 + coord(rng));
    std::uniform_int_distribution<int> pick(0, n - 1);
    std::uniform_real_distribution<double> detour(1.0, 1.6);
    for (int i = 0; i < n * 4; ++i) {
        int u = pick(rng), v = pick(rng);
        if (u == v) continue;
        const Node& a = g.node(u);
        const Node& b = g.node(v);
        double len = haversine_m(a.lat, a.lon, b.lat, b.lon) * detour(rng);  // roads are never shorter
        g.add_edge(u, v, len, len / (5 + 20 * detour(rng)));
    }
    for (Weight w : {Weight::Distance, Weight::Time}) {
        SearchOptions opt;
        opt.weight = w;
        for (int q = 0; q < 200; ++q) {
            int s = pick(rng), t = pick(rng);
            RouteResult d = dijkstra(g, s, t, opt);
            RouteResult a = astar(g, s, t, opt);
            RouteResult b = bfs(g, s, t, opt);
            CHECK(d.found == a.found);
            CHECK(d.found == b.found);
            if (d.found) {
                CHECK_NEAR(a.cost, d.cost);
                CHECK(b.path.size() <= d.path.size());
            }
        }
    }
}

static bool is_permutation_from_zero(const Tour& t, int n) {
    if (static_cast<int>(t.size()) != n || t.empty() || t[0] != 0) return false;
    std::set<int> s(t.begin(), t.end());
    return static_cast<int>(s.size()) == n && *s.begin() == 0 && *s.rbegin() == n - 1;
}

static void test_tsp_heuristics() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(0, 1000), skew(1.0, 1.3);
    for (int trial = 0; trial < 50; ++trial) {
        const int n = 4 + trial % 8;  // 4 .. 11 stops
        std::vector<std::pair<double, double>> p(n);
        for (auto& q : p) q = {u(rng), u(rng)};
        Matrix m(n, std::vector<double>(n, 0));
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (i != j) m[i][j] = std::hypot(p[i].first - p[j].first, p[i].second - p[j].second) * skew(rng);

        Tour t = nearest_neighbor_tour(m, 0);
        CHECK(is_permutation_from_zero(t, n));
        const double before = tour_cost(m, t);
        two_opt(m, t);
        CHECK(is_permutation_from_zero(t, n));
        const double after = tour_cost(m, t);
        CHECK(after <= before + 1e-9);

        Tour best;
        auto opt = held_karp(m, &best);
        CHECK(opt.has_value());
        CHECK(is_permutation_from_zero(best, n));
        CHECK_NEAR(tour_cost(m, best), *opt);
        CHECK(*opt <= after + 1e-6);  // exact optimum is never worse than the heuristic
    }
}

static void test_tour_on_graph() {
    Graph g = diamond();
    TourResult r = solve_tour(g, {0, 2, 3, 4}, Weight::Distance);
    CHECK(r.feasible);
    CHECK(r.order.size() == 4 && r.order[0] == 0);
    CHECK(r.legs.size() == 4);
    // Optimal loop around the square: 0-1-2-4-3-0 = 800 m.
    CHECK_NEAR(r.cost, 800.0);
    CHECK(r.optimal_cost.has_value());
    CHECK_NEAR(*r.optimal_cost, 800.0);
    for (const TourLeg& l : r.legs) {
        CHECK(l.reachable);
        CHECK_NEAR(l.distance_m, l.cost);
    }

    g.add_node(5, 5);  // isolated stop
    TourResult bad = solve_tour(g, {0, 2, 5}, Weight::Distance);
    CHECK(!bad.feasible);
    int unreachable = 0;
    for (const TourLeg& l : bad.legs) unreachable += !l.reachable;
    CHECK(unreachable == 2);  // leg into stop 5 and leg out of it
}

static void test_asymmetric_two_opt() {
    // Directed cycle 0->1->2->3->0 is cheap, the reverse direction expensive.
    const int n = 4;
    Matrix m(n, std::vector<double>(n, 100));
    for (int i = 0; i < n; ++i) m[i][i] = 0;
    for (int i = 0; i < n; ++i) m[i][(i + 1) % n] = 1;
    Tour t = {0, 3, 2, 1};  // the expensive direction
    two_opt(m, t);
    CHECK((t == Tour{0, 1, 2, 3}));
    CHECK_NEAR(tour_cost(m, t), 4.0);
}


// ======================================================================
// Pathos 2.0: time-dependent traffic, uncertainty, replanning, fleet
// ======================================================================

// w x h grid, two-way roads, ~100 m spacing with random speeds (so there are many alternatives).
static Graph make_grid(int w, int h, unsigned seed) {
    Graph g;
    std::mt19937 rng(seed);
    const double d = 0.0009;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) g.add_node(26.9 + y * d, 75.8 + x * d);
    auto id = [&](int x, int y) { return y * w + x; };
    auto both = [&](int a, int b) {
        const double len = 100.0 * (0.9 + 0.4 * std::uniform_real_distribution<double>(0, 1)(rng));
        const double speed = 6.0 + 10.0 * std::uniform_real_distribution<double>(0, 1)(rng);
        g.add_edge(a, b, len, len / speed);
        g.add_edge(b, a, len, len / speed);
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (x + 1 < w) both(id(x, y), id(x + 1, y));
            if (y + 1 < h) both(id(x, y), id(x, y + 1));
        }
    return g;
}

static void test_json_parser() {
    const JsonValue v = parse_json(R"({"a":1.5,"b":[1,2,{"c":"x\"y"}],"t":true,"n":null,"neg":-2e2})");
    CHECK(v.is_obj());
    CHECK_NEAR(v.num("a", 0), 1.5);
    CHECK(v.get("b")->items().size() == 3);
    CHECK(v.get("b")->items()[2].str("c", "") == "x\"y");
    CHECK(v.boolean("t", false));
    CHECK(v.get("n")->is_null());
    CHECK_NEAR(v.num("neg", 0), -200.0);
    bool threw = false;
    try { parse_json("{\"a\":"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

static void test_traffic_basics() {
    Graph g = make_grid(8, 8, 1);
    TrafficModel tm(g);
    // Congestion is >= 1, peaks in the morning and is free at night.
    for (int eid = 0; eid < static_cast<int>(tm.edge_count()); eid += 7) {
        CHECK(tm.congestion(eid, hours(3)) <= tm.congestion(eid, hours(8)) + 1e-12);
        CHECK(tm.congestion(eid, hours(8)) >= 1.0);
        CHECK_NEAR(tm.congestion(eid, hours(3)), 1.0);
    }
    TrafficParams off;
    off.intensity = 0;
    TrafficModel flat(g, off);
    CHECK_NEAR(flat.congestion(5, hours(8)), 1.0);

    // FIFO: leaving later never means arriving earlier (also with closures and incidents).
    Disruptions dis(tm.edge_count());
    dis.add_closure(10, 1000, 1800);
    dis.add_slowdown(11, 500, 4000, 3.0);
    Net net{&g, &tm, &dis};
    std::mt19937 rng(3);
    for (int trial = 0; trial < 400; ++trial) {
        const NodeId u = static_cast<NodeId>(rng() % g.node_count());
        const auto& adj = g.neighbors(u);
        const int k = static_cast<int>(rng() % adj.size());
        double prev = -1;
        for (double t = 0; t < hours(24); t += 97.0) {
            const double a = net.arrive(u, k, t, EvalMode{});
            CHECK(a >= t);
            CHECK(a >= prev - 1e-9);
            prev = a;
        }
    }
    // A closure makes a vehicle wait until it ends.
    const NodeId tail = tm.tail_of(10);
    const int k = 10 - tm.first_edge(tail);
    CHECK(net.arrive(tail, k, 1200, EvalMode{}) >= 1800.0);
}

static void test_noise() {
    Graph g = make_grid(6, 6, 2);
    TrafficModel tm(g);
    const double cv = 0.2;
    double s = 0, s2 = 0, cross = 0;
    const int n = 20000;
    for (int sc = 0; sc < n; ++sc) {
        const double a = tm.noise(3, static_cast<std::uint32_t>(sc), cv), b = tm.noise(4, static_cast<std::uint32_t>(sc), cv);
        CHECK(a > 0);
        s += a;
        s2 += a * a;
        cross += (a - 1) * (b - 1);
    }
    CHECK(std::fabs(s / n - 1.0) < 0.01);                          // mean one: expected times stay expected
    const double sd = std::sqrt(s2 / n - (s / n) * (s / n));
    CHECK(sd > 0.15 && sd < 0.25);                                  // about cv
    CHECK(cross / n > 0.0);                                         // edges are positively correlated
    CHECK_NEAR(tm.noise(3, 77, cv), tm.noise(3, 77, cv));           // deterministic
    CHECK_NEAR(normal_quantile(0.5), 0.0);
    CHECK(std::fabs(normal_quantile(0.95) - 1.644854) < 1e-4);
    CHECK(std::fabs(normal_quantile(0.025) + 1.959964) < 1e-4);
}

// Reference earliest arrival by repeated relaxation (Bellman-Ford style; valid because edges are FIFO).
static std::vector<double> reference_arrivals(const Net& net, NodeId s, double t0) {
    const std::size_t n = net.g->node_count();
    std::vector<double> best(n, kInf);
    best[static_cast<std::size_t>(s)] = t0;
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t u = 0; u < n; ++u) {
            if (best[u] == kInf) continue;
            const auto& adj = net.g->neighbors(static_cast<NodeId>(u));
            for (std::size_t k = 0; k < adj.size(); ++k) {
                const double a = net.arrive(static_cast<NodeId>(u), static_cast<int>(k), best[u], EvalMode{});
                if (a < best[static_cast<std::size_t>(adj[k].to)] - 1e-9) {
                    best[static_cast<std::size_t>(adj[k].to)] = a;
                    changed = true;
                }
            }
        }
    }
    return best;
}

static void test_td_routing() {
    Graph g = make_grid(12, 12, 5);
    TrafficModel tm(g);
    Disruptions dis(tm.edge_count());
    std::mt19937 rng(9);
    for (int i = 0; i < 12; ++i) dis.add_closure(static_cast<int>(rng() % tm.edge_count()), 25000, 31000);
    dis.add_slowdown(7, 20000, 40000, 3.0);
    Net net{&g, &tm, &dis};

    // With no traffic the time-dependent search equals the static time-weighted Dijkstra.
    TrafficParams off;
    off.intensity = 0;
    TrafficModel flat(g, off);
    const Disruptions none(flat.edge_count());
    Net fnet{&g, &flat, &none};
    SearchOptions so;
    so.weight = Weight::Time;
    for (int i = 0; i < 20; ++i) {
        const NodeId s = static_cast<NodeId>(rng() % g.node_count()), t = static_cast<NodeId>(rng() % g.node_count());
        const RouteResult ref = dijkstra(g, s, t, so);
        const TdResult td = td_route(fnet, s, t, 5000, EvalMode{}, false);
        CHECK(td.found);
        CHECK_NEAR(td.travel_s, ref.cost);
    }
    // Under peak traffic and disruptions: Dijkstra == A* == reference relaxation; paths are consistent.
    for (int i = 0; i < 25; ++i) {
        const NodeId s = static_cast<NodeId>(rng() % g.node_count()), t = static_cast<NodeId>(rng() % g.node_count());
        const double dep = 24000 + 100.0 * (rng() % 100);
        const auto ref = reference_arrivals(net, s, dep);
        const TdResult a = td_route(net, s, t, dep, EvalMode{}, false);
        const TdResult b = td_route(net, s, t, dep, EvalMode{}, true);
        CHECK(a.found && b.found);
        CHECK_NEAR(a.arrival, ref[static_cast<std::size_t>(t)]);
        CHECK_NEAR(b.arrival, a.arrival);
        CHECK(b.expanded <= a.expanded);
        CHECK(a.path.nodes.front() == s && a.path.nodes.back() == t);
        CHECK_NEAR(walk_path(net, a.path, 0, dep, EvalMode{}), a.arrival);  // the path reproduces its own cost
        CHECK(path_length_m(g, a.path) > -1);
    }
}

static void test_closure_detour() {
    // Two parallel routes 0-1-2 and 0-3-2; closing the shorter one forever forces the detour.
    Graph g;
    g.add_node(0, 0); g.add_node(0, 0.001); g.add_node(0, 0.002); g.add_node(0.001, 0.001);
    auto both = [&](int a, int b, double len) { g.add_edge(a, b, len, len / 10); g.add_edge(b, a, len, len / 10); };
    both(0, 1, 100); both(1, 2, 100); both(0, 3, 150); both(3, 2, 150);
    TrafficParams off;
    off.intensity = 0;
    TrafficModel tm(g, off);
    Disruptions dis(tm.edge_count());
    const int k = find_edge(g, 1, 2);
    dis.add_closure(tm.first_edge(1) + k, 0, kInf);
    Net net{&g, &tm, &dis};
    const TdResult r = td_route(net, 0, 2, 0, EvalMode{}, true);
    CHECK(r.found);
    CHECK((r.path.nodes == std::vector<NodeId>{0, 3, 2}));
    CHECK_NEAR(r.travel_s, 30.0);
    // Closed only for an hour: waiting (110 s... 3600 s) is worse than the detour.
    Disruptions timed(tm.edge_count());
    timed.add_closure(tm.first_edge(1) + k, 0, 3600);
    Net tnet{&g, &tm, &timed};
    CHECK_NEAR(td_route(tnet, 0, 2, 0, EvalMode{}, true).travel_s, 30.0);
    // With no detour at all (cut both alternatives) the vehicle waits for the reopening.
    Disruptions both_closed(tm.edge_count());
    both_closed.add_closure(tm.first_edge(1) + k, 0, 3600);
    both_closed.add_closure(tm.first_edge(3) + find_edge(g, 3, 2), 0, 3000);
    Net wnet{&g, &tm, &both_closed};
    CHECK_NEAR(td_route(wnet, 0, 2, 0, EvalMode{}, true).arrival, 3000 + 15.0);
}

static void test_reliability() {
    Graph g = make_grid(14, 14, 11);
    TrafficModel tm(g);
    const Disruptions none(tm.edge_count());
    Net net{&g, &tm, &none};
    ReliabilityQuery q;
    q.source = 3;
    q.target = static_cast<NodeId>(g.node_count() - 5);
    q.depart = hours(8);
    q.scenarios = 200;
    q.alpha = 0.1;
    const ReliabilityResult r = reliability_routes(net, q);
    CHECK(!r.candidates.empty());
    CHECK(r.freeflow >= 0 && r.expected >= 0 && r.robust >= 0 && r.reliable >= 0);
    double best_p = 0;
    for (const RouteStats& c : r.candidates) {
        CHECK(c.p_on_time >= 0 && c.p_on_time <= 1);
        CHECK(c.mc_p50 <= c.mc_p95 + 1e-9 && c.mc_p95 <= c.mc_worst + 1e-9);
        CHECK(c.expected_s >= c.free_s - 1e-6);  // traffic never speeds a route up
        best_p = std::max(best_p, c.p_on_time);
    }
    const RouteStats& rel = r.candidates[static_cast<std::size_t>(r.reliable)];
    if (r.reliable_feasible) CHECK(rel.p_on_time >= 1.0 - q.alpha - 1e-12);
    else CHECK_NEAR(rel.p_on_time, best_p);
    // The expected-time route is the fastest in expectation (up to Monte Carlo noise).
    const RouteStats& ex = r.candidates[static_cast<std::size_t>(r.expected)];
    for (const RouteStats& c : r.candidates) CHECK(ex.expected_s <= c.expected_s + 1e-6);
    // Generous deadline: every route is reliable and the fastest one wins.
    q.deadline_s = 3.0 * ex.expected_s;
    const ReliabilityResult easy = reliability_routes(net, q);
    CHECK(easy.reliable_feasible);
}

static void test_local_repair() {
    Graph g = make_grid(14, 14, 13);
    TrafficModel tm(g);
    const Disruptions none(tm.edge_count());
    Net base{&g, &tm, &none};
    int repaired = 0;
    for (int trial = 0; trial < 20; ++trial) {
        const NodeId s = static_cast<NodeId>((trial * 7) % 30), t = static_cast<NodeId>(g.node_count() - 1 - (trial * 11) % 30);
        const TdResult p = td_route(base, s, t, hours(8), EvalMode{}, true);
        CHECK(p.found && p.path.edges() > 10);
        const std::size_t idx = p.path.edges() / 2;
        Disruptions fresh(tm.edge_count());
        fresh.add_closure(tm.first_edge(p.path.nodes[idx]) + p.path.ks[idx], hours(8), hours(8) + 3600);
        Net after{&g, &tm, &fresh};
        const RepairResult rep = local_repair(after, fresh, p.path, hours(8));
        CHECK(rep.affected && rep.repaired);
        const TdResult full = td_route(after, s, t, hours(8), EvalMode{}, true);
        const double keep = walk_path(after, p.path, 0, hours(8), EvalMode{});
        CHECK(rep.arrival >= full.arrival - 1e-6);     // full recomputation is optimal
        CHECK(rep.arrival <= keep + 1e-6);              // repair is never worse than waiting
        CHECK(rep.path.nodes.front() == s && rep.path.nodes.back() == t);
        CHECK_NEAR(walk_path(after, rep.path, 0, hours(8), EvalMode{}), rep.arrival);
        repaired += rep.arrival > full.arrival + 1e-6 ? 0 : 1;
        // An unrelated closure leaves the route untouched and costs no search.
        Disruptions elsewhere(tm.edge_count());
        const NodeId far = static_cast<NodeId>(g.node_count() - 1);
        elsewhere.add_closure(tm.first_edge(far) + 0, hours(8), hours(9));
        bool on_route = false;
        for (std::size_t i = 0; i < p.path.edges(); ++i)
            on_route |= tm.first_edge(p.path.nodes[i]) + p.path.ks[i] == tm.first_edge(far);
        if (!on_route) {
            const RepairResult none_r = local_repair(Net{&g, &tm, &elsewhere}, elsewhere, p.path, hours(8));
            CHECK(!none_r.affected && none_r.expanded == 0 && none_r.path.nodes == p.path.nodes);
        }
    }
    CHECK(repaired > 0);
}

static Problem tiny_problem(const Graph& g, TrafficModel& tm, std::vector<Vehicle>& vs, std::vector<Request>& rs, int n, int nv, unsigned seed) {
    (void)tm;
    std::mt19937 rng(seed);
    vs.clear(); rs.clear();
    for (int v = 0; v < nv; ++v) vs.push_back({v, 0, 6.0, hours(8), hours(12)});
    for (int i = 0; i < n; ++i) {
        Request r;
        r.id = i;
        r.node = static_cast<NodeId>(1 + rng() % (g.node_count() - 1));
        r.demand = 1 + static_cast<int>(rng() % 2);
        r.service_s = 60;
        r.tw_start = hours(8) + 300.0 * (rng() % 8);
        r.tw_end = r.tw_start + 2400 + 600.0 * (rng() % 4);
        rs.push_back(r);
    }
    return make_problem(vs, rs, hours(8));
}

static void test_fleet_solver() {
    Graph g = make_grid(10, 10, 17);
    TrafficModel tm(g);
    const Disruptions none(tm.edge_count());
    Net net{&g, &tm, &none};
    FleetParams fp;
    double worst_gap = 0;
    int compared = 0;
    for (unsigned seed = 1; seed <= 6; ++seed) {
        std::vector<Vehicle> vs;
        std::vector<Request> rs;
        Problem P = tiny_problem(g, tm, vs, rs, 6, 2, seed);
        TdMatrix M;
        build_matrix(net, P, fp, M);
        const Solution opt = brute_force(P, M, fp);
        PlanStats st;
        Solution s = plan_problem(net, P, fp, PlanMode::Scratch, 500, &st);
        // Feasibility: every route evaluates ok, capacity holds, every request at most once.
        std::vector<int> seen(rs.size(), 0);
        for (std::size_t v = 0; v < s.routes.size(); ++v) {
            CHECK(s.ev[v].ok);
            double load = 0;
            for (int r : s.routes[v]) { ++seen[static_cast<std::size_t>(r)]; load += rs[static_cast<std::size_t>(r)].demand; }
            CHECK(load <= vs[v].capacity + 1e-9);
        }
        for (int c : seen) CHECK(c <= 1);
        CHECK(s.cost <= st.initial_cost + 1e-9);  // the improvement phase never worsens the start
        if (opt.cost < kInf && opt.unassigned() == 0) {
            ++compared;
            CHECK(s.cost >= opt.cost - 1e-6);       // nothing beats the exhaustive optimum
            worst_gap = std::max(worst_gap, (s.cost - opt.cost) / opt.cost);
        }
    }
    CHECK(compared >= 3);
    CHECK(worst_gap < 0.05);
}

static void test_disruption_metric() {
    Problem P;
    P.prev_veh = {0, 0, 1, -1};
    P.prev_rank = {0, 1, 0, -1};
    CHECK_NEAR(schedule_disruption(P, {{0, 1}, {2}, {3}}), 0.0);   // unchanged (new request ignored)
    CHECK_NEAR(schedule_disruption(P, {{1, 0}, {2}, {3}}), 1.0);   // two requests reordered: 0.5 each
    CHECK_NEAR(schedule_disruption(P, {{0}, {2, 1}, {}}), 1.0);    // request 1 moved to another vehicle
}

static void test_simulation() {
    Graph g = make_grid(12, 12, 21);
    TrafficModel tm(g);
    WorldConfig wc;
    wc.seed = 3; wc.vehicles = 3; wc.requests = 14; wc.closures = 2; wc.incidents = 1; wc.window_min = 40;
    const World w = generate_world(g, tm, wc);
    CHECK(w.vehicles.size() == 3 && w.requests.size() == 14);
    std::vector<SimResult> res;
    for (Strategy s : {Strategy::Static, Strategy::Greedy, Strategy::Full, Strategy::Selective}) {
        SimConfig c;
        c.strategy = s;
        c.scenario = 5;
        c.iters_per_request = 10;
        res.push_back(simulate(g, tm, w, c));
        const SimResult& r = res.back();
        CHECK(r.m.on_time + r.m.late + r.m.unserved == r.m.requests);
        // Capacity respected per vehicle; vehicle clocks never run backwards; trajectories follow roads.
        std::vector<double> load(w.vehicles.size(), 0);
        for (const RequestOutcome& o : r.outcomes)
            if (o.served) load[static_cast<std::size_t>(o.vehicle)] += w.requests[static_cast<std::size_t>(o.id)].demand;
        for (std::size_t v = 0; v < load.size(); ++v) CHECK(load[v] <= w.vehicles[v].capacity + 1e-9);
        for (const Trajectory& t : r.trajectories) {
            for (std::size_t i = 1; i < t.times.size(); ++i) {
                CHECK(t.times[i] >= t.times[i - 1] - 1e-9);
                if (t.nodes[i] != t.nodes[i - 1]) CHECK(find_edge(g, t.nodes[i - 1], t.nodes[i]) >= 0);
            }
        }
        CHECK(r.m.success_rate >= 0 && r.m.success_rate <= 1);
    }
    // Determinism: same input, same outcome (timings aside).
    SimConfig c;
    c.strategy = Strategy::Selective; c.scenario = 5; c.iters_per_request = 10;
    const SimResult again = simulate(g, tm, w, c);
    CHECK(again.m.on_time == res[3].m.on_time && again.m.unserved == res[3].m.unserved);
    CHECK_NEAR(again.m.total_distance_m, res[3].m.total_distance_m);

    // With no disruptions and no late requests all strategies execute the same initial plan.
    WorldConfig calm = wc;
    calm.closures = 0; calm.incidents = 0; calm.dynamic_fraction = 0;
    const World w2 = generate_world(g, tm, calm);
    double dist0 = -1;
    for (Strategy s : {Strategy::Static, Strategy::Full, Strategy::Selective}) {
        SimConfig cc;
        cc.strategy = s; cc.scenario = -1; cc.iters_per_request = 10;
        const SimResult r = simulate(g, tm, w2, cc);
        if (dist0 < 0) dist0 = r.m.total_distance_m;
        CHECK_NEAR(r.m.total_distance_m, dist0);
        CHECK(r.m.replans == 0);
    }
    // Reliability evaluation returns probabilities.
    SimConfig mc;
    const McPlanStats st = evaluate_plan_mc(g, tm, w2, mc, 20, false);
    CHECK(st.on_time_rate >= 0 && st.on_time_rate <= 1 && st.on_time_rate_p5 <= st.on_time_rate + 1e-9);
}

static void test_json_commands() {
    Graph g = make_grid(12, 12, 23);
    TrafficModel tm(g);
    EngineContext ctx{g, tm};
    std::string out = handle_json_command(ctx, R"({"cmd":"td_route","source":0,"target":140,"depart_s":28800,"scenarios":50})");
    CHECK(out.find("\"ok\":true") != std::string::npos);
    CHECK(out.find("\"reliable\"") != std::string::npos);
    out = handle_json_command(ctx, R"({"cmd":"replan_demo","source":0,"target":143,"repeat":1})");
    CHECK(out.find("\"selective\"") != std::string::npos);
    out = handle_json_command(ctx, R"({"cmd":"fleet","generate":{"seed":1,"vehicles":2,"requests":8,"closures":1,"incidents":0},"strategies":["selective"],"params":{"iters_per_request":5}})");
    CHECK(out.find("\"ok\":true") != std::string::npos);
    CHECK(out.find("\"trajectories\"") != std::string::npos);
    out = handle_json_command(ctx, R"({"cmd":"traffic","t_s":28800})");
    CHECK(out.find("\"edges\"") != std::string::npos);
    out = handle_json_command(ctx, R"({"cmd":"td_route","source":0,"target":99999})");
    CHECK(out.find("\"ok\":false") != std::string::npos);
    out = handle_json_command(ctx, R"({"cmd":"nope"})");
    CHECK(out.find("\"ok\":false") != std::string::npos);
    out = handle_json_command(ctx, "{bad json");
    CHECK(out.find("\"ok\":false") != std::string::npos);
}

int main() {
    test_basic_routes();
    test_one_way_and_unreachable();
    test_source_equals_target();
    test_trace();
    test_random_agreement();
    test_tsp_heuristics();
    test_tour_on_graph();
    test_asymmetric_two_opt();
    test_json_parser();
    test_traffic_basics();
    test_noise();
    test_td_routing();
    test_closure_detour();
    test_reliability();
    test_local_repair();
    test_fleet_solver();
    test_disruption_metric();
    test_simulation();
    test_json_commands();
    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

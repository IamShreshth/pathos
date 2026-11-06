// Unit tests for the pathos engine (no external framework needed).
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <string>

#include "pathos/graph.hpp"
#include "pathos/routing.hpp"
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

int main() {
    test_basic_routes();
    test_one_way_and_unreachable();
    test_source_equals_target();
    test_trace();
    test_random_agreement();
    test_tsp_heuristics();
    test_tour_on_graph();
    test_asymmetric_two_opt();
    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

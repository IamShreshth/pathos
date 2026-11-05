// Multi-stop tour planning on a road graph.
//
// 1. Run one Dijkstra per stop to build a k x k shortest-path matrix
//    (asymmetric, because of one-way streets).
// 2. Build an initial tour with nearest neighbour.
// 3. Improve it with 2-opt until no reversal lowers the cost.
// 4. For small k, also solve exactly with Held–Karp to measure the gap.
// 5. Expand every leg back into the road path that realises it.
#include "pathos/tsp.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include "pathos/routing.hpp"

namespace pathos {

namespace {

double leg(const Matrix& m, int a, int b) {
    const double c = m[a][b];
    return std::isfinite(c) ? c : kUnreachablePenalty;
}

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

double tour_cost(const Matrix& m, const Tour& t) {
    if (t.size() < 2) return 0.0;
    double total = 0.0;
    for (std::size_t i = 0; i < t.size(); ++i) total += leg(m, t[i], t[(i + 1) % t.size()]);
    return total;
}

Tour nearest_neighbor_tour(const Matrix& m, int start) {
    const int n = static_cast<int>(m.size());
    Tour tour;
    if (n == 0) return tour;
    std::vector<char> used(n, 0);
    int cur = start;
    used[cur] = 1;
    tour.push_back(cur);
    for (int step = 1; step < n; ++step) {
        int best = -1;
        for (int j = 0; j < n; ++j) {
            if (!used[j] && (best == -1 || leg(m, cur, j) < leg(m, cur, best))) best = j;
        }
        used[best] = 1;
        tour.push_back(best);
        cur = best;
    }
    return tour;
}

int two_opt(const Matrix& m, Tour& t, int max_passes) {
    const int n = static_cast<int>(t.size());
    if (n < 3) return 0;
    int moves = 0;

    // fwd[x] = cost of legs t[0]->t[1] ... t[x-1]->t[x]
    // rev[x] = cost of the same legs travelled backwards
    std::vector<double> fwd(n), rev(n);
    for (int pass = 0; pass < max_passes; ++pass) {
        fwd[0] = rev[0] = 0.0;
        for (int x = 1; x < n; ++x) {
            fwd[x] = fwd[x - 1] + leg(m, t[x - 1], t[x]);
            rev[x] = rev[x - 1] + leg(m, t[x], t[x - 1]);
        }

        double best_gain = 0.0;
        int best_i = -1, best_k = -1;
        // Reverse t[i..k]; position 0 (the start stop) never moves.
        for (int i = 1; i < n - 1; ++i) {
            const int prev = t[i - 1];
            for (int k = i + 1; k < n; ++k) {
                const int next = t[(k + 1) % n];
                const double old_cost = leg(m, prev, t[i]) + (fwd[k] - fwd[i]) + leg(m, t[k], next);
                const double new_cost = leg(m, prev, t[k]) + (rev[k] - rev[i]) + leg(m, t[i], next);
                const double gain = old_cost - new_cost;
                // Relative tolerance avoids cycling on floating-point noise.
                if (gain > best_gain && gain > 1e-9 * std::max(1.0, old_cost)) {
                    best_gain = gain;
                    best_i = i;
                    best_k = k;
                }
            }
        }
        if (best_i == -1) break;
        std::reverse(t.begin() + best_i, t.begin() + best_k + 1);
        ++moves;
    }
    return moves;
}

std::optional<double> held_karp(const Matrix& m, Tour* best) {
    const int n = static_cast<int>(m.size());
    if (n == 0 || n > kHeldKarpMaxStops) return std::nullopt;
    if (n == 1) {
        if (best) *best = {0};
        return 0.0;
    }
    const int full = 1 << n;
    // dp[mask][j]: cheapest path that starts at 0, visits exactly `mask`, ends at j.
    std::vector<std::vector<double>> dp(full, std::vector<double>(n, kInf));
    std::vector<std::vector<int>> from(full, std::vector<int>(n, -1));
    dp[1][0] = 0.0;
    for (int mask = 1; mask < full; mask += 2) {  // masks always contain stop 0
        for (int j = 0; j < n; ++j) {
            if (!(mask & (1 << j)) || !std::isfinite(dp[mask][j])) continue;
            for (int k = 1; k < n; ++k) {
                if (mask & (1 << k)) continue;
                const int nm = mask | (1 << k);
                const double c = dp[mask][j] + leg(m, j, k);
                if (c < dp[nm][k]) {
                    dp[nm][k] = c;
                    from[nm][k] = j;
                }
            }
        }
    }
    double best_cost = kInf;
    int last = -1;
    for (int j = 1; j < n; ++j) {
        const double c = dp[full - 1][j] + leg(m, j, 0);
        if (c < best_cost) {
            best_cost = c;
            last = j;
        }
    }
    if (best) {
        Tour t;
        int mask = full - 1;
        for (int j = last; j != -1;) {
            t.push_back(j);
            const int pj = from[mask][j];
            mask ^= (1 << j);
            j = pj;
        }
        std::reverse(t.begin(), t.end());
        *best = t;
    }
    return best_cost;
}

TourResult solve_tour(const Graph& g, const std::vector<NodeId>& stops, Weight w) {
    TourResult r;
    r.weight = w;
    const int k = static_cast<int>(stops.size());
    if (k < 2) throw std::invalid_argument("a tour needs at least 2 stops");

    // 1. Shortest-path matrix (one Dijkstra per stop, parents kept for step 5).
    auto t0 = std::chrono::steady_clock::now();
    r.matrix.assign(k, std::vector<double>(k, kInf));
    std::vector<std::vector<NodeId>> parents(k);
    std::vector<double> dist;
    for (int i = 0; i < k; ++i) {
        dijkstra_to_targets(g, stops[i], stops, w, dist, parents[i]);
        for (int j = 0; j < k; ++j) r.matrix[i][j] = dist[stops[j]];
    }
    r.matrix_ms = ms_since(t0);

    // 2-4. Heuristic tour, then exact optimum for comparison.
    t0 = std::chrono::steady_clock::now();
    r.nn_order = nearest_neighbor_tour(r.matrix, 0);
    r.nn_cost = tour_cost(r.matrix, r.nn_order);
    r.order = r.nn_order;
    r.two_opt_moves = two_opt(r.matrix, r.order);
    r.cost = tour_cost(r.matrix, r.order);
    r.heuristic_ms = ms_since(t0);
    if (auto opt = held_karp(r.matrix); opt && *opt < kUnreachablePenalty) r.optimal_cost = opt;

    // 5. Expand legs into road paths.
    r.feasible = true;
    for (int i = 0; i < k; ++i) {
        TourLeg l;
        l.from_stop = r.order[i];
        l.to_stop = r.order[(i + 1) % k];
        l.cost = r.matrix[l.from_stop][l.to_stop];
        l.reachable = std::isfinite(l.cost);
        if (l.reachable) {
            l.path = reconstruct_path(parents[l.from_stop], stops[l.from_stop], stops[l.to_stop]);
            measure_path(g, l.path, w, l.distance_m, l.time_s);
            r.distance_m += l.distance_m;
            r.time_s += l.time_s;
        } else {
            r.feasible = false;
        }
        r.legs.push_back(std::move(l));
    }
    if (!r.feasible) r.cost = kInf;
    return r;
}

}  // namespace pathos

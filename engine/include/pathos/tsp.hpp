// Multi-stop tours: nearest-neighbour construction + 2-opt improvement.
#pragma once

#include <optional>
#include <vector>

#include "pathos/graph.hpp"
#include "pathos/types.hpp"

namespace pathos {

// matrix[i][j] = shortest road cost from stop i to stop j (kInf if unreachable).
// With one-way roads the matrix is generally asymmetric.
using Matrix = std::vector<std::vector<double>>;

// A tour is a permutation of stop indices that starts with stop 0; the
// return leg to stop 0 is implicit.
using Tour = std::vector<int>;

// Cost of the closed tour. Unreachable legs are counted with a large finite
// penalty so heuristics still compare tours sensibly.
double tour_cost(const Matrix& m, const Tour& t);
constexpr double kUnreachablePenalty = 1e12;

Tour nearest_neighbor_tour(const Matrix& m, int start = 0);

// Best-improvement 2-opt with stop 0 fixed in position 0. Works on asymmetric
// matrices: reversing a segment also reverses the direction of its inner legs,
// which is accounted for with prefix sums so each move is evaluated in O(1).
// Returns the number of improving moves applied.
int two_opt(const Matrix& m, Tour& t, int max_passes = 10000);

// Exact optimum via Held–Karp bitmask DP, O(2^n n^2). Only for small n;
// used to report how far the heuristic is from optimal.
constexpr int kHeldKarpMaxStops = 12;
std::optional<double> held_karp(const Matrix& m, Tour* best = nullptr);

struct TourLeg {
    int from_stop = 0;
    int to_stop = 0;
    bool reachable = false;
    double cost = kInf;
    double distance_m = 0.0;
    double time_s = 0.0;
    std::vector<NodeId> path;
};

struct TourResult {
    bool feasible = false;
    Weight weight = Weight::Distance;
    Matrix matrix;
    Tour nn_order;
    Tour order;
    double nn_cost = kInf;
    double cost = kInf;
    std::optional<double> optimal_cost;
    int two_opt_moves = 0;
    std::vector<TourLeg> legs;
    double distance_m = 0.0;
    double time_s = 0.0;
    double matrix_ms = 0.0;
    double heuristic_ms = 0.0;
};

TourResult solve_tour(const Graph& g, const std::vector<NodeId>& stops, Weight w);

}  // namespace pathos

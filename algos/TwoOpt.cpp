#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std;

struct Point {
    double x, y;
};

double distance(const Point& a, const Point& b) {
    return hypot(a.x - b.x, a.y - b.y);
}

// The tour stores each city once; the closing edge is implicit.
double tourLength(const vector<Point>& cities, const vector<int>& tour) {
    if (tour.empty()) return 0.0;

    double total = 0.0;
    for (size_t i = 0; i < tour.size(); ++i) {
        total += distance(cities[tour[i]],
                          cities[tour[(i + 1) % tour.size()]]);
    }
    return total;
}

// Best-improvement 2-opt for a symmetric TSP.
// Repeats until a full pass finds no improving edge exchange.
vector<int> twoOpt(const vector<Point>& cities, vector<int> tour) {
    const int n = static_cast<int>(tour.size());
    if (n != static_cast<int>(cities.size()))
        throw invalid_argument("Tour must contain every city exactly once.");

    bool improved = true;
    while (improved) {
        improved = false;
        double bestGain = 0.0;
        int bestI = -1, bestK = -1;

        for (int i = 0; i < n; ++i) {
            for (int k = i + 2; k < n; ++k) {
                // These edges are adjacent in the cyclic tour.
                if (i == 0 && k == n - 1) continue;

                const int a = tour[i];
                const int b = tour[(i + 1) % n];
                const int c = tour[k];
                const int d = tour[(k + 1) % n];

                const double oldCost =
                    distance(cities[a], cities[b]) +
                    distance(cities[c], cities[d]);
                const double newCost =
                    distance(cities[a], cities[c]) +
                    distance(cities[b], cities[d]);

                const double gain = oldCost - newCost;
                if (gain > bestGain) {
                    bestGain = gain;
                    bestI = i;
                    bestK = k;
                }
            }
        }

        if (bestI != -1) {
            // Reconnect the edges by reversing the segment between them.
            // The strict improvement test avoids accepting equal-cost moves.
            reverse(tour.begin() + bestI + 1, tour.begin() + bestK + 1);
            improved = true;
        }
    }

    return tour;
}

int main() {
    int n;
    cout << "Number of cities: ";
    if (!(cin >> n) || n < 2) {
        cerr << "Enter at least 2 cities.\n";
        return 1;
    }

    vector<Point> cities(n);
    cout << "Enter each city's x and y coordinates:\n";
    for (int i = 0; i < n; ++i) {
        if (!(cin >> cities[i].x >> cities[i].y)) {
            cerr << "Invalid coordinate input.\n";
            return 1;
        }
    }

    // Example starting tour: city indices in input order.
    vector<int> tour(n);
    for (int i = 0; i < n; ++i) tour[i] = i;

    const double before = tourLength(cities, tour);
    const vector<int> improvedTour = twoOpt(cities, tour);
    const double after = tourLength(cities, improvedTour);

    cout << fixed << setprecision(3);
    cout << "Initial tour length: " << before << '\n';
    cout << "Improved tour: ";
    for (int city : improvedTour) cout << city << " -> ";
    cout << improvedTour.front() << '\n';
    cout << "Improved tour length: " << after << '\n';
    return 0;
}

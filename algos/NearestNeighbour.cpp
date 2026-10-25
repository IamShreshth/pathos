#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace std;

struct Point {
    double x, y;
};

double distance(const Point& a, const Point& b) {
    return hypot(a.x - b.x, a.y - b.y);
}

// Returns each city once; the final edge back to the start is implicit.
vector<int> nearestNeighbour(const vector<Point>& cities, int start = 0) {
    const int n = static_cast<int>(cities.size());
    if (n == 0 || start < 0 || start >= n)
        throw invalid_argument("Invalid city list or starting city.");

    vector<bool> visited(n, false);
    vector<int> tour;
    tour.reserve(n);

    int current = start;
    tour.push_back(current);
    visited[current] = true;

    while (static_cast<int>(tour.size()) < n) {
        int nearest = -1;
        double bestDistance = numeric_limits<double>::infinity();

        for (int city = 0; city < n; ++city) {
            if (visited[city]) continue;

            const double d = distance(cities[current], cities[city]);
            if (d < bestDistance) {
                bestDistance = d;
                nearest = city;
            }
        }

        // A valid unvisited city always exists until the tour is complete.
        current = nearest;
        visited[current] = true;
        tour.push_back(current);
    }

    return tour;
}

double tourLength(const vector<Point>& cities, const vector<int>& tour) {
    if (tour.empty()) return 0.0;

    double total = 0.0;
    for (size_t i = 0; i < tour.size(); ++i) {
        const int from = tour[i];
        const int to = tour[(i + 1) % tour.size()];
        total += distance(cities[from], cities[to]);
    }
    return total;
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

    int start;
    cout << "Starting city (0 to " << n - 1 << "): ";
    if (!(cin >> start) || start < 0 || start >= n) {
        cerr << "Invalid starting city.\n";
        return 1;
    }

    const vector<int> tour = nearestNeighbour(cities, start);

    cout << "Tour: ";
    for (int city : tour) cout << city << " -> ";
    cout << start << '\n';

    cout << fixed << setprecision(3)
         << "Tour length: " << tourLength(cities, tour) << '\n';
    return 0;
}

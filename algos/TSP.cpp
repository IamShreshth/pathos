#include <iostream>
#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>
#include <limits>

using namespace std;

const int INF = 1e9;

// Recursive TSP function using Dynamic Programming with Bitmasking
int tspSolve(int mask, int pos, int n, const vector<vector<int>>& dist, vector<vector<int>>& memo) {
    // Base Case: All cities visited (all bits set to 1)
    if (mask == (1 << n) - 1) {
        return dist[pos][0]; // Return cost to return to start city (0)
    }

    // Return cached result if available
    if (memo[mask][pos] != -1) {
        return memo[mask][pos];
    }

    int minCost = INF;

    // Try visiting all unvisited cities
    for (int city = 0; city < n; ++city) {
        if ((mask & (1 << city)) == 0) { // If bit is 0, city is unvisited
            int newCost = dist[pos][city] + tspSolve(mask | (1 << city), city, n, dist, memo);
            minCost = min(minCost, newCost);
        }
    }

    return memo[mask][pos] = minCost;
}

void runTSP(const vector<vector<int>>& dist) {
    int n = dist.size();
    // Memoization table: 2^N states x N current cities
    vector<vector<int>> memo(1 << n, vector<int>(n, -1));

    // Start at city 0 with mask = 1 (bit 0 set)
    int optimalCost = tspSolve(1, 0, n, dist, memo);

    cout << "[TSP Dynamic Programming] Minimum tour distance: " << optimalCost << "\n";
}



#include <iostream>
#include <vector>
#include <queue>
#include <limits>
#include <algorithm>

using namespace std;

// Structure representing a directed, weighted edge
struct Edge {
    int to;
    double weight;
};

// State for the min-heap priority queue
struct State {
    int u;
    double dist;

    // Min-heap ordering by distance
    bool operator>(const State& other) const {
        return dist > other.dist;
    }
};

// Dijkstra's algorithm for single-source shortest path
void dijkstra(int n, const vector<vector<Edge>>& adj, int start, int goal) {
    const double INF = numeric_limits<double>::infinity();
    vector<double> dist(n, INF);
    vector<int> parent(n, -1);
    vector<bool> visited(n, false);

    // Min-heap priority queue
    priority_queue<State, vector<State>, greater<State>> pq;

    dist[start] = 0.0;
    pq.push({start, 0.0});

    int nodesExplored = 0;

    while (!pq.empty()) {
        auto [u, d] = pq.top();
        pq.pop();

        if (visited[u]) continue;
        visited[u] = true;
        nodesExplored++;

        if (u == goal) break;

        // Edge relaxation
        for (const auto& edge : adj[u]) {
            int v = edge.to;
            double w = edge.weight;

            if (dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                parent[v] = u;
                pq.push({v, dist[v]});
            }
        }
    }

    if (dist[goal] == INF) {
        cout << "No path found from " << start << " to " << goal << "!" << endl;
        return;
    }

    // Reconstruct shortest path
    vector<int> path;
    for (int curr = goal; curr != -1; curr = parent[curr]) {
        path.push_back(curr);
    }
    reverse(path.begin(), path.end());

    cout << "Shortest Path Found!" << endl;
    cout << "Total Distance : " << dist[goal] << endl;
    cout << "Nodes Explored : " << nodesExplored << endl;
    cout << "Route          : ";
    for (size_t i = 0; i < path.size(); i++) {
        cout << path[i];
        if (i + 1 < path.size()) cout << " -> ";
    }
    cout << endl;
}

int main() {
    // 6-node weighted graph example
    int n = 6;
    vector<vector<Edge>> adj(n);

    auto addEdge = [&](int u, int v, double w) {
        adj[u].push_back({v, w});
        adj[v].push_back({u, w}); // Bidirectional
    };

    addEdge(0, 1, 4.0);
    addEdge(0, 2, 2.0);
    addEdge(1, 2, 1.0);
    addEdge(1, 3, 5.0);
    addEdge(2, 3, 8.0);
    addEdge(2, 4, 10.0);
    addEdge(3, 4, 2.0);
    addEdge(3, 5, 6.0);
    addEdge(4, 5, 3.0);

    int start = 0;
    int goal = 5;

    cout << "--- Dijkstra's Shortest Path Algorithm ---" << endl;
    dijkstra(n, adj, start, goal);

    return 0;
}

#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

struct Node {
    int id;              // Internal 0-based index: 0, 1, 2, ...
    int64_t osm_id;      // Original OpenStreetMap ID
    double lat;
    double lon;
};

struct Edge {
    int to;              // Target internal node ID
    double length;       // Distance in meters
    std::string name;    // Road / path name
    bool oneway;
};

class Graph {
private:
    std::vector<Node> nodes;
    std::vector<std::vector<Edge>> adj;
    std::unordered_map<int64_t, int> osm_to_internal;

public:
    Graph();

    int addNode(int64_t osm_id, double lat, double lon);
    void addEdge(int u, int v, double length, const std::string& name = "", bool oneway = false);

    bool loadFromJSON(const std::string& filepath);

    int nodeCount() const;
    int edgeCount() const;

    const Node& getNode(int id) const;
    const std::vector<Edge>& getNeighbors(int u) const;

    // Haversine distance between two nodes in meters
    double distanceMeters(int u, int v) const;

    // Breadth-First Search to find path with fewest turns/segments
    std::vector<int> bfs(int start, int goal, int& explored_count) const;
};

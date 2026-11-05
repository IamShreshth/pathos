#include "../include/Graph.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <queue>
#include <algorithm>

Graph::Graph() {}

int Graph::addNode(int64_t osm_id, double lat, double lon) {
    int id = static_cast<int>(nodes.size());
    nodes.push_back({id, osm_id, lat, lon});
    adj.emplace_back();
    osm_to_internal[osm_id] = id;
    return id;
}

void Graph::addEdge(int u, int v, double length, const std::string& name, bool oneway) {
    if (u < 0 || u >= static_cast<int>(nodes.size()) ||
        v < 0 || v >= static_cast<int>(nodes.size())) {
        return;
    }
    adj[u].push_back({v, length, name, oneway});
}

int Graph::nodeCount() const {
    return static_cast<int>(nodes.size());
}

int Graph::edgeCount() const {
    int count = 0;
    for (const auto& list : adj) {
        count += static_cast<int>(list.size());
    }
    return count;
}

const Node& Graph::getNode(int id) const {
    return nodes.at(id);
}

const std::vector<Edge>& Graph::getNeighbors(int u) const {
    return adj.at(u);
}

double Graph::distanceMeters(int u, int v) const {
    const double R = 6371000.0;
    const double PI = 3.14159265358979323846;

    double lat1 = nodes.at(u).lat * PI / 180.0;
    double lon1 = nodes.at(u).lon * PI / 180.0;
    double lat2 = nodes.at(v).lat * PI / 180.0;
    double lon2 = nodes.at(v).lon * PI / 180.0;

    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;

    double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
               std::cos(lat1) * std::cos(lat2) *
               std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    return R * c;
}

bool Graph::loadFromJSON(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filepath << "\n";
        return false;
    }

    std::string line;
    bool in_nodes = false;
    bool in_edges = false;

    int current_id = -1;
    int64_t current_osm_id = -1;
    double current_lat = 0.0;
    double current_lon = 0.0;

    int edge_u = -1;
    int edge_v = -1;
    double edge_length = 0.0;
    std::string edge_name = "";
    bool edge_oneway = false;

    while (std::getline(file, line)) {
        if (line.find("\"nodes\":") != std::string::npos) {
            in_nodes = true;
            in_edges = false;
            continue;
        }
        if (line.find("\"edges\":") != std::string::npos) {
            in_nodes = false;
            in_edges = true;
            continue;
        }

        if (in_nodes) {
            if (line.find("\"id\":") != std::string::npos) {
                size_t pos = line.find(':');
                current_id = std::stoi(line.substr(pos + 1));
            } else if (line.find("\"osm_id\":") != std::string::npos) {
                size_t pos = line.find(':');
                current_osm_id = std::stoll(line.substr(pos + 1));
            } else if (line.find("\"lat\":") != std::string::npos) {
                size_t pos = line.find(':');
                current_lat = std::stod(line.substr(pos + 1));
            } else if (line.find("\"lon\":") != std::string::npos) {
                size_t pos = line.find(':');
                current_lon = std::stod(line.substr(pos + 1));
            } else if (line.find('}') != std::string::npos && current_id != -1) {
                addNode(current_osm_id, current_lat, current_lon);
                current_id = -1;
            }
        } else if (in_edges) {
            if (line.find("\"u\":") != std::string::npos) {
                size_t pos = line.find(':');
                edge_u = std::stoi(line.substr(pos + 1));
            } else if (line.find("\"v\":") != std::string::npos) {
                size_t pos = line.find(':');
                edge_v = std::stoi(line.substr(pos + 1));
            } else if (line.find("\"length\":") != std::string::npos) {
                size_t pos = line.find(':');
                edge_length = std::stod(line.substr(pos + 1));
            } else if (line.find("\"name\":") != std::string::npos) {
                size_t first = line.find('"');
                size_t second = line.find('"', first + 1);
                size_t third = line.find('"', second + 1);
                size_t fourth = line.find('"', third + 1);
                if (third != std::string::npos && fourth != std::string::npos) {
                    edge_name = line.substr(third + 1, fourth - third - 1);
                }
            } else if (line.find("\"oneway\":") != std::string::npos) {
                edge_oneway = (line.find("true") != std::string::npos);
            } else if (line.find('}') != std::string::npos && edge_u != -1 && edge_v != -1) {
                addEdge(edge_u, edge_v, edge_length, edge_name, edge_oneway);
                edge_u = -1;
                edge_v = -1;
                edge_name = "";
                edge_oneway = false;
            }
        }
    }

    return nodeCount() > 0;
}

std::vector<int> Graph::bfs(int start, int goal, int& explored_count) const {
    explored_count = 0;
    int n = nodeCount();
    if (start < 0 || start >= n || goal < 0 || goal >= n) {
        return {};
    }

    std::vector<bool> visited(n, false);
    std::vector<int> parent(n, -1);
    std::queue<int> q;

    q.push(start);
    visited[start] = true;

    bool found = false;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        explored_count++;

        if (u == goal) {
            found = true;
            break;
        }

        for (const auto& edge : adj[u]) {
            int v = edge.to;
            if (!visited[v]) {
                visited[v] = true;
                parent[v] = u;
                q.push(v);
            }
        }
    }

    if (!found) {
        return {};
    }

    std::vector<int> path;
    for (int at = goal; at != -1; at = parent[at]) {
        path.push_back(at);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

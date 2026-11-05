#include "../include/Graph.hpp"
#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    std::string graphPath = "data/iitj_graph.json";
    if (argc > 1) {
        graphPath = argv[1];
    }

    std::cout << "=======================================\n";
    std::cout << " PATHOS-X Graph Engine (IIT Jodhpur)   \n";
    std::cout << "=======================================\n";

    Graph graph;
    std::cout << "Loading graph from: " << graphPath << " ...\n";

    auto t0 = std::chrono::high_resolution_clock::now();
    if (!graph.loadFromJSON(graphPath)) {
        std::cerr << "Failed to load graph from " << graphPath << "!\n";
        return 1;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double load_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << "[+] Graph Loaded Successfully in " << load_ms << " ms\n";
    std::cout << "    Total Nodes : " << graph.nodeCount() << "\n";
    std::cout << "    Total Edges : " << graph.edgeCount() << "\n\n";

    int maxNode = graph.nodeCount() - 1;
    int start = -1;
    int goal = -1;

    // Interactive user input
    std::cout << "Enter Start Node ID (0 to " << maxNode << "): ";
    if (!(std::cin >> start) || start < 0 || start > maxNode) {
        std::cerr << "Invalid start node ID!\n";
        return 1;
    }

    std::cout << "Enter Goal Node ID  (0 to " << maxNode << "): ";
    if (!(std::cin >> goal) || goal < 0 || goal > maxNode) {
        std::cerr << "Invalid goal node ID!\n";
        return 1;
    }

    std::cout << "\n---------------------------------------\n";
    std::cout << "Source Node      : " << start << " (lat: " << graph.getNode(start).lat << ", lon: " << graph.getNode(start).lon << ")\n";
    std::cout << "Destination Node : " << goal << " (lat: " << graph.getNode(goal).lat << ", lon: " << graph.getNode(goal).lon << ")\n";
    std::cout << "Direct Distance  : " << graph.distanceMeters(start, goal) << " meters (approx as the crow flies)\n";
    std::cout << "---------------------------------------\n";

    int explored_count = 0;
    auto start_search = std::chrono::high_resolution_clock::now();
    std::vector<int> path = graph.bfs(start, goal, explored_count);
    auto end_search = std::chrono::high_resolution_clock::now();

    double search_ms = std::chrono::duration<double, std::milli>(end_search - start_search).count();

    if (path.empty()) {
        std::cout << "[-] No path found between " << start << " and " << goal << ".\n";
    } else {
        std::cout << "[+] Path found via BFS!\n";
        std::cout << "    Hops (road segments) : " << (path.size() - 1) << "\n";
        std::cout << "    Nodes explored       : " << explored_count << "\n";
        std::cout << "    Search time          : " << search_ms << " ms\n";
        std::cout << "    Route sequence       : ";
        for (size_t i = 0; i < path.size(); ++i) {
            std::cout << path[i];
            if (i + 1 < path.size()) std::cout << " -> ";
        }
        std::cout << "\n";
    }

    std::cout << "=======================================\n";
    return 0;
}

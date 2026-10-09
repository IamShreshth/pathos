#include <iostream>
#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>
#include <limits>

using namespace std;

struct Node {
    int x, y;          // Grid coordinates
    double g;          // Cost from start to current node
    double h;          // Estimated cost from current node to goal (Heuristic)
    double f;          // Total estimated cost (f = g + h)
    int parentX, parentY;

    // Operator overload to order the priority queue (min-heap based on f-value)
    bool operator>(const Node& other) const {
        return f > other.f;
    }
};

// Euclidean distance heuristic function
double calculateHeuristic(int x, int y, int goalX, int goalY) {
    return sqrt((x - goalX) * (x - goalX) + (y - goalY) * (y - goalY));
}

// Function to check if a grid cell is valid and walkable
bool isValid(int x, int y, int rows, int cols, const vector<vector<int>>& grid) {
    return (x >= 0 && x < rows && y >= 0 && y < cols && grid[x][y] == 0);
}

// A* Search execution function
void aStarSearch(const vector<vector<int>>& grid, pair<int, int> start, pair<int, int> goal) {
    int rows = grid.size();
    int cols = grid[0].size();

    // Priority queue for the open set (min-heap ordered by f = g + h)
    priority_queue<Node, vector<Node>, greater<Node>> openSet;

    // Data structures to track g-scores and parent pointers
    vector<vector<double>> gScore(rows, vector<double>(cols, numeric_limits<double>::infinity()));
    vector<vector<pair<int, int>>> parent(rows, vector<pair<int, int>>(cols, {-1, -1}));
    vector<vector<bool>> closedSet(rows, vector<bool>(cols, false));

    // Initialize start node
    int startX = start.first, startY = start.second;
    int goalX = goal.first, goalY = goal.second;

    gScore[startX][startY] = 0.0;
    double startH = calculateHeuristic(startX, startY, goalX, goalY);
    openSet.push({startX, startY, 0.0, startH, startH, -1, -1});

    // 8-directional movement vectors (Up, Down, Left, Right, Diagonals)
    int dx[] = {-1, 1, 0, 0, -1, -1, 1, 1};
    int dy[] = {0, 0, -1, 1, -1, 1, -1, 1};
    double moveCost[] = {1.0, 1.0, 1.0, 1.0, 1.414, 1.414, 1.414, 1.414};

    bool foundGoal = false;

    while (!openSet.empty()) {
        Node current = openSet.top();
        openSet.pop();

        int currX = current.x;
        int currY = current.y;

        if (closedSet[currX][currY]) continue;
        closedSet[currX][currY] = true;

        // Goal check
        if (currX == goalX && currY == goalY) {
            foundGoal = true;
            break;
        }

        // Explore neighbors
        for (int i = 0; i < 8; ++i) {
            int nextX = currX + dx[i];
            int nextY = currY + dy[i];

            if (isValid(nextX, nextY, rows, cols, grid) && !closedSet[nextX][nextY]) {
                double tentativeG = gScore[currX][currY] + moveCost[i];

                if (tentativeG < gScore[nextX][nextY]) {
                    gScore[nextX][nextY] = tentativeG;
                    parent[nextX][nextY] = {currX, currY};
                    double hCost = calculateHeuristic(nextX, nextY, goalX, goalY);
                    
                    openSet.push({nextX, nextY, tentativeG, hCost, tentativeG + hCost, currX, currY});
                }
            }
        }
    }

    // Output results
    if (foundGoal) {
        cout << "[A* Search] Path found with total cost: " << gScore[goalX][goalY] << "\nPath Coordinates: ";
        vector<pair<int, int>> path;
        pair<int, int> curr = goal;
        while (curr.first != -1 && curr.second != -1) {
            path.push_back(curr);
            curr = parent[curr.first][curr.second];
        }
        reverse(path.begin(), path.end());
        for (const auto& p : path) {
            cout << "(" << p.first << "," << p.second << ") ";
        }
        cout << "\n\n";
    } else {
        cout << "[A* Search] Path unreachable.\n\n";
    }
}
#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>

using namespace std;

// Checks whether a grid cell is within bounds and walkable (0 = walkable, 1 = obstacle)
bool isValid(int x, int y, int rows, int cols, const vector<vector<int>>& grid) {
    return (x >= 0 && x < rows && y >= 0 && y < cols && grid[x][y] == 0);
}

// Breadth-First Search on an unweighted grid
void bfsSearch(const vector<vector<int>>& grid, pair<int, int> start, pair<int, int> goal) {
    int rows = grid.size();
    int cols = grid[0].size();

    // 4-directional movements (Up, Down, Left, Right)
    int dx[] = {-1, 1, 0, 0};
    int dy[] = {0, 0, -1, 1};

    // Tracking visited nodes and parent pointers for path reconstruction
    vector<vector<bool>> visited(rows, vector<bool>(cols, false));
    vector<vector<pair<int, int>>> parent(rows, vector<pair<int, int>>(cols, {-1, -1}));

    queue<pair<int, int>> q;

    // Initialize with start position
    q.push(start);
    visited[start.first][start.second] = true;

    bool found = false;
    int nodesExplored = 0;

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();
        nodesExplored++;

        if (cx == goal.first && cy == goal.second) {
            found = true;
            break;
        }

        // Explore adjacent neighbors
        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];

            if (isValid(nx, ny, rows, cols, grid) && !visited[nx][ny]) {
                visited[nx][ny] = true;
                parent[nx][ny] = {cx, cy};
                q.push({nx, ny});
            }
        }
    }

    if (!found) {
        cout << "Path not found!" << endl;
        return;
    }

    // Reconstruct path by backtracking from goal to start
    vector<pair<int, int>> path;
    pair<int, int> curr = goal;
    while (curr.first != -1 && curr.second != -1) {
        path.push_back(curr);
        curr = parent[curr.first][curr.second];
    }
    reverse(path.begin(), path.end());

    cout << "Path found successfully!" << endl;
    cout << "Nodes explored: " << nodesExplored << endl;
    cout << "Path length (hops): " << path.size() - 1 << endl;
    cout << "Route: ";
    for (size_t i = 0; i < path.size(); i++) {
        cout << "(" << path[i].first << ", " << path[i].second << ")";
        if (i + 1 < path.size()) cout << " -> ";
    }
    cout << endl;
}

int main() {
    // 0: Walkable path, 1: Obstacle
    vector<vector<int>> grid = {
        {0, 0, 0, 0, 0},
        {0, 1, 1, 1, 0},
        {0, 0, 0, 1, 0},
        {0, 1, 0, 0, 0},
        {0, 0, 0, 0, 0}
    };

    pair<int, int> start = {0, 0};
    pair<int, int> goal = {4, 4};

    cout << "--- Breadth-First Search (BFS) ---" << endl;
    bfsSearch(grid, start, goal);

    return 0;
}

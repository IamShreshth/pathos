## Dijkstra's Algorithm

### 1. Overview
Dijkstra's Algorithm is a greedy graph search algorithm that solves the **single-source shortest path** problem on graphs with **non-negative edge weights**. It finds the path with the minimum total weight from a designated start node to every other reachable node (or a specific goal).

### 2. Edge Relaxation
The fundamental operation in Dijkstra's algorithm is **relaxation**. For an edge $(u, v)$ with weight $w(u, v)$:

$$\text{if } d[u] + w(u, v) < d[v] \implies d[v] = d[u] + w(u, v)$$

If the path through node $u$ offers a shorter route to node $v$, the recorded shortest distance to $v$ is updated, and $u$ becomes the parent of $v$.

### 3. Algorithm Steps
1. Initialize distance to start node as $d[\text{start}] = 0$ and all other nodes as $\infty$.
2. Insert $(\text{start}, 0)$ into a **min-heap priority queue**.
3. While the priority queue is not empty:
   - Extract the node $u$ with the minimum tentative distance.
   - If $u$ is already visited, skip it. Otherwise, mark $u$ as visited.
   - If $u$ is the destination node, stop early.
   - For each outgoing edge $(u, v)$ with weight $w$:
     - Relax the edge: if $d[u] + w < d[v]$, update $d[v] = d[u] + w$, record $\text{parent}[v] = u$, and push $(v, d[v])$ into the priority queue.
4. Backtrack through parent pointers from goal to start to reconstruct the optimal route.

### 4. Non-Negative Weight Requirement
Dijkstra assumes that adding an edge can never decrease the total path cost. If negative edge weights exist, a previously finalized node could be improved later, violating the greedy invariant (in which case the Bellman-Ford algorithm must be used instead).

### 5. Time & Space Complexity
- **Time Complexity:** $O((V + E) \log V)$ using a binary min-heap priority queue.
- **Space Complexity:** $O(V)$ for the distance array, visited set, and priority queue storage.

---

## Breadth-First Search (BFS)

### 1. Overview
Breadth-First Search is an unweighted graph traversal and pathfinding algorithm. It explores all neighbors at the present depth level before moving on to the nodes at the next depth level.

### 2. Properties
- **Optimality:** Guarantees the shortest path in terms of the **minimum number of edges/hops** on unweighted graphs.
- **Data Structure:** Uses a First-In-First-Out (**FIFO Queue**) to maintain the frontier of nodes.
- **Parent Tracking:** Maintains a parent array or mapping to reconstruct the shortest path from the goal back to the source.

### 3. Algorithm Steps
1. Enqueue the start node and mark it as visited.
2. While the queue is not empty:
   - Dequeue the front node $u$.
   - If $u$ is the destination, terminate search.
   - For every unvisited neighbor $v$ of $u$:
     - Mark $v$ as visited.
     - Set $\text{parent}[v] = u$.
     - Enqueue $v$.
3. Trace backwards from the destination to the source using parent pointers to reconstruct the path.

### 4. Time & Space Complexity
- **Time Complexity:** $O(V + E)$, where $V$ is the number of vertices and $E$ is the number of edges.
- **Space Complexity:** $O(V)$, required for the FIFO queue and visited tracking.

---

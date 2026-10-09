## 1. A* Search Algorithm

### 1.1 Overview
A* Search is an informed, heuristic-based graph traversal and pathfinding algorithm. It expands nodes by balancing the known distance from the start node and an estimated distance to the destination node.

### 1.2 Core Evaluation Function
A* evaluates nodes using the scoring formula:

$$f(n) = g(n) + h(n)$$

* **$g(n)$**: The exact cost spent to reach node $n$ from the start node.
* **$h(n)$**: The heuristic function estimating the cost from node $n$ to the goal.
* **$f(n)$**: The total estimated path cost through node $n$.

### 1.3 Admissibility & Consistency
For A* to guarantee finding the shortest path (optimality), the heuristic $h(n)$ must be:
1. **Admissible**: It must **never overestimate** the actual distance to the goal.
2. **Consistent (Monotone)**: For every node $n$ and neighbor $p$, $h(n) \le \text{cost}(n, p) + h(p)$.

Common Heuristics:
* **Euclidean Distance** (used for free diagonal movement): $h = \sqrt{\Delta x^2 + \Delta y^2}$
* **Manhattan Distance** (used for 4-directional grid movement): $h = |\Delta x| + |\Delta y|$

### 1.4 Time & Space Complexity
* **Time Complexity**: $O(E) = O(b^d)$, where $b$ is the branching factor and $d$ is the path depth.
* **Space Complexity**: $O(V)$, as A* maintains all explored and frontier nodes in memory (Priority Queue and Hash Maps).

---
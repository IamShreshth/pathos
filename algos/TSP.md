## 2. Traveling Salesperson Problem (TSP)

### 2.1 Overview
The Traveling Salesperson Problem is an NP-hard optimization problem. Given a set of $N$ cities and pairwise distances, the goal is to find the shortest closed route that visits every city exactly once and returns to the starting node.

### 2.2 Dynamic Programming + Bitmasking Approach
Instead of testing all $N!$ permutations, we use **Dynamic Programming (DP)** combined with **Bitmasking** (Held-Karp algorithm) to reduce redundant subproblem computations.

#### Bitmask Representation
An integer bitmask represents visited cities using binary flags:
* Bit state `1` at index $i$: City $i$ has been visited.
* Mask `0101` ($5_{10}$): Cities 0 and 2 have been visited.

#### State Definition & Transition Formula
Let $DP(\text{mask}, i)$ be the minimum cost to visit all remaining cities and return to start, given the current set of visited cities ($\text{mask}$) and current location ($i$).

$$\text{DP}(\text{mask}, i) = \min_{j \in \text{Unvisited}} \Big( \text{dist}[i][j] + \text{DP}(\text{mask} \cup \{j\}, j) \Big)$$

**Base Case**:
When all cities are visited ($\text{mask} = 2^N - 1$), return distance back to the starting city ($\text{dist}[i][0]$).

### 2.3 Complexity Comparison

| Method | Time Complexity | Space Complexity | Practical Limit |
| :--- | :--- | :--- | :--- |
| **Brute Force (Permutations)** | $O(N!)$ | $O(N)$ | $N \le 10$ |
| **DP + Bitmask (Held-Karp)** | $O(N^2 \cdot 2^N)$ | $O(N \cdot 2^N)$ | $N \le 20$ |

---
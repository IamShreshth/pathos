# Nearest Neighbour (TSP)

A greedy heuristic that builds a tour by repeatedly visiting the closest unvisited city.

## Steps
1. Choose a starting city.
2. Repeatedly visit the nearest unvisited city.
3. Return to the start after visiting every city.

For a tour \(T\), its cyclic length is
\[
L(T)=\sum_{i=0}^{n-1} d(T_i,T_{(i+1)\bmod n})
\]

- **Time:** \(O(n^2)\)
- **Extra space:** \(O(n)\)
- **Limitation:** Fast, but does not guarantee the optimal tour.

`NearestNeighbour.cpp` reads city coordinates as `(x, y)` pairs and prints the tour and its length. City indices are zero-based.

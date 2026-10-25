# 2-opt (TSP)

A local-search heuristic that improves a tour by replacing two edges and reversing the segment between them.

For edges \((a,b)\) and \((c,d)\), compare:
\[
\text{old}=d(a,b)+d(c,d)
\]
\[
\text{new}=d(a,c)+d(b,d)
\]

Accept the move when `new < old`. Repeat until no improving move is found.

- **Time per pass:** \(O(n^2)\)
- **Extra space:** \(O(1)\) for the in-place tour operation, excluding input storage.
- **Limitation:** Finds a 2-opt local optimum, not necessarily the global optimum.

`TwoOpt.cpp` starts from the input-order tour. In a combined solver, pass the Nearest Neighbour tour to `twoOpt()` instead. The edge-exchange formula assumes symmetric distances.

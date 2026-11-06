#!/usr/bin/env bash
# Build the engine (and its tests) into engine/build/.
# Uses CMake when available, otherwise falls back to calling the compiler directly.
set -euo pipefail
cd "$(dirname "$0")"

if command -v cmake >/dev/null 2>&1; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
else
  echo "cmake not found; building with ${CXX:-c++} directly"
  mkdir -p build
  CXX="${CXX:-c++}"
  FLAGS="-std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude"
  SRCS="src/graph.cpp src/path.cpp src/bfs.cpp src/dijkstra.cpp src/astar.cpp src/tsp.cpp"
  $CXX $FLAGS $SRCS src/main.cpp -o build/pathos_engine
  $CXX $FLAGS $SRCS tests/test_engine.cpp -o build/pathos_tests
fi
echo "built: $(pwd)/build/pathos_engine"

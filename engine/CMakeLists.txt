cmake_minimum_required(VERSION 3.16)
project(pathos_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PATHOS_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

add_library(pathos_core
  src/graph.cpp
  src/path.cpp
  src/bfs.cpp
  src/dijkstra.cpp
  src/astar.cpp
  src/tsp.cpp
  src/traffic.cpp
  src/tdroute.cpp
  src/fleet.cpp
  src/sim.cpp
  src/commands.cpp
  src/bench.cpp
)
target_include_directories(pathos_core PUBLIC include)
target_compile_options(pathos_core PRIVATE -Wall -Wextra -Wpedantic)

if(PATHOS_SANITIZE)
  target_compile_options(pathos_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(pathos_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(pathos_engine src/main.cpp)
target_link_libraries(pathos_engine PRIVATE pathos_core)

enable_testing()
add_executable(pathos_tests tests/test_engine.cpp)
target_link_libraries(pathos_tests PRIVATE pathos_core)
add_test(NAME engine_unit_tests COMMAND pathos_tests)

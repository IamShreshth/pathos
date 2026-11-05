cmake_minimum_required(VERSION 3.15)
project(pathos_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(pathos_engine
    src/Graph.cpp
    src/main.cpp
)

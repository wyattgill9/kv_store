cmake_minimum_required(VERSION 3.31.0)
project(orderbook)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")

add_executable(kv_store
    src/test_main.cpp
    src/SPSCQueue.h
)

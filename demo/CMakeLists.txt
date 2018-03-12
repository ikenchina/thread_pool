cmake_minimum_required (VERSION 2.8)

project(demo)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -Wall")

include_directories(${GTEST_INCLUDE_DIRS} ../)

add_executable(demo ../demo.cc ../../thread_pool.cc)

target_link_libraries(demo  pthread rt)
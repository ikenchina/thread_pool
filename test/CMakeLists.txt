cmake_minimum_required (VERSION 2.8)

project(test_case)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -Wall")

find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

include_directories(${GTEST_INCLUDE_DIRS} ../)

add_executable(test_case ../thread_pool_test.cc ../../thread_pool.cc)

target_link_libraries(test_case ${GTEST_BOTH_LIBRARIES})
target_link_libraries(test_case ${CMAKE_THREAD_LIBS_INIT})
add_test(Test test_case)
enable_testing()

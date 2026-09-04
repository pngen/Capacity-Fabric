cmake_minimum_required(VERSION 3.21)

add_executable(bench_capacity bench_capacity.cpp)
target_link_libraries(bench_capacity PRIVATE capacity_fabric)
capacity_fabric_set_cxx20(bench_capacity)
capacity_fabric_set_strict_warnings(bench_capacity)

option(CF_BENCH_BIG "Include the 100k-event / 10k-resource large-scale run" ON)

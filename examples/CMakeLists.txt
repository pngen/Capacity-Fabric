cmake_minimum_required(VERSION 3.21)

function(cf_add_example name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE capacity_fabric)
  capacity_fabric_set_cxx20(${name})
  capacity_fabric_set_strict_warnings(${name})
endfunction()

cf_add_example(example_capacity example_capacity.cpp)
cf_add_example(example_forecast example_forecast.cpp)
cf_add_example(example_scenario example_scenario.cpp)

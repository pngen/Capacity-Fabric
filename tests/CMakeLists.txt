cmake_minimum_required(VERSION 3.21)

# Lightweight, dependency-free test executables. Every test is a plain binary
# registered with CTest; no timeout is enforced (a hang is a defect to diagnose).
function(cf_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE capacity_fabric)
  capacity_fabric_set_cxx20(${name})
  capacity_fabric_set_strict_warnings(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

cf_add_test(test_core test_core.cpp)
cf_add_test(test_property test_property.cpp)
cf_add_test(test_concurrency test_concurrency.cpp)
cf_add_test(test_adversarial test_adversarial.cpp)
cf_add_test(test_protocol test_protocol.cpp)
cf_add_test(test_persistence test_persistence.cpp)
cf_add_test(test_scenario test_scenario.cpp)

# The multiprocess reference proof needs the coordinator/worker executables.
add_executable(test_multiprocess test_multiprocess.cpp)
target_link_libraries(test_multiprocess PRIVATE capacity_fabric)
capacity_fabric_set_cxx20(test_multiprocess)
capacity_fabric_set_strict_warnings(test_multiprocess)
target_compile_definitions(test_multiprocess PRIVATE
  CF_COORD_EXE="$<TARGET_FILE:cfcoord>"
  CF_WORKER_EXE="$<TARGET_FILE:cfworker>")
add_test(NAME test_multiprocess COMMAND test_multiprocess)

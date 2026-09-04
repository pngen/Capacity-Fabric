cmake_minimum_required(VERSION 3.21)
project(CapacityFabricConsumer LANGUAGES CXX)

find_package(CapacityFabric CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE CapacityFabric::CapacityFabric)

set_target_properties(consumer PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(consumer PRIVATE /W4 /WX)
endif()

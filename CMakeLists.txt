cmake_minimum_required(VERSION 3.21)

project(CapacityFabric
  VERSION 1.0.0
  DESCRIPTION "Modeling, forecasting, and explaining usable AI-infrastructure capacity"
  LANGUAGES CXX)

# ---- Strict warnings ----------------------------------------------------------
# Zero-warning policy. Set the strictest warning set plus -Werror on MSVC (/W4 /WX).
function(capacity_fabric_set_strict_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

function(capacity_fabric_set_cxx20 target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
endfunction()

# ---- Options --------------------------------------------------------------------
option(CF_BUILD_TESTS "Build the test suite" ON)
option(CF_BUILD_BENCHMARKS "Build benchmarks" ON)
option(CF_BUILD_EXAMPLES "Build examples" ON)
option(CF_BUILD_CLI "Build the cfcli inspection tool" ON)
option(CF_BUILD_CUDA "Build the CUDA hardware proof (requires CUDA toolkit)" ON)
option(CF_BUILD_ASAN "Build an AddressSanitizer configuration (MSVC)" OFF)
option(CF_INSTALL "Enable install rules" ON)

# AddressSanitizer on MSVC requires the dynamic runtime; force it and emit debug
# info so the C5072 ASAN-without-debug-info warning is not promoted by /WX.
if(CF_BUILD_ASAN)
  if(NOT MSVC)
    message(FATAL_ERROR "CF_BUILD_ASAN is supported only on the MSVC toolchain")
  endif()
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  add_compile_options(/fsanitize=address /Zi)
  add_link_options(/INFERASANLIBS)
endif()

# ---- Core library ---------------------------------------------------------------
add_library(capacity_fabric STATIC
  src/model/model.cpp
  src/persistence/store.cpp
  src/protocol/protocol.cpp
  src/protocol/tcp.cpp
  src/coordinator/coordinator.cpp
  src/adapters/reference.cpp
)

add_library(CapacityFabric::CapacityFabric ALIAS capacity_fabric)
set_target_properties(capacity_fabric PROPERTIES EXPORT_NAME CapacityFabric)

target_include_directories(capacity_fabric
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

capacity_fabric_set_cxx20(capacity_fabric)
capacity_fabric_set_strict_warnings(capacity_fabric)

if(MSVC)
  target_compile_options(capacity_fabric PRIVATE /utf-8 /permissive-)
endif()

find_package(Threads REQUIRED)
target_link_libraries(capacity_fabric PUBLIC Threads::Threads)

if(WIN32)
  target_link_libraries(capacity_fabric PUBLIC ws2_32)
endif()

# ---- Reference distributed executables ------------------------------------------
add_executable(cfcoord tools/coordinator_main.cpp)
target_link_libraries(cfcoord PRIVATE capacity_fabric)
capacity_fabric_set_cxx20(cfcoord)
capacity_fabric_set_strict_warnings(cfcoord)

add_executable(cfworker tools/worker_main.cpp)
target_link_libraries(cfworker PRIVATE capacity_fabric)
capacity_fabric_set_cxx20(cfworker)
capacity_fabric_set_strict_warnings(cfworker)

# ---- CLI -------------------------------------------------------------------------
if(CF_BUILD_CLI)
  add_executable(cfcli src/cli/cfcli.cpp)
  target_link_libraries(cfcli PRIVATE capacity_fabric)
  capacity_fabric_set_cxx20(cfcli)
  capacity_fabric_set_strict_warnings(cfcli)
endif()

# ---- CUDA ------------------------------------------------------------------------
if(CF_BUILD_CUDA)
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    message(STATUS "CUDA toolkit ${CUDAToolkit_VERSION} found; building the CUDA hardware proof")
    enable_language(CUDA)
    add_library(capacity_fabric_cuda STATIC
      cuda/cuda_proof.cu
      cuda/cuda_host.cpp)
    target_include_directories(capacity_fabric_cuda PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>)
    capacity_fabric_set_cxx20(capacity_fabric_cuda)
    # Host-side (.cpp) translations are built with /W4 /WX; the nvcc-compiled
    # translation forwards the same host warning set via -Xcompiler.
    if(MSVC)
      target_compile_options(capacity_fabric_cuda PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/WX>
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4;-Xcompiler=/WX;-Wno-deprecated-gpu-targets>)
    else()
      target_compile_options(capacity_fabric_cuda PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-Wall,-Wextra,-Werror>)
    endif()
    target_link_libraries(capacity_fabric_cuda PUBLIC capacity_fabric)
    if(TARGET CUDA::cudart)
      target_link_libraries(capacity_fabric_cuda PUBLIC CUDA::cudart)
    endif()
    add_executable(cf_cuda_proof cuda/cuda_proof_main.cpp)
    target_link_libraries(cf_cuda_proof PRIVATE capacity_fabric_cuda)
    capacity_fabric_set_cxx20(cf_cuda_proof)
    capacity_fabric_set_strict_warnings(cf_cuda_proof)
    target_compile_definitions(cf_cuda_proof PRIVATE
      CF_COORD_EXE="$<TARGET_FILE:cfcoord>"
      CF_CUDA_WORKER_EXE="$<TARGET_FILE:cf_cuda_worker>")

    add_executable(cf_cuda_worker cuda/cuda_worker_main.cpp)
    target_link_libraries(cf_cuda_worker PRIVATE capacity_fabric_cuda)
    capacity_fabric_set_cxx20(cf_cuda_worker)
    capacity_fabric_set_strict_warnings(cf_cuda_worker)
  else()
    message(STATUS "CUDA toolkit not found; skipping the CUDA hardware proof")
    set(CF_CUDA_UNAVAILABLE TRUE)
  endif()
endif()

# ---- Tests / examples / benchmarks ------------------------------------------------
if(CF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CF_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(CF_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ---- Install / export -------------------------------------------------------------
if(CF_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS capacity_fabric
    EXPORT CapacityFabricTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  if(TARGET capacity_fabric_cuda)
    install(TARGETS capacity_fabric_cuda
      EXPORT CapacityFabricTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()

  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(EXPORT CapacityFabricTargets
    FILE CapacityFabricTargets.cmake
    NAMESPACE CapacityFabric::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CapacityFabric)

  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CapacityFabricConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CapacityFabricConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CapacityFabric)

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CapacityFabricConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CapacityFabricConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CapacityFabricConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CapacityFabric)
endif()

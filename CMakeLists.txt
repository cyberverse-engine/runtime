cmake_minimum_required(VERSION 4.0.0)
project(cyberverse_runtime VERSION 0.1.0 LANGUAGES CXX)

option(CYBERVERSE_RUNTIME_BUILD_TESTS "Build unit test cases" ON)
option(CYBERVERSE_RUNTIME_BUILD_BENCHMARKS "Build benchmarks" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src)

if(CYBERVERSE_RUNTIME_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

if(CYBERVERSE_RUNTIME_BUILD_BENCHMARKS)
  add_subdirectory(benchmark)
endif()

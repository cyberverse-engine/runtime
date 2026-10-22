cmake_minimum_required(VERSION 4.0.0)

set(BENCHMARK_ENABLE_TESTING OFF)

include(FetchContent)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark
  GIT_TAG v1.9.5
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(benchmark)

add_executable(cyberverse_runtime_benchmarks frame_benchmark.cpp)

target_link_libraries(
  cyberverse_runtime_benchmarks
  PRIVATE
  cyberverse::runtime::static_lib
  benchmark::benchmark
)

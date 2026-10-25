cmake_minimum_required(VERSION 4.0.0)

include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(Catch2)

add_executable(
  cyberverse_runtime_tests
  clock_node_test.cpp
  clock_tree_test.cpp
  frame_budget_test.cpp
  frame_pipeline_test.cpp
  frame_schedule_test.cpp
  frame_stage_test.cpp
  frame_stats_test.cpp
  task_queue_test.cpp
  this_thread_test.cpp
)

target_link_libraries(
  cyberverse_runtime_tests
  PRIVATE
  cyberverse::runtime::static_lib
  Catch2::Catch2WithMain
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(cyberverse_runtime_tests)

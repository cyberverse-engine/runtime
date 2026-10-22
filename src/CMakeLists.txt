cmake_minimum_required(VERSION 4.0.0)

add_library(cyberverse_runtime_static_lib STATIC)
add_library(cyberverse::runtime::static_lib ALIAS cyberverse_runtime_static_lib)

target_include_directories(cyberverse_runtime_static_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Public headers
set(
  HEADER_FILES
  cyberverse/runtime.hpp
  cyberverse/runtime/clock_node.hpp
  cyberverse/runtime/clock_tree.hpp
  cyberverse/runtime/frame_pipeline.hpp
  cyberverse/runtime/frame_schedule.hpp
  cyberverse/runtime/frame_stage.hpp
  cyberverse/runtime/frame_stats.hpp
)

# Source files
set(
  SOURCE_FILES
  cyberverse/runtime/clock_node.cpp
  cyberverse/runtime/clock_tree.cpp
  cyberverse/runtime/frame_pipeline.cpp
  cyberverse/runtime/frame_schedule.cpp
  cyberverse/runtime/frame_stage.cpp
  cyberverse/runtime/frame_stats.cpp
)

target_sources(cyberverse_runtime_static_lib PUBLIC ${HEADER_FILES})
target_sources(cyberverse_runtime_static_lib PRIVATE ${SOURCE_FILES})

target_compile_features(cyberverse_runtime_static_lib PUBLIC cxx_std_23)

find_package(Threads REQUIRED)
target_link_libraries(cyberverse_runtime_static_lib PUBLIC Threads::Threads)

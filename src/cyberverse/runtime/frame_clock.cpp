/**
 * @file frame_clock.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Pacing clock implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/frame_clock.hpp"

#include <thread>

namespace cyberverse::runtime {
namespace {

using steady_clock_type = std::chrono::steady_clock;

} // namespace

double steady_frame_clock::now() noexcept {
  return std::chrono::duration<double>(steady_clock_type::now() - origin_).count();
}

void steady_frame_clock::sleep_until(double deadline) {
  if (!(deadline > now())) {
    return;
  }

  const auto target =
      origin_ + std::chrono::duration_cast<steady_clock_type::duration>(std::chrono::duration<double>(deadline));
  std::this_thread::sleep_until(target);
}

} // namespace cyberverse::runtime

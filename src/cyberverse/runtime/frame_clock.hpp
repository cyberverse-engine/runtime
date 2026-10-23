/**
 * @file frame_clock.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Monotonic pacing clock abstraction
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <chrono>

namespace cyberverse {
namespace runtime {

/**
 * @brief Monotonic clock used for render pacing and wait deadlines.
 *
 * The frame delta itself still comes from @ref frame_pipeline::tick_clock so
 * applications can inject synthetic time. A `frame_clock` only answers "what
 * time is it now" and "sleep until this deadline"; it lets tests provide a
 * deterministic pacing clock without touching the real system clock.
 */
class frame_clock {
public:
  virtual ~frame_clock() = default;

  frame_clock(const frame_clock&) = delete;
  frame_clock& operator=(const frame_clock&) = delete;
  frame_clock(frame_clock&&) = delete;
  frame_clock& operator=(frame_clock&&) = delete;

  /** @brief Monotonic time in seconds. The origin is implementation-defined. */
  virtual double now() noexcept = 0;

  /**
   * @brief Sleep until the monotonic deadline, in seconds.
   *
   * A deadline at or before @ref now may return immediately.
   */
  virtual void sleep_until(double deadline) = 0;

protected:
  frame_clock() = default;
};

/**
 * @brief Default pacing clock backed by `std::chrono::steady_clock`.
 */
class steady_frame_clock final : public frame_clock {
public:
  steady_frame_clock() noexcept = default;

  double now() noexcept override;

  void sleep_until(double deadline) override;

private:
  std::chrono::steady_clock::time_point origin_ = std::chrono::steady_clock::now();
};

} // namespace runtime
} // namespace cyberverse

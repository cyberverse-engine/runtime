/**
 * @file frame_schedule.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Frame schedule implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/frame_schedule.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "cyberverse/runtime/frame_stats.hpp"

namespace cyberverse::runtime {
namespace {

constexpr double kStepEpsilon = 1e-9;
constexpr double kDefaultLogicHz = 60.0;
constexpr double kDefaultMaxFrameTime = 0.25;
constexpr int kDefaultMaxLogicSteps = 5;
constexpr int kDefaultAdjustInterval = 30;

/** Target utilization below which the adaptive controller recovers frame rate. */
constexpr double kRecoveryUtilization = 0.70;

/** Multiplicative step used when the target render rate is lowered. */
constexpr double kShrinkFactor = 0.9;

/** Multiplicative step used when the target render rate recovers. */
constexpr double kGrowFactor = 1.1;

bool is_positive_finite(double value) { return std::isfinite(value) && value > 0.0; }

} // namespace

frame_schedule::frame_schedule(const schedule_config& config) noexcept : config_(config) {
  if (!is_positive_finite(config_.target_logic_hz)) {
    config_.target_logic_hz = kDefaultLogicHz;
  }
  if (!is_positive_finite(config_.max_frame_time)) {
    config_.max_frame_time = kDefaultMaxFrameTime;
  }
  if (config_.max_logic_steps_per_frame <= 0) {
    config_.max_logic_steps_per_frame = kDefaultMaxLogicSteps;
  }

  if (!std::isfinite(config_.target_render_hz) || config_.target_render_hz < 0.0) {
    config_.target_render_hz = 0.0;
  }
  if (config_.target_render_hz > 0.0) {
    if (!is_positive_finite(config_.min_render_hz) || config_.min_render_hz > config_.target_render_hz) {
      config_.min_render_hz = std::min(std::max(config_.min_render_hz, 0.0), config_.target_render_hz);
      if (!is_positive_finite(config_.min_render_hz)) {
        config_.min_render_hz = config_.target_render_hz;
      }
    }
  } else {
    config_.min_render_hz = 0.0;
  }

  if (config_.auto_adjust_interval <= 0) {
    config_.auto_adjust_interval = kDefaultAdjustInterval;
  }

  logic_step_ = 1.0 / config_.target_logic_hz;
  desired_render_hz_ = config_.target_render_hz;
  target_render_hz_ = desired_render_hz_;
  frames_since_adjust_ = 0;
}

void frame_schedule::target_render_hz(double hz) noexcept {
  if (!std::isfinite(hz) || hz < 0.0) {
    hz = 0.0;
  }

  config_.target_render_hz = hz;
  desired_render_hz_ = hz;
  target_render_hz_ = hz;
  frames_since_adjust_ = 0;
}

double frame_schedule::target_render_hz() const noexcept { return target_render_hz_; }

double frame_schedule::desired_render_hz() const noexcept { return desired_render_hz_; }

bool frame_schedule::is_unlimited() const noexcept { return target_render_hz_ <= 0.0; }

bool frame_schedule::should_throttle(double elapsed_since_last_present) const noexcept {
  if (is_unlimited() || !std::isfinite(elapsed_since_last_present)) {
    return false;
  }

  return elapsed_since_last_present < target_frame_time();
}

double frame_schedule::target_frame_time() const noexcept { return is_unlimited() ? 0.0 : 1.0 / target_render_hz_; }

void frame_schedule::observe(const frame_stats& stats) noexcept {
  if (!config_.auto_render_hz || desired_render_hz_ <= 0.0 || target_render_hz_ <= 0.0) {
    return;
  }
  if (config_.min_render_hz >= desired_render_hz_ || stats.total_frames() == 0) {
    return;
  }

  ++frames_since_adjust_;
  if (frames_since_adjust_ < config_.auto_adjust_interval) {
    return;
  }
  frames_since_adjust_ = 0;

  const double utilization = std::max(stats.logic_utilization(), stats.render_utilization());

  if (stats.is_over_budget()) {
    target_render_hz_ = std::max(config_.min_render_hz, target_render_hz_ * kShrinkFactor);
  } else if (!stats.is_jittering() && utilization < kRecoveryUtilization) {
    target_render_hz_ = std::min(desired_render_hz_, target_render_hz_ * kGrowFactor);
  }
}

double frame_schedule::logic_step() const noexcept { return logic_step_; }

const schedule_config& frame_schedule::config() const noexcept { return config_; }

frame_schedule::fixed_step_scheduler frame_schedule::new_fixed_step_scheduler() const noexcept {
  return fixed_step_scheduler(this);
}

frame_schedule::fixed_step_scheduler::fixed_step_scheduler(const frame_schedule* p_owner) noexcept
    : config_(p_owner ? p_owner->config() : schedule_config{})
    , logic_step_(p_owner ? p_owner->logic_step() : 1.0 / kDefaultLogicHz) {}

step_result frame_schedule::fixed_step_scheduler::step(double delta) noexcept {
  step_result result;

  double sanitized = delta;
  if (!std::isfinite(sanitized) || sanitized < 0.0) {
    sanitized = 0.0;
  }
  if (sanitized > config_.max_frame_time) {
    sanitized = config_.max_frame_time;
    result.dropped_backlog = true;
  }

  accumulator_ += sanitized;

  const double step = logic_step_;
  if (!is_positive_finite(step)) {
    accumulator_ = 0.0;
    return result;
  }

  const double pending = accumulator_;
  const double raw_steps = std::floor(pending / step + kStepEpsilon);

  int steps = 0;
  if (raw_steps > static_cast<double>(std::numeric_limits<int>::max())) {
    steps = std::numeric_limits<int>::max();
  } else {
    steps = static_cast<int>(raw_steps);
  }

  if (steps > config_.max_logic_steps_per_frame) {
    steps = config_.max_logic_steps_per_frame;
    result.dropped_backlog = true;

    // Discard whole steps that cannot be executed, but keep the fractional
    // phase so rendering still interpolates between logic states.
    accumulator_ = std::fmod(pending, step);
    if (accumulator_ >= step - kStepEpsilon) {
      // Floating-point division may leave a remainder that rounds up to a
      // whole step; treat it as no remainder.
      accumulator_ = 0.0;
    }
  } else {
    accumulator_ = pending - static_cast<double>(steps) * step;
  }

  if (accumulator_ < 0.0) {
    accumulator_ = 0.0;
  }

  result.logic_steps = steps;
  result.alpha = std::clamp(accumulator_ / step, 0.0, 1.0);
  return result;
}

void frame_schedule::fixed_step_scheduler::reset() noexcept { accumulator_ = 0.0; }

double frame_schedule::fixed_step_scheduler::logic_step() const noexcept { return logic_step_; }

double frame_schedule::fixed_step_scheduler::accumulator() const noexcept { return accumulator_; }

} // namespace cyberverse::runtime

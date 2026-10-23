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

void normalize_logic_config(schedule_config& config) noexcept {
  if (!is_positive_finite(config.target_logic_hz)) {
    config.target_logic_hz = kDefaultLogicHz;
  }
  if (!is_positive_finite(config.max_frame_time)) {
    config.max_frame_time = kDefaultMaxFrameTime;
  }
  if (config.max_logic_steps_per_frame <= 0) {
    config.max_logic_steps_per_frame = kDefaultMaxLogicSteps;
  }
}

schedule_config normalize_config(schedule_config config) noexcept {
  normalize_logic_config(config);

  if (!std::isfinite(config.target_render_hz) || config.target_render_hz < 0.0) {
    config.target_render_hz = 0.0;
  }
  if (config.target_render_hz > 0.0) {
    if (!is_positive_finite(config.min_render_hz) || config.min_render_hz > config.target_render_hz) {
      config.min_render_hz = std::min(std::max(config.min_render_hz, 0.0), config.target_render_hz);
      if (!is_positive_finite(config.min_render_hz)) {
        config.min_render_hz = config.target_render_hz;
      }
    }
  } else {
    config.min_render_hz = 0.0;
  }

  if (config.auto_adjust_interval <= 0) {
    config.auto_adjust_interval = kDefaultAdjustInterval;
  }
  return config;
}

} // namespace

frame_schedule::frame_schedule(const schedule_config& config) noexcept { set_config(config); }

void frame_schedule::target_render_hz(double hz) noexcept {
  if (!std::isfinite(hz) || hz < 0.0) {
    hz = 0.0;
  }

  config_.target_render_hz = hz;
  desired_render_hz_ = hz;
  target_render_hz_ = hz;
  frames_since_adjust_ = 0;
}

void frame_schedule::target_logic_hz(double hz) noexcept {
  config_.target_logic_hz = hz;
  normalize_logic_config(config_);
  logic_step_ = 1.0 / config_.target_logic_hz;
}

void frame_schedule::max_frame_time(double seconds) noexcept {
  config_.max_frame_time = seconds;
  normalize_logic_config(config_);
}

void frame_schedule::max_logic_steps_per_frame(int steps) noexcept {
  config_.max_logic_steps_per_frame = steps;
  normalize_logic_config(config_);
}

void frame_schedule::set_config(const schedule_config& config) noexcept {
  config_ = normalize_config(config);
  logic_step_ = 1.0 / config_.target_logic_hz;
  desired_render_hz_ = config_.target_render_hz;
  target_render_hz_ = desired_render_hz_;
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

frame_schedule::fixed_step_scheduler
frame_schedule::new_fixed_step_scheduler(const fixed_step_overrides& overrides) const noexcept {
  return fixed_step_scheduler(this, overrides);
}

frame_schedule::fixed_step_scheduler::fixed_step_scheduler(const frame_schedule* p_owner) noexcept
    : fixed_step_scheduler(p_owner, {}) {}

frame_schedule::fixed_step_scheduler::fixed_step_scheduler(const frame_schedule* p_owner,
                                                           const fixed_step_overrides& overrides) noexcept
    : owner_(p_owner), overrides_(overrides) {}

frame_schedule::fixed_step_scheduler::fixed_step_scheduler(const schedule_config& config) noexcept
    : standalone_config_(normalize_config(config)) {}

schedule_config frame_schedule::fixed_step_scheduler::resolve_config() const noexcept {
  schedule_config config = owner_ ? owner_->config() : standalone_config_;
  if (overrides_.target_logic_hz) {
    config.target_logic_hz = *overrides_.target_logic_hz;
  }
  if (overrides_.max_frame_time) {
    config.max_frame_time = *overrides_.max_frame_time;
  }
  if (overrides_.max_logic_steps_per_frame) {
    config.max_logic_steps_per_frame = *overrides_.max_logic_steps_per_frame;
  }
  normalize_logic_config(config);
  return config;
}

schedule_config frame_schedule::fixed_step_scheduler::config() const noexcept { return resolve_config(); }

step_result frame_schedule::fixed_step_scheduler::step(double delta) noexcept {
  step_result result;

  const schedule_config config = resolve_config();
  double sanitized = delta;
  if (!std::isfinite(sanitized) || sanitized < 0.0) {
    sanitized = 0.0;
  }
  if (sanitized > config.max_frame_time) {
    sanitized = config.max_frame_time;
    result.dropped_backlog = true;
  }

  accumulator_ += sanitized;

  const double step = 1.0 / config.target_logic_hz;
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

  if (steps > config.max_logic_steps_per_frame) {
    steps = config.max_logic_steps_per_frame;
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

double frame_schedule::fixed_step_scheduler::logic_step() const noexcept {
  return 1.0 / resolve_config().target_logic_hz;
}

double frame_schedule::fixed_step_scheduler::accumulator() const noexcept { return accumulator_; }

} // namespace cyberverse::runtime

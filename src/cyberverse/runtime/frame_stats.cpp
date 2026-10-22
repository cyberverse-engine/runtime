/**
 * @file frame_stats.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Frame stats implementation
 * @version 0.1
 * @date 2026-10-22
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/frame_stats.hpp"

#include <cmath>

namespace cyberverse::runtime {
namespace {

constexpr double kDefaultEmaAlpha = 0.1;
constexpr double kDefaultBudget = 1.0 / 60.0;

double sanitize_time(double value) { return std::isfinite(value) && value > 0.0 ? value : 0.0; }

} // namespace

frame_stats::frame_stats(double ema_alpha, double budget) noexcept
    : ema_alpha_(std::isfinite(ema_alpha) && ema_alpha > 0.0 && ema_alpha <= 1.0 ? ema_alpha : kDefaultEmaAlpha)
    , budget_(std::isfinite(budget) && budget > 0.0 ? budget : kDefaultBudget) {}

void frame_stats::record_frame(const frame_sample& sample) noexcept {
  const double frame_time = sanitize_time(sample.frame_time);
  const double logic_time = sanitize_time(sample.logic_time);
  const double render_time = sanitize_time(sample.render_time);

  ++total_frames_;

  if (!has_sample_) {
    smoothed_frame_time_ = frame_time;
    smoothed_logic_time_ = logic_time;
    smoothed_render_time_ = render_time;
    has_sample_ = true;
    jitter_streak_ = 0;
    over_budget_streak_ = 0;
    return;
  }

  if (smoothed_frame_time_ > 0.0 && std::abs(frame_time - smoothed_frame_time_) > jitter_ratio * smoothed_frame_time_) {
    ++jitter_streak_;
  } else {
    jitter_streak_ = 0;
  }

  const double keep = 1.0 - ema_alpha_;
  smoothed_frame_time_ = ema_alpha_ * frame_time + keep * smoothed_frame_time_;
  smoothed_logic_time_ = ema_alpha_ * logic_time + keep * smoothed_logic_time_;
  smoothed_render_time_ = ema_alpha_ * render_time + keep * smoothed_render_time_;

  if (frame_time > budget_ * over_budget_ratio) {
    ++over_budget_streak_;
  } else {
    over_budget_streak_ = 0;
  }
}

void frame_stats::reset() noexcept {
  smoothed_frame_time_ = 0.0;
  smoothed_logic_time_ = 0.0;
  smoothed_render_time_ = 0.0;
  total_frames_ = 0;
  jitter_streak_ = 0;
  over_budget_streak_ = 0;
  has_sample_ = false;
}

double frame_stats::smoothed_frame_time() const noexcept { return smoothed_frame_time_; }

double frame_stats::smoothed_logic_time() const noexcept { return smoothed_logic_time_; }

double frame_stats::smoothed_render_time() const noexcept { return smoothed_render_time_; }

double frame_stats::average_fps() const noexcept {
  return smoothed_frame_time_ > 0.0 ? 1.0 / smoothed_frame_time_ : 0.0;
}

double frame_stats::logic_utilization() const noexcept { return budget_ > 0.0 ? smoothed_logic_time_ / budget_ : 0.0; }

double frame_stats::render_utilization() const noexcept {
  return budget_ > 0.0 ? smoothed_render_time_ / budget_ : 0.0;
}

bool frame_stats::is_jittering() const noexcept { return jitter_streak_ >= jitter_streak_threshold; }

bool frame_stats::is_over_budget() const noexcept { return over_budget_streak_ >= over_budget_streak_threshold; }

uint64_t frame_stats::total_frames() const noexcept { return total_frames_; }

void frame_stats::set_budget(double seconds) noexcept {
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return;
  }
  if (seconds != budget_) {
    budget_ = seconds;
    over_budget_streak_ = 0;
  }
}

double frame_stats::budget() const noexcept { return budget_; }

} // namespace cyberverse::runtime

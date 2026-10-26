/**
 * @file frame_stats.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Frame stats implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/frame_stats.hpp"

#include <algorithm>
#include <cmath>

namespace cyberverse::runtime {
namespace {

constexpr double kDefaultEmaAlpha = 0.1;
constexpr double kDefaultBudget = 1.0 / 60.0;

double sanitize_time(double value) { return std::isfinite(value) && value > 0.0 ? value : 0.0; }

} // namespace

frame_stats::frame_stats(double ema_alpha, double budget, std::size_t window_size) noexcept
    : ema_alpha_(std::isfinite(ema_alpha) && ema_alpha > 0.0 && ema_alpha <= 1.0 ? ema_alpha : kDefaultEmaAlpha)
    , budget_(std::isfinite(budget) && budget > 0.0 ? budget : kDefaultBudget)
    , window_capacity_(std::clamp(window_size, std::size_t{ 1 }, max_window_size)) {}

void frame_stats::record_frame(const frame_sample& sample) noexcept {
  const double frame_time = sanitize_time(sample.frame_time);
  const double prelude_time = sanitize_time(sample.prelude_time);
  const double logic_time = sanitize_time(sample.logic_time);
  const double render_time = sanitize_time(sample.render_time);

  ++total_frames_;
  record_window_sample(frame_time);

  if (!has_sample_) {
    smoothed_frame_time_ = frame_time;
    smoothed_prelude_time_ = prelude_time;
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
  smoothed_prelude_time_ = ema_alpha_ * prelude_time + keep * smoothed_prelude_time_;
  smoothed_logic_time_ = ema_alpha_ * logic_time + keep * smoothed_logic_time_;
  smoothed_render_time_ = ema_alpha_ * render_time + keep * smoothed_render_time_;

  if (frame_time > budget_ * over_budget_ratio) {
    ++over_budget_streak_;
  } else {
    over_budget_streak_ = 0;
  }
}

void frame_stats::record_window_sample(double frame_time) noexcept {
  window_[window_next_] = frame_time;
  window_next_ = (window_next_ + 1) % window_capacity_;
  window_count_ = std::min(window_count_ + 1, window_capacity_);
}

void frame_stats::reset() noexcept {
  smoothed_frame_time_ = 0.0;
  smoothed_prelude_time_ = 0.0;
  smoothed_logic_time_ = 0.0;
  smoothed_render_time_ = 0.0;
  total_frames_ = 0;
  jitter_streak_ = 0;
  over_budget_streak_ = 0;
  has_sample_ = false;
  window_count_ = 0;
  window_next_ = 0;
}

double frame_stats::smoothed_frame_time() const noexcept { return smoothed_frame_time_; }

double frame_stats::smoothed_prelude_time() const noexcept { return smoothed_prelude_time_; }

double frame_stats::smoothed_logic_time() const noexcept { return smoothed_logic_time_; }

double frame_stats::smoothed_render_time() const noexcept { return smoothed_render_time_; }

double frame_stats::average_fps() const noexcept {
  return smoothed_frame_time_ > 0.0 ? 1.0 / smoothed_frame_time_ : 0.0;
}

double frame_stats::prelude_utilization() const noexcept {
  return budget_ > 0.0 ? smoothed_prelude_time_ / budget_ : 0.0;
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

double frame_stats::min_frame_time() const noexcept {
  if (window_count_ == 0) {
    return 0.0;
  }

  const std::size_t start = (window_next_ + window_capacity_ - window_count_) % window_capacity_;
  double result = window_[start];
  for (std::size_t i = 1; i < window_count_; ++i) {
    result = std::min(result, window_[(start + i) % window_capacity_]);
  }
  return result;
}

double frame_stats::max_frame_time() const noexcept {
  if (window_count_ == 0) {
    return 0.0;
  }

  const std::size_t start = (window_next_ + window_capacity_ - window_count_) % window_capacity_;
  double result = window_[start];
  for (std::size_t i = 1; i < window_count_; ++i) {
    result = std::max(result, window_[(start + i) % window_capacity_]);
  }
  return result;
}

double frame_stats::percentile(double p) const noexcept {
  if (window_count_ == 0) {
    return 0.0;
  }
  if (!std::isfinite(p) || p <= 0.0) {
    return min_frame_time();
  }
  if (p >= 1.0) {
    return max_frame_time();
  }

  std::array<double, max_window_size> sorted{};
  const std::size_t start = (window_next_ + window_capacity_ - window_count_) % window_capacity_;
  for (std::size_t i = 0; i < window_count_; ++i) {
    sorted[i] = window_[(start + i) % window_capacity_];
  }
  std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(window_count_));

  const double rank = p * static_cast<double>(window_count_ - 1);
  const auto lower = static_cast<std::size_t>(std::floor(rank));
  const auto upper = static_cast<std::size_t>(std::ceil(rank));
  if (lower == upper) {
    return sorted[lower];
  }

  const double fraction = rank - static_cast<double>(lower);
  return sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction;
}

double frame_stats::budget_margin() const noexcept { return budget_ - smoothed_frame_time_; }

std::size_t frame_stats::window_size() const noexcept { return window_capacity_; }

std::size_t frame_stats::window_sample_count() const noexcept { return window_count_; }

} // namespace cyberverse::runtime

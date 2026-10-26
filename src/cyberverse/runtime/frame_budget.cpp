/**
 * @file frame_budget.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Task budget implementation
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/frame_budget.hpp"

#include <cmath>
#include <limits>

#include "cyberverse/runtime/frame_clock.hpp"

namespace cyberverse::runtime {
namespace {

constexpr double kUnlimitedBudget = -1.0;

bool is_unlimited_seconds(double seconds) noexcept { return !std::isfinite(seconds) || seconds < 0.0; }

double clamp_ratio(double ratio) noexcept {
  if (!std::isfinite(ratio) || ratio < 0.0) {
    return 0.0;
  }
  return ratio > 1.0 ? 1.0 : ratio;
}

} // namespace

frame_budget::frame_budget(double seconds, frame_clock& clock) noexcept : frame_budget(seconds, clock, clock.now()) {}

frame_budget::frame_budget() noexcept : budget_seconds_(kUnlimitedBudget) {}

frame_budget frame_budget::unlimited() noexcept { return frame_budget{}; }

frame_budget::frame_budget(double seconds, frame_clock& clock, double start_seconds) noexcept
    : clock_(&clock)
    , start_seconds_(start_seconds)
    , budget_seconds_(is_unlimited_seconds(seconds) ? kUnlimitedBudget : seconds) {}

bool frame_budget::is_unlimited() const noexcept { return budget_seconds_ < 0.0; }

bool frame_budget::has_remaining() const noexcept { return is_unlimited() || used_seconds() < budget_seconds_; }

double frame_budget::remaining_seconds() const noexcept {
  if (is_unlimited()) {
    return std::numeric_limits<double>::max();
  }
  return remaining_at(current_seconds());
}

double frame_budget::used_seconds() const noexcept {
  if (clock_ == nullptr) {
    return 0.0;
  }
  const double elapsed = current_seconds() - start_seconds_;
  return elapsed > 0.0 ? elapsed : 0.0;
}

double frame_budget::deadline_seconds() const noexcept {
  return is_unlimited() ? std::numeric_limits<double>::infinity() : start_seconds_ + budget_seconds_;
}

frame_budget frame_budget::sub(double ratio) const noexcept {
  if (is_unlimited()) {
    if (clock_ == nullptr) {
      return unlimited();
    }
    return frame_budget(kUnlimitedBudget, *clock_, current_seconds());
  }

  const double now = current_seconds();
  const double child_seconds = remaining_at(now) * clamp_ratio(ratio);
  return frame_budget(child_seconds, *clock_, now);
}

frame_budget frame_budget::share(double ratio) const noexcept {
  if (is_unlimited()) {
    if (clock_ == nullptr) {
      return unlimited();
    }
    return frame_budget(kUnlimitedBudget, *clock_, start_seconds_);
  }

  const double child_seconds = budget_seconds_ * clamp_ratio(ratio);
  return frame_budget(child_seconds, *clock_, start_seconds_);
}

double frame_budget::current_seconds() const noexcept { return clock_ != nullptr ? clock_->now() : 0.0; }

double frame_budget::remaining_at(double now) const noexcept {
  if (is_unlimited()) {
    return std::numeric_limits<double>::max();
  }

  const double elapsed = now - start_seconds_;
  const double remaining = budget_seconds_ - (elapsed > 0.0 ? elapsed : 0.0);
  return remaining > 0.0 ? remaining : 0.0;
}

} // namespace cyberverse::runtime

/**
 * @file frame_budget.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Task budget
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

namespace cyberverse {
namespace runtime {

class frame_clock;

/**
 * @brief Remaining-time budget for work executed inside one frame.
 *
 * A budget is always bound to a @ref frame_clock and starts counting when it is
 * constructed. `has_remaining()` tells a task whether it may continue and
 * `remaining_seconds()` gives the amount left. A negative or non-finite budget
 * means unlimited: `has_remaining()` always returns true and
 * `remaining_seconds()` returns `std::numeric_limits<double>::max()`.
 *
 * The clock must outlive the budget and every sub-budget derived from it. There
 * is intentionally no default constructor: callers must choose the frame's time
 * source instead of accidentally measuring against the wall clock.
 */
class frame_budget {
public:
  /**
   * @brief Start a budget against a monotonic clock.
   *
   * @param seconds Time available from now, in seconds. Negative or non-finite
   * values mean unlimited.
   * @param clock Clock used for all elapsed-time queries. It must outlive this
   * budget and any sub-budget.
   */
  frame_budget(double seconds, frame_clock& clock) noexcept;

  /** @brief Return whether this budget is unlimited. */
  bool is_unlimited() const noexcept;

  /**
   * @brief Return whether any budget remains.
   *
   * Unlimited budgets always return true. A finite budget with exactly zero
   * remaining returns false.
   */
  bool has_remaining() const noexcept;

  /**
   * @brief Return the remaining time in seconds.
   *
   * Finite budgets never return a negative value. Unlimited budgets return
   * `std::numeric_limits<double>::max()`.
   */
  double remaining_seconds() const noexcept;

  /** @brief Return the elapsed time since construction, in seconds. */
  double used_seconds() const noexcept;

  /**
   * @brief Return the absolute deadline on the bound clock.
   *
   * Finite budgets return `start + budget`. Unlimited budgets return
   * `std::numeric_limits<double>::infinity()`; check @ref is_unlimited before
   * using the value as a sleep deadline.
   */
  double deadline_seconds() const noexcept;

  /**
   * @brief Create a sub-budget covering a fraction of the current remainder.
   *
   * The sub-budget starts now, so its @ref used_seconds begins at zero. The
   * ratio is clamped to `[0, 1]`; a non-finite or negative ratio creates an
   * exhausted budget. The parent continues counting independently; no time is
   * reserved or subtracted from it.
   *
   * An unlimited parent returns an unlimited sub-budget starting now.
   *
   * @see share for a frame-aligned fraction of the parent's total budget.
   */
  frame_budget sub(double ratio) const noexcept;

  /**
   * @brief Create a sub-budget covering a fraction of the parent's total budget.
   *
   * The sub-budget shares the parent's start, so its @ref used_seconds includes
   * time already elapsed in the parent. Its total budget is the parent's total
   * budget multiplied by the ratio, so its deadline is earlier than the parent's
   * unless the ratio is 1. The ratio is clamped to `[0, 1]`; a non-finite or
   * negative ratio creates an exhausted budget. This is the frame-aligned
   * counterpart to @ref sub.
   *
   * An unlimited parent returns an unlimited sub-budget with the same start.
   */
  frame_budget share(double ratio) const noexcept;

private:
  frame_budget(double seconds, frame_clock& clock, double start_seconds) noexcept;

  double current_seconds() const noexcept;
  double remaining_at(double now) const noexcept;

  frame_clock* clock_ = nullptr;
  double start_seconds_ = 0.0;
  double budget_seconds_ = 0.0;
};

} // namespace runtime
} // namespace cyberverse

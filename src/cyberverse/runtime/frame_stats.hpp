/**
 * @file frame_stats.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Smoothed frame statistics and budget/jitter detection
 * @version 0.1
 * @date 2026-10-22
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cyberverse {
namespace runtime {

/**
 * @brief Read-only snapshot of one stage's latest frame.
 *
 * The name is a view into the owning pipeline's stage storage. It is valid
 * until a stage is added, erased, or cleared.
 */
struct frame_stage_stats {
  std::string_view name;
  uint64_t stage_id = 0;
  int group = 0;
  bool enabled = true;
  bool receive_update = true;
  bool receive_render = true;
  double update_time = 0.0;
  double render_time = 0.0;
  int fixed_steps = 0;
  double alpha = 0.0;
  double clock_delta = 0.0;
  uint64_t tick = 0;
};

/**
 * @brief One measured frame.
 *
 * All times are in seconds. Invalid (negative or non-finite) values are
 * treated as zero by @ref frame_stats::record_frame.
 */
struct frame_sample {
  /** Wall-clock interval between two frame starts, including any throttle wait. */
  double frame_time = 0.0;

  /** Time spent executing logic updates in this frame. */
  double logic_time = 0.0;

  /** Time spent rendering and presenting in this frame. */
  double render_time = 0.0;

  /** Number of fixed logic steps executed in this frame. */
  int logic_steps = 0;
};

/**
 * @brief Exponentially smoothed frame timing and health flags.
 *
 * Every recorded sample updates three independent EMAs: frame interval, logic
 * time, and render time. The first sample initializes the EMAs directly; it
 * does not raise the jitter or over-budget flags because startup work is not a
 * representative steady-state frame.
 *
 * Budget semantics: @ref budget is the frame interval the runtime is trying
 * to hit (usually `1 / target_render_hz`). A frame is "over budget" when its
 * interval exceeds the budget by more than @ref over_budget_ratio for
 * @ref over_budget_streak_threshold consecutive frames. Jitter is detected by
 * comparing the raw frame interval with the previous smoothed interval.
 */
class frame_stats {
public:
  /** Consecutive frames above @ref over_budget_ratio that raise @ref is_over_budget. */
  static constexpr int over_budget_streak_threshold = 3;

  /** Consecutive frames deviating from the smoothed interval that raise @ref is_jittering. */
  static constexpr int jitter_streak_threshold = 4;

  /** Relative deviation above which a frame counts as jitter. */
  static constexpr double jitter_ratio = 0.2;

  /** Multiplier above which a frame interval counts as over budget. */
  static constexpr double over_budget_ratio = 1.1;

  /** Maximum number of samples retained by the rolling window. */
  static constexpr std::size_t max_window_size = 256;

  /** Default rolling-window capacity (about two seconds at 60 Hz). */
  static constexpr std::size_t default_window_size = 120;

  /**
   * @brief Construct an empty statistics collector.
   *
   * @param ema_alpha Smoothing factor in `(0, 1]`; invalid values fall back to 0.1.
   * @param budget Frame budget in seconds; invalid values fall back to 1/60 s.
   * @param window_size Rolling-window capacity, clamped to
   * `[1, max_window_size]`.
   */
  explicit frame_stats(double ema_alpha = 0.1,
                       double budget = 1.0 / 60.0,
                       std::size_t window_size = default_window_size) noexcept;

  /**
   * @brief Record one frame and refresh the smoothed values and flags.
   *
   * The first call only initializes the EMAs. Subsequent calls update the
   * EMA with `alpha * sample + (1 - alpha) * previous` and evaluate the
   * jitter/over-budget counters.
   */
  void record_frame(const frame_sample& sample) noexcept;

  /**
   * @brief Clear all samples and flags while keeping alpha and budget.
   */
  void reset() noexcept;

  /** @brief Smoothed wall-clock frame interval in seconds (0 before the first sample). */
  double smoothed_frame_time() const noexcept;

  /** @brief Smoothed logic time in seconds (0 before the first sample). */
  double smoothed_logic_time() const noexcept;

  /** @brief Smoothed render time in seconds (0 before the first sample). */
  double smoothed_render_time() const noexcept;

  /**
   * @brief Instantaneous frames per second derived from the smoothed interval.
   *
   * Returns 0 before the first sample or when the smoothed interval is zero.
   */
  double average_fps() const noexcept;

  /** @brief Smoothed logic time divided by the frame budget. */
  double logic_utilization() const noexcept;

  /** @brief Smoothed render time divided by the frame budget. */
  double render_utilization() const noexcept;

  /**
   * @brief Return whether the last @ref jitter_streak_threshold frames jittered.
   */
  bool is_jittering() const noexcept;

  /**
   * @brief Return whether the last @ref over_budget_streak_threshold frames were over budget.
   */
  bool is_over_budget() const noexcept;

  /** @brief Number of recorded samples since construction or @ref reset. */
  uint64_t total_frames() const noexcept;

  /**
   * @brief Change the frame budget used by utilization and over-budget checks.
   *
   * Changing the budget resets the over-budget streak; the EMAs are kept.
   * Invalid (non-positive/non-finite) values are ignored.
   */
  void set_budget(double seconds) noexcept;

  /** @brief Frame budget currently used by this collector, in seconds. */
  double budget() const noexcept;

  /** @brief Smallest frame interval in the rolling window (0 when empty). */
  double min_frame_time() const noexcept;

  /** @brief Largest frame interval in the rolling window (0 when empty). */
  double max_frame_time() const noexcept;

  /**
   * @brief Percentile of the rolling window frame intervals.
   *
   * @param p Fraction in `[0, 1]`; values outside the range are clamped.
   * @return Linearly interpolated percentile, or 0 when the window is empty.
   * The query uses stack storage and performs no heap allocation.
   */
  double percentile(double p) const noexcept;

  /** @brief Budget minus the smoothed frame interval in seconds. */
  double budget_margin() const noexcept;

  /** @brief Rolling-window capacity in samples. */
  std::size_t window_size() const noexcept;

  /** @brief Number of samples currently in the rolling window. */
  std::size_t window_sample_count() const noexcept;

private:
  void record_window_sample(double frame_time) noexcept;

  double ema_alpha_;
  double budget_;
  double smoothed_frame_time_ = 0.0;
  double smoothed_logic_time_ = 0.0;
  double smoothed_render_time_ = 0.0;
  uint64_t total_frames_ = 0;
  int jitter_streak_ = 0;
  int over_budget_streak_ = 0;
  bool has_sample_ = false;
  std::size_t window_capacity_ = default_window_size;
  std::size_t window_count_ = 0;
  std::size_t window_next_ = 0;
  std::array<double, max_window_size> window_{};
};

} // namespace runtime
} // namespace cyberverse

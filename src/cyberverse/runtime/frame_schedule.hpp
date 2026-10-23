/**
 * @file frame_schedule.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Render pacing policy and per-stage fixed-step schedulers
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <optional>

namespace cyberverse {
namespace runtime {

class frame_stats;

/**
 * @brief Configuration for @ref frame_schedule and its schedulers.
 *
 * `frame_schedule` owns the render pacing policy (target rate, stabilization)
 * and the shared logic-step configuration. Each frame stage owns an independent
 * @ref frame_schedule::fixed_step_scheduler created by
 * @ref frame_schedule::new_fixed_step_scheduler, so stages can accumulate
 * different clock deltas without sharing an accumulator. A scheduler follows
 * the schedule's logic configuration live; only explicitly overridden fields
 * are pinned.
 */
struct schedule_config {
  /** Fixed logic frequency. Non-positive/non-finite values fall back to 60 Hz. */
  double target_logic_hz = 60.0;

  /** Upper bound for one stage delta in seconds, used to prevent a death spiral. */
  double max_frame_time = 0.25;

  /** Maximum number of fixed logic steps one stage may execute in a frame. */
  int max_logic_steps_per_frame = 5;

  /**
   * Desired render rate in Hz. Zero (or a negative value) means unlimited:
   * no throttle wait is requested and the caller presents as fast as possible.
   */
  double target_render_hz = 60.0;

  /**
   * Lower bound used by automatic render-rate stabilization. When the frame
   * rate is consistently missed, the schedule reduces the target towards this
   * value instead of oscillating between a fast and a slow frame. Invalid
   * values fall back to `target_render_hz`, which disables adaptation.
   */
  double min_render_hz = 30.0;

  /** Enable automatic render-rate stabilization through @ref frame_schedule::observe. */
  bool auto_render_hz = true;

  /** Number of observed frames between two automatic target-rate adjustments. */
  int auto_adjust_interval = 30;
};

/**
 * @brief Result of advancing a fixed-step scheduler by one frame delta.
 */
struct step_result {
  /** Number of fixed logic steps the caller should execute now. */
  int logic_steps = 0;

  /** Fraction in `[0, 1)` between the previous and the next logic step. */
  double alpha = 0.0;

  /** True when accumulated time was discarded to prevent a death spiral. */
  bool dropped_backlog = false;
};

/**
 * @brief Render pacing policy plus the factory for per-stage schedulers.
 *
 * The schedule owns no clock and no accumulator. It answers two questions:
 * - How fast should the pipeline present? (@ref target_render_hz,
 *   @ref should_throttle, @ref observe)
 * - What fixed logic step and catch-up limits should stage schedulers use?
 *   (@ref logic_step, @ref config, @ref new_fixed_step_scheduler)
 *
 * Invalid configuration values are normalized on construction and by the
 * mutators; a negative or non-finite render rate means "unlimited".
 */
class frame_schedule {
public:
  class fixed_step_scheduler;

  /**
   * @brief Partial logic-step overrides pinned by a scheduler.
   *
   * Every engaged field stops following later changes to the owning
   * @ref frame_schedule. Disengaged fields continue to inherit the schedule
   * live. Engaged values are normalized like @ref schedule_config, so an
   * explicit zero/invalid value uses the documented fallback instead of
   * falling back to inheritance. This differs from a full @ref schedule_config,
   * where every value is treated as an explicit stage setting.
   */
  struct fixed_step_overrides {
    /** Pinned fixed logic frequency; disengaged inherits the schedule. */
    std::optional<double> target_logic_hz;

    /** Pinned delta clamp in seconds; disengaged inherits the schedule. */
    std::optional<double> max_frame_time;

    /** Pinned catch-up limit; disengaged inherits the schedule. */
    std::optional<int> max_logic_steps_per_frame;
  };

  explicit frame_schedule(const schedule_config& config = {}) noexcept;

  /**
   * @brief Set the desired render rate.
   *
   * This replaces both the desired rate and the currently active adaptive
   * rate. Zero or a negative/non-finite value means unlimited.
   *
   * @param hz Render rate in Hz.
   */
  void target_render_hz(double hz) noexcept;

  /**
   * @brief Replace the logic-step frequency.
   *
   * Schedulers that inherit this field pick up the new value on their next
   * step; their accumulators are preserved. Invalid values fall back to 60 Hz.
   *
   * @param hz Fixed logic frequency in Hz.
   */
  void target_logic_hz(double hz) noexcept;

  /**
   * @brief Replace the per-frame delta clamp.
   *
   * Inheriting schedulers pick up the new value on their next step. Invalid
   * values fall back to 0.25 seconds.
   *
   * @param seconds Maximum delta in seconds.
   */
  void max_frame_time(double seconds) noexcept;

  /**
   * @brief Replace the maximum fixed steps executed per frame.
   *
   * Inheriting schedulers pick up the new value on their next step. Values
   * below 1 fall back to 5.
   *
   * @param steps Maximum catch-up steps.
   */
  void max_logic_steps_per_frame(int steps) noexcept;

  /**
   * @brief Replace the whole normalized configuration.
   *
   * This is equivalent to constructing a new schedule: the desired and active
   * render rates are reset from `config`, and adaptive adjustment restarts.
   * Inheriting schedulers observe the new logic configuration on their next
   * step and keep their accumulators.
   */
  void set_config(const schedule_config& config) noexcept;

  /** @brief Current render rate in Hz, including automatic adaptation. */
  double target_render_hz() const noexcept;

  /** @brief Render rate configured by the caller, ignoring adaptation. */
  double desired_render_hz() const noexcept;

  /** @brief Return whether rendering is unlimited. */
  bool is_unlimited() const noexcept;

  /**
   * @brief Return whether the caller should wait before presenting.
   *
   * Always false when unlimited. When true, the caller should wait for
   * `target_frame_time() - elapsed_since_last_present` seconds.
   */
  bool should_throttle(double elapsed_since_last_present) const noexcept;

  /** @brief One render frame interval in seconds; 0 when unlimited. */
  double target_frame_time() const noexcept;

  /**
   * @brief Feed observed frame statistics into automatic rate stabilization.
   *
   * When `auto_render_hz` is enabled, the desired render rate is lowered after
   * consistently missed frames and slowly recovered while there is CPU
   * headroom. The method is a no-op for unlimited schedules and when the
   * configured minimum equals the desired rate. It is normally called once per
   * frame by @ref frame_pipeline after recording a sample.
   */
  void observe(const frame_stats& stats) noexcept;

  /** @brief Duration of one fixed logic step in seconds. */
  double logic_step() const noexcept;

  /** @brief Read-only access to the normalized configuration. */
  const schedule_config& config() const noexcept;

  /**
   * @brief Create an independent scheduler that inherits all logic settings.
   *
   * The scheduler holds only an accumulator and a reference to this schedule;
   * it reads the current logic configuration on every `step`/`logic_step` call.
   * Later changes through @ref target_logic_hz, @ref max_frame_time,
   * @ref max_logic_steps_per_frame, or @ref set_config therefore apply
   * automatically. Each stage must own its own scheduler; sharing one would mix
   * stage clocks into a single accumulator.
   *
   * The schedule must outlive the returned scheduler. Pipelines satisfy this
   * because they own their schedule; a scheduler constructed from a temporary
   * or a copied schedule follows that exact instance, not the copy.
   */
  fixed_step_scheduler new_fixed_step_scheduler() const noexcept;

  /**
   * @brief Create an independent scheduler with partially pinned settings.
   *
   * Engaged fields in @p overrides are fixed at their explicit values; every
   * other logic setting continues to follow this schedule live. Use this
   * overload for stages that need a different logic frequency or catch-up
   * limits while still inheriting later schedule changes.
   */
  fixed_step_scheduler new_fixed_step_scheduler(const fixed_step_overrides& overrides) const noexcept;

private:
  schedule_config config_;
  double logic_step_ = 1.0 / 60.0;
  double desired_render_hz_ = 60.0;
  double target_render_hz_ = 60.0;
  int frames_since_adjust_ = 0;
};

/**
 * @brief Independent fixed-step accumulator for one frame stage.
 *
 * The scheduler owns only an accumulator plus either a live schedule reference
 * or a standalone configuration. It has no clock and no thread affinity: the
 * stage clock's effective delta is supplied by @ref frame_pipeline. Two
 * schedulers created from the same schedule can therefore accumulate different
 * deltas (for example a one-times game clock and a half-speed slow-motion
 * clock) without affecting each other.
 *
 * An inherited scheduler resolves the schedule's current logic configuration on
 * every `step`/`logic_step` call. Engaged @ref frame_schedule::fixed_step_overrides
 * fields stay pinned; everything else follows the schedule. The owner must
 * outlive the scheduler. A scheduler constructed from a @ref schedule_config is
 * standalone and never follows another schedule.
 */
class frame_schedule::fixed_step_scheduler {
public:
  /**
   * @brief Create a scheduler that inherits every logic setting.
   *
   * @param p_owner Schedule to inherit from. A null pointer uses the default
   * configuration and stays valid without an owner.
   */
  explicit fixed_step_scheduler(const frame_schedule* p_owner) noexcept;

  /**
   * @brief Create a scheduler that inherits with partially pinned settings.
   *
   * @param p_owner Schedule to inherit from. A null pointer inherits the
   * default configuration.
   * @param overrides Fields pinned at their explicit values.
   */
  fixed_step_scheduler(const frame_schedule* p_owner, const fixed_step_overrides& overrides) noexcept;

  /**
   * @brief Create a standalone scheduler from a full, normalized configuration.
   *
   * The scheduler copies and normalizes @p config and never follows another
   * schedule.
   */
  explicit fixed_step_scheduler(const schedule_config& config) noexcept;

  /** @brief Resolved logic configuration, including inherited fields. */
  schedule_config config() const noexcept;

  /**
   * @brief Advance this scheduler's accumulator.
   *
   * The delta is sanitized (negative/non-finite becomes zero), clamped to
   * `max_frame_time`, and converted to zero or more fixed logic steps. Excess
   * whole steps are discarded and `dropped_backlog` is set when more steps are
   * pending than `max_logic_steps_per_frame`.
   */
  step_result step(double delta) noexcept;

  /** @brief Clear this scheduler's accumulator. */
  void reset() noexcept;

  /** @brief Duration of one fixed logic step, resolved from the current configuration. */
  double logic_step() const noexcept;

  /** @brief Unconsumed time currently held by this scheduler, in seconds. */
  double accumulator() const noexcept;

private:
  schedule_config resolve_config() const noexcept;

  const frame_schedule* owner_ = nullptr;
  schedule_config standalone_config_{};
  fixed_step_overrides overrides_{};
  double accumulator_ = 0.0;
};

} // namespace runtime
} // namespace cyberverse

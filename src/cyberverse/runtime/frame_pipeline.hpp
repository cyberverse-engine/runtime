/**
 * @file frame_pipeline.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Stage-driven frame loop
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cyberverse/runtime/clock_node.hpp"
#include "cyberverse/runtime/clock_tree.hpp"
#include "cyberverse/runtime/frame_clock.hpp"
#include "cyberverse/runtime/frame_schedule.hpp"
#include "cyberverse/runtime/frame_stage.hpp"
#include "cyberverse/runtime/frame_stats.hpp"

namespace cyberverse {
namespace runtime {

/**
 * @brief Per-stage ordering and dispatch options.
 *
 * Stages are sorted by `update_order` for the update phase and by
 * `render_order` for the render phase. Lower values run first; equal values
 * keep registration order. The two lists are independent, so a stage can
 * update late but render early.
 *
 * `receive_update` / `receive_render` gate only the corresponding action:
 * `on_update` and `on_render`. Frame lifecycle hooks still run for every
 * enabled stage. `enabled == false` disables every stage callback, including
 * lifecycle hooks, without removing the stage.
 *
 * `target_logic_hz`, `max_frame_time`, and `max_logic_steps_per_frame` override
 * the pipeline's fixed-step configuration for this stage. Non-positive values
 * inherit the pipeline schedule.
 */
struct frame_stage_options {
  /** Update priority; lower runs first, ties keep registration order. */
  int update_order = 0;

  /** Render priority; lower runs first, ties keep registration order. */
  int render_order = 0;

  /** When false, the pipeline skips this stage's on_update and scheduler step. */
  bool receive_update = true;

  /** When false, the pipeline skips this stage's on_render. */
  bool receive_render = true;

  /** When false, no stage callbacks run and no update/render time is recorded. */
  bool enabled = true;

  /** Opaque group used by @ref frame_pipeline::set_group_enabled and telemetry. */
  int group = 0;

  /** Fixed logic frequency override; `<= 0` inherits the pipeline schedule. */
  double target_logic_hz = 0.0;

  /** Fixed max frame-time override; `<= 0` inherits the pipeline schedule. */
  double max_frame_time = 0.0;

  /** Fixed catch-up step limit override; `<= 0` inherits the pipeline schedule. */
  int max_logic_steps_per_frame = 0;
};

/**
 * @brief Frame loop that polls, updates stages, renders stages, and presents.
 *
 * The pipeline owns:
 * - one render pacing policy (@ref frame_schedule);
 * - one clock tree, updated once per frame with the real delta;
 * - one statistics collector;
 * - an ordered list of @ref frame_stage entries, each with its own clock node
 *   and fixed-step scheduler.
 *
 * Pipeline responsibilities are deliberately small: obtain the frame delta,
 * poll platform events, update/render every stage, and present. Stage update
 * and render work lives in @ref frame_stage, so a game stage and a UI stage can
 * use different clocks and different step models in the same frame.
 *
 * Frame order:
 * 1. `on_poll()`.
 * 2. Update the clock tree with the frame delta.
 * 3. `on_frame_begin(frame_delta)`, then every enabled stage's `on_frame_begin`.
 * 4. Update every enabled stage in `update_order` (skipping `receive_update ==
 *    false`).
 * 5. `on_render_frame_begin()`, then every enabled stage's
 *    `on_render_frame_begin`.
 * 6. Render every enabled stage in `render_order` (skipping `receive_render ==
 *    false`).
 * 7. Every enabled stage's `on_render_frame_end()`, then
 *    `on_render_frame_end()`.
 * 8. `on_present()`.
 * 9. Record statistics, stage snapshots, and automatic pacing.
 *
 * Lifecycle hooks run for every enabled stage, even when the corresponding
 * receive flag is false. Disabled stages run no callbacks. Ties in either order
 * keep stage registration order.
 *
 * The loop is single-threaded, but @ref request_quit is safe to call from
 * another thread (for example a signal or window callback). Every other
 * operation must run on the loop thread.
 */
class frame_pipeline {
public:
  /**
   * @brief Construct a pipeline around a schedule, clock tree, and pacing clock.
   *
   * @param schedule Render pacing and fixed-step configuration.
   * @param p_clock_tree Clock tree shared with the stages. A null pointer
   * creates a private tree.
   * @param p_pacing_clock Monotonic clock used for wait deadlines. A null
   * pointer creates a @ref steady_frame_clock.
   */
  explicit frame_pipeline(frame_schedule schedule,
                          std::shared_ptr<clock_tree> p_clock_tree = nullptr,
                          std::shared_ptr<frame_clock> p_pacing_clock = nullptr);

  virtual ~frame_pipeline() = default;

  frame_pipeline(const frame_pipeline&) = delete;
  frame_pipeline& operator=(const frame_pipeline&) = delete;
  frame_pipeline(frame_pipeline&&) = delete;
  frame_pipeline& operator=(frame_pipeline&&) = delete;

  /**
   * @brief Run until @ref should_quit or @ref should_quit_requested is true.
   *
   * Each iteration calls @ref tick_clock for the real delta, optionally waits
   * through @ref on_throttle_wait, and executes one frame.
   */
  void run();

  /**
   * @brief Execute exactly one frame with a caller-supplied delta.
   *
   * This is the deterministic frame primitive: it never sleeps and never
   * throttles. The clock tree is advanced by `frame_delta`, then every stage
   * updates and renders. Statistics are recorded and automatic pacing is
   * evaluated.
   *
   * Quit semantics:
   * - a quit from @ref on_poll skips clock update, lifecycle hooks, stages,
   *   and present;
   * - a quit from a frame-begin hook or stage update stops remaining updates
   *   and skips render and present for the whole frame;
   * - a quit from the render phase stops remaining renders, still runs the
   *   render-frame-end hooks, and still presents and records statistics;
   * - a quit from @ref on_present is observed after presentation.
   *
   * @param frame_delta Real seconds since the previous frame start.
   * @return True when the pipeline should continue, false when it has quit.
   */
  bool step_frame(double frame_delta);

  /**
   * @brief Ask the loop to stop after the current frame.
   *
   * Safe to call from another thread.
   */
  void request_quit() noexcept;

  /** @brief Return whether quit has been requested. */
  bool should_quit() const noexcept;

  /**
   * @brief Return the real delta since the previous call, in seconds.
   *
   * `run()` calls this once per iteration instead of reading a hard-coded
   * clock. Implementations may use `std::chrono::steady_clock`, a platform
   * timer, or a synthetic clock in tests. The first call may return zero.
   * Clock, scheduler, and statistics consumers sanitize negative or
   * non-finite deltas to zero.
   */
  virtual double tick_clock() noexcept = 0;

  /** @brief Poll platform events before updating this frame. */
  virtual void on_poll() noexcept = 0;

  /**
   * @brief Frame lifecycle hook called after poll and the clock-tree update,
   * before any stage on_frame_begin or update.
   *
   * @param frame_delta Real frame delta supplied to the pipeline.
   */
  virtual void on_frame_begin(double frame_delta) noexcept;

  /** @brief Render lifecycle hook called after all stage updates and before
   * any stage on_render_frame_begin. */
  virtual void on_render_frame_begin() noexcept;

  /** @brief Render lifecycle hook called after all stage on_render_frame_end
   * and before on_present. */
  virtual void on_render_frame_end() noexcept;

  /** @brief Present all rendered stages. */
  virtual void on_present() noexcept = 0;

  /**
   * @brief External quit condition checked once per frame by @ref run.
   *
   * Typical implementations forward the window close state. Returning true
   * stops the loop without executing another frame.
   */
  virtual bool should_quit_requested() noexcept = 0;

  /**
   * @brief Wait before presenting when the schedule is throttling.
   *
   * The default implementation advances @ref pacing_clock to
   * `now() + seconds` with sleep-until. Tests and custom platforms may override
   * it to inject a synthetic wait.
   *
   * @param seconds Positive wait duration in seconds.
   */
  virtual void on_throttle_wait(double seconds);

  /** @brief Statistics collected by this pipeline. */
  const frame_stats& stats() const noexcept;

  /** @brief Mutable schedule, for runtime target-rate changes. */
  frame_schedule& schedule() noexcept;

  /** @brief Read-only schedule. */
  const frame_schedule& schedule() const noexcept;

  /** @brief Clock tree shared with the stages. */
  clock_tree& clocks() noexcept;

  /** @brief Clock tree shared with the stages. */
  const clock_tree& clocks() const noexcept;

  /** @brief Monotonic clock used for wait deadlines. */
  frame_clock& pacing_clock() noexcept;

  /** @brief Monotonic clock used for wait deadlines. */
  const frame_clock& pacing_clock() const noexcept;

  /** @brief Sentinel returned by @ref stage_id when no stage has the name. */
  static constexpr uint64_t invalid_stage_id = 0;

  /**
   * @brief Per-stage snapshots for the latest frame.
   *
   * The vector is in registration order and is updated in place each frame.
   * Querying it performs no allocation. The name views and the reference are
   * invalidated by add/erase/clear or by another stage mutation.
   */
  const std::vector<frame_stage_stats>& stage_stats() const noexcept;

  /** @brief Stable id of a stage, or @ref invalid_stage_id when absent. */
  uint64_t stage_id(std::string_view name) const noexcept;

  /** @brief Copy the current options of a stage. */
  std::expected<frame_stage_options, std::error_code> stage_options(std::string_view name) const noexcept;

  /**
   * @brief Replace a stage's options.
   *
   * Changing a fixed-step override rebuilds the stage scheduler and clears its
   * accumulator/alpha. Order, receive, enabled, and group changes take effect
   * on the next frame. Returns `std::errc::no_such_file_or_directory` for an
   * unknown stage and `std::errc::operation_in_progress` during a frame.
   */
  std::expected<void, std::error_code> set_stage_options(std::string_view name, frame_stage_options options);

  /**
   * @brief Enable or disable a stage without removing it.
   *
   * A disabled stage runs no callbacks, consumes no ticks, and records zero
   * update/render time. `receive_*` flags are not modified.
   */
  std::expected<void, std::error_code> set_stage_enabled(std::string_view name, bool enabled);

  /**
   * @brief Enable or disable every stage in a group.
   *
   * @return Number of stages whose enabled flag changed; 0 during a frame.
   */
  std::size_t set_group_enabled(int group, bool enabled);

  /**
   * @brief Total number of stage update calls executed so far.
   *
   * For a fixed-step stage this counts logic steps; for a variable-step stage
   * it counts frames. Each stage also has its own tick counter passed to
   * @ref frame_stage::on_update.
   */
  uint64_t tick_count() const noexcept;

  /**
   * @brief Add a stage to the frame.
   *
   * The stage is inserted into the independent update and render order lists
   * using @ref frame_stage_options. Ties keep registration order. Each stage
   * receives a stable id and an independent scheduler built from the stage's
   * fixed-step overrides. The clock node must belong to (or be reachable from)
   * this pipeline's clock tree; a null clock uses the root clock. The call is
   * intended for setup or between frames, not from inside a stage callback.
   *
   * Errors:
   * - `std::errc::invalid_argument` for an empty name or a null stage;
   * - `std::errc::file_exists` for a duplicate name.
   */
  std::expected<void, std::error_code> add_stage(std::string_view name,
                                                 std::unique_ptr<frame_stage> stage,
                                                 std::shared_ptr<clock_node> clock,
                                                 frame_stage_options options);

  /**
   * @brief Remove a stage by name.
   *
   * @return `std::errc::no_such_file_or_directory` when no stage has that name,
   * or `std::errc::operation_in_progress` during a frame.
   */
  std::expected<void, std::error_code> erase_stage(std::string_view name);

  /**
   * @brief Remove every stage.
   *
   * This is a no-op during a frame; use @ref set_stage_enabled or
   * @ref set_group_enabled to disable stages at runtime.
   */
  void clear_stages() noexcept;

  /** @brief Number of registered stages. */
  std::size_t stage_count() const noexcept;

  /** @brief Return whether a stage with this name is registered. */
  bool has_stage(std::string_view name) const noexcept;

private:
  struct stage_entry {
    stage_entry(std::string stage_name,
                std::unique_ptr<frame_stage> stage_ptr,
                std::shared_ptr<clock_node> clock_ptr,
                frame_stage_options stage_options,
                uint64_t stage_id,
                frame_schedule::fixed_step_scheduler scheduler_value)
        : name(std::move(stage_name))
        , stage(std::move(stage_ptr))
        , clock(std::move(clock_ptr))
        , options(stage_options)
        , id(stage_id)
        , scheduler(std::move(scheduler_value)) {}

    std::string name;
    std::unique_ptr<frame_stage> stage;
    std::shared_ptr<clock_node> clock;
    frame_stage_options options;
    uint64_t id = 0;
    frame_schedule::fixed_step_scheduler scheduler;
    double last_alpha = 0.0;
    uint64_t tick = 0;
  };

  void refresh_budget() noexcept;

  /** @brief Rebuild both order index lists after a stage is added or removed. */
  void rebuild_order_lists();

  /** @brief Rebuild stage-stat metadata while preserving numeric history by id. */
  void rebuild_stage_stats();

  frame_schedule schedule_;
  frame_stats stats_;
  std::shared_ptr<clock_tree> clock_tree_;
  std::shared_ptr<frame_clock> pacing_clock_;
  std::atomic<bool> quit_{ false };
  bool stepping_ = false;
  uint64_t tick_ = 0;
  uint64_t next_stage_id_ = 1;
  std::vector<stage_entry> stages_;
  std::vector<std::size_t> update_order_;
  std::vector<std::size_t> render_order_;
  std::vector<frame_stage_stats> stage_stats_;
};

} // namespace runtime
} // namespace cyberverse

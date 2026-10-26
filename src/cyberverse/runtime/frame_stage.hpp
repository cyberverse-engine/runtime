/**
 * @file frame_stage.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Independent update/render stage driven by the frame pipeline
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstdint>

namespace cyberverse {
namespace runtime {

class frame_budget;
struct frame_stage_options;

/**
 * @brief One update/render stage owned by a @ref frame_pipeline.
 *
 * Stages separate update and render work that should run on different clocks.
 * A game stage typically uses a fixed-step scheduler and a game clock, while a
 * UI stage can use a variable step and a UI clock that keeps running while the
 * game is paused.
 *
 * The pipeline updates every stage before rendering any stage, then presents.
 * Update and render traversal are controlled by independent priorities in
 * @ref frame_stage_options; ties keep registration order. The pipeline is
 * responsible for polling and presenting; a stage only advances its own logic
 * and draws its own output.
 *
 * Stage update/render hooks are not `noexcept`; an exception propagates out of
 * @ref frame_pipeline::step_frame and no statistics are recorded for that
 * frame. Frame lifecycle hooks are `noexcept`. Callbacks must not call back
 * into the pipeline in a way that mutates the stage list.
 */
class frame_stage {
public:
  virtual ~frame_stage() = default;

  frame_stage(const frame_stage&) = delete;
  frame_stage& operator=(const frame_stage&) = delete;
  frame_stage(frame_stage&&) = delete;
  frame_stage& operator=(frame_stage&&) = delete;

  /**
   * @brief Return whether this stage uses a fixed-step scheduler.
   *
   * Fixed-step stages receive `on_update(logic_step, tick, budget)` zero or more
   * times per frame and `on_render(clock_delta, alpha, budget)` once.
   * Variable-step stages receive exactly one `on_update(clock_delta, tick,
   * budget)` and `on_render(clock_delta, 1.0, budget)` per frame.
   */
  bool use_fixed_step() const noexcept;

  /**
   * @brief Frame lifecycle hook called after the pipeline's on_frame_begin and
   * before any stage update.
   *
   * Runs for every registered stage, including stages whose
   * `receive_update` flag is false.
   *
   * @param frame_delta Real frame delta supplied to the pipeline.
   */
  virtual void on_frame_begin(double frame_delta) noexcept;

  /**
   * @brief Render lifecycle hook called after the pipeline's
   * on_render_frame_begin and before any stage render.
   *
   * Runs for every registered stage, including stages whose
   * `receive_render` flag is false.
   */
  virtual void on_render_frame_begin() noexcept;

  /**
   * @brief Render lifecycle hook called after all stage renders and before the
   * pipeline's on_render_frame_end.
   *
   * Runs for every registered stage, including stages whose
   * `receive_render` flag is false.
   */
  virtual void on_render_frame_end() noexcept;

  /**
   * @brief Advance one update.
   *
   * @param dt Stage clock delta for variable-step stages, or the fixed logic
   * step for fixed-step stages.
   * @param tick Stage-local update counter, starting at zero and incremented
   * after every call.
   * @param budget Frame budget shared by all stages for this frame. It is valid
   * only during the callback; copy it before storing. The budget is advisory:
   * the pipeline still executes every fixed step returned by the scheduler.
   */
  virtual void on_update(double dt, uint64_t tick, const frame_budget& budget) = 0;

  /**
   * @brief Render the stage.
   *
   * @param dt Stage clock delta for this frame.
   * @param alpha Interpolation fraction in `[0, 1)` for fixed-step stages;
   * always 1.0 for variable-step stages.
   * @param budget Frame budget shared by all stages for this frame. It is valid
   * only during the callback; copy it before storing.
   */
  virtual void on_render(double dt, double alpha, const frame_budget& budget) = 0;

protected:
  /** @brief Construct a stage and choose its update model. */
  explicit frame_stage(bool use_fixed_step) noexcept;

private:
  bool use_fixed_step_;
};

} // namespace runtime
} // namespace cyberverse

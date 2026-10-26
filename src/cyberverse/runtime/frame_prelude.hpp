/**
 * @file frame_prelude.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Budgeted work executed at the start of every frame
 * @version 0.1
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

namespace cyberverse {
namespace runtime {

class frame_budget;

/**
 * @brief Independent work executed once at the start of every frame.
 *
 * Preludes run after poll and the clock-tree update, but before pipeline/stage
 * `on_frame_begin` and before any stage update or render. They are independent
 * of game logic; typical uses are resource-upload checks, GPU capability probes,
 * and other frame-start services.
 *
 * A prelude receives the frame budget shared with update/render. The budget is
 * advisory: the pipeline does not skip remaining preludes, fixed steps, or
 * present when it is exhausted. Preludes should check `has_remaining()` and
 * degrade their own optional work.
 *
 * Exceptions propagate out of @ref frame_pipeline::step_frame like stage
 * update exceptions; no statistics are recorded for that frame.
 */
class frame_prelude {
public:
  virtual ~frame_prelude() = default;

  frame_prelude(const frame_prelude&) = delete;
  frame_prelude& operator=(const frame_prelude&) = delete;
  frame_prelude(frame_prelude&&) = delete;
  frame_prelude& operator=(frame_prelude&&) = delete;

  /**
   * @brief Execute this prelude once for the current frame.
   *
   * @param frame_delta Real frame delta supplied to the pipeline.
   * @param budget Frame budget shared with update/render. The reference is valid
   * only during the callback; copy it before storing.
   */
  virtual void on_frame_start(double frame_delta, const frame_budget& budget) = 0;

protected:
  frame_prelude() = default;
};

} // namespace runtime
} // namespace cyberverse

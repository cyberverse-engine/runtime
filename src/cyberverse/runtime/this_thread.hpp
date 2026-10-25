/**
 * @file this_thread.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief This thread
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

namespace cyberverse {
namespace runtime {

/**
 * @brief Return whether the calling thread has been marked as a render thread.
 *
 * The flag is thread-local and starts false. @ref frame_pipeline::step_frame
 * marks the calling thread before any frame callback, so stages, presenters, and
 * render helpers can assert that they run on the render thread. The flag stays
 * true for the rest of that thread's lifetime.
 *
 * @return True when the calling thread is a render thread.
 */
bool is_this_render_thread() noexcept;

/**
 * @brief Mark the calling thread as a render thread.
 *
 * This is idempotent. @ref frame_pipeline::step_frame calls it automatically;
 * call it directly only when a thread performs render work outside the pipeline.
 * There is intentionally no unset operation: a thread keeps the role until it
 * exits.
 */
void set_this_as_render_thread() noexcept;

} // namespace runtime
} // namespace cyberverse

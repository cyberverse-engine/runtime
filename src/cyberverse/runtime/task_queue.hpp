/**
 * @file task_queue.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Task queue
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <deque>
#include <mutex>
#include <optional>
#include <utility>

#include "cyberverse/runtime/this_thread.hpp"

namespace cyberverse {
namespace runtime {

/**
 * @brief Task queue optimized for the render thread.
 *
 * The queue has two lanes:
 * - a render-thread lane written and read only by the render thread, with no
 *   mutex;
 * - an any-thread lane protected by a mutex, for producers on other threads.
 *
 * Render-thread pushes never take the mutex. Pops always prefer the
 * render-thread lane; the any-thread lane is then either popped one task at a
 * time under the mutex, or drained in a batch through @ref pop_locker.
 *
 * Exactly one thread may be marked as the render thread (see
 * @ref set_this_as_render_thread). The render-thread lane is intentionally
 * unsynchronized: it relies on that single-thread access. `T` must be movable,
 * and move-assignable when tasks are popped.
 */
template <typename T>
class task_queue {
public:
  /**
   * @brief RAII access to the locked any-thread lane.
   *
   * Created only by @ref task_queue::pop_locker. The lock is held until this
   * object is destroyed. Both `try_pop` overloads drain only the any-thread
   * lane and never touch the render-thread lane, so the locked lane cannot be
   * starved by newly submitted render work.
   */
  class pop_locker {
  public:
    pop_locker(const pop_locker&) = delete;
    pop_locker& operator=(const pop_locker&) = delete;
    pop_locker(pop_locker&&) noexcept = default;
    pop_locker& operator=(pop_locker&&) = delete;
    ~pop_locker() = default;

    /**
     * @brief Pop one task from the locked any-thread lane.
     *
     * No mutex is acquired here; the locker owns the lock. This method must be
     * called on the thread that created the locker.
     *
     * @return True when a task was written to @p out.
     */
    bool try_pop(T& out);

    /**
     * @brief Pop one task from the locked any-thread lane into an optional.
     *
     * Convenience overload for drain loops. It never touches the render lane.
     */
    std::optional<T> try_pop();

  private:
    friend class task_queue;

    explicit pop_locker(task_queue& queue);

    task_queue* queue_ = nullptr;
    std::unique_lock<std::mutex> lock_;
  };

private:
  using pop_locker_type = pop_locker;

public:
  task_queue() = default;
  ~task_queue() = default;

  task_queue(const task_queue&) = delete;
  task_queue& operator=(const task_queue&) = delete;
  task_queue(task_queue&&) = delete;
  task_queue& operator=(task_queue&&) = delete;

  /**
   * @brief Push a task onto the appropriate lane.
   *
   * The render thread pushes without locking. Every other thread locks the
   * any-thread lane. The call blocks only when a non-render producer competes
   * for the any-thread mutex.
   */
  void push(T task) {
    if (is_this_render_thread()) {
      render_thread_queue_.push_back(std::move(task));
      return;
    }

    std::lock_guard<std::mutex> lock(any_thread_mutex_);
    any_thread_queue_.push_back(std::move(task));
  }

  /**
   * @brief Pop one task without blocking.
   *
   * On the render thread the render-thread lane is checked first without a
   * lock; then up to one any-thread task is popped under the mutex. Other
   * threads only touch the any-thread lane.
   *
   * Do not call this while a @ref pop_locker is alive; use the locker's
   * `try_pop` instead.
   *
   * @return True when a task was written to @p out.
   */
  bool try_pop(T& out) {
    if (is_this_render_thread() && pop_render_thread(out)) {
      return true;
    }

    std::lock_guard<std::mutex> lock(any_thread_mutex_);
    return pop_any_thread(out);
  }

  /**
   * @brief Lock the any-thread lane and return an RAII locker.
   *
   * The returned locker holds the mutex until it is destroyed. Its `try_pop`
   * overloads pop only the any-thread lane without re-locking; use
   * @ref try_pop for render-first single pops.
   *
   * Do not call @ref try_pop while the locker is alive.
   */
  [[nodiscard]] pop_locker_type pop_locker();

private:
  bool pop_render_thread(T& out) {
    if (render_thread_queue_.empty()) {
      return false;
    }
    out = std::move(render_thread_queue_.front());
    render_thread_queue_.pop_front();
    return true;
  }

  bool pop_any_thread(T& out) {
    if (any_thread_queue_.empty()) {
      return false;
    }
    out = std::move(any_thread_queue_.front());
    any_thread_queue_.pop_front();
    return true;
  }

  std::deque<T> render_thread_queue_;
  std::deque<T> any_thread_queue_;
  std::mutex any_thread_mutex_;
};

template <typename T>
task_queue<T>::pop_locker::pop_locker(task_queue& queue) : queue_(&queue), lock_(queue.any_thread_mutex_) {}

template <typename T>
bool task_queue<T>::pop_locker::try_pop(T& out) {
  return queue_->pop_any_thread(out);
}

template <typename T>
std::optional<T> task_queue<T>::pop_locker::try_pop() {
  if (queue_->any_thread_queue_.empty()) {
    return std::nullopt;
  }

  std::optional<T> task(std::in_place, std::move(queue_->any_thread_queue_.front()));
  queue_->any_thread_queue_.pop_front();
  return task;
}

template <typename T>
typename task_queue<T>::pop_locker_type task_queue<T>::pop_locker() {
  return pop_locker_type(*this);
}

} // namespace runtime
} // namespace cyberverse

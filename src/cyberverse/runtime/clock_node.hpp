/**
 * @file clock_node.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Node in a hierarchical game clock tree
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cyberverse {
namespace runtime {

class clock_tree;

/**
 * @brief One clock in a hierarchical clock tree.
 *
 * A node owns a local pause flag and a local time scale. The effective state is
 * the combination of the node and all of its ancestors:
 * - a node is paused when it or any ancestor is paused;
 * - the effective time scale is the product of all local time scales.
 *
 * Each frame the tree calls @ref update once with the real frame delta. The
 * node then stores its effective delta (@ref delta) and accumulates it into
 * @ref total_time. Paused nodes report a zero delta and do not advance their
 * total time.
 *
 * Nodes are normally created through @ref clock_tree::ensure or
 * @ref clock_node::ensure. They are owned by `std::shared_ptr`, and a child
 * holds a weak reference to its parent, so holding a child after the tree is
 * destroyed is safe; the detached node simply stops inheriting parent state.
 *
 * This class is not thread-safe. Mutate and update it on the loop thread.
 */
class clock_node : public std::enable_shared_from_this<clock_node> {
public:
  /**
   * @brief Construct a standalone clock node.
   *
   * A standalone node has no parent and behaves as a root clock. Prefer
   * @ref clock_tree::ensure when the node belongs to a tree.
   *
   * @param name Debug name of the node.
   */
  explicit clock_node(std::string_view name);

  ~clock_node() = default;

  clock_node(const clock_node&) = delete;
  clock_node& operator=(const clock_node&) = delete;
  clock_node(clock_node&&) = delete;
  clock_node& operator=(clock_node&&) = delete;

  /** @brief Debug name of this clock. */
  std::string_view name() const noexcept;

  /**
   * @brief Pause or resume this node locally.
   *
   * The pause is inherited by all descendants. A paused node still receives
   * @ref update calls, but its delta is zero.
   */
  void set_paused(bool paused) noexcept;

  /**
   * @brief Set the local time scale.
   *
   * The value is clamped to `[0, 100]`; NaN falls back to 1. A scale of zero
   * freezes this node without marking it paused. The effective scale is the
   * product of all local scales on the ancestor chain.
   */
  void set_time_scale(double scale) noexcept;

  /** @brief Return whether this node is paused locally (ignoring ancestors). */
  bool is_paused_locally() const noexcept;

  /** @brief Return this node's local time scale. */
  double local_time_scale() const noexcept;

  /** @brief Return whether this node or any ancestor is paused. */
  bool is_paused() const noexcept;

  /** @brief Return the product of this node's and all ancestors' time scales. */
  double effective_time_scale() const noexcept;

  /** @brief Effective delta of the last @ref update, in seconds. */
  double delta() const noexcept;

  /** @brief Accumulated effective time since creation, in seconds. */
  double total_time() const noexcept;

  /**
   * @brief Apply one real frame delta to this node and its descendants.
   *
   * Negative and non-finite deltas are treated as zero. The node computes its
   * effective delta from its pause/scale state and then recursively updates
   * every child. Call this once per frame on the root, or use
   * @ref clock_tree::update.
   */
  void update(double real_delta) noexcept;

  /** @brief Return whether a direct child with this name exists. */
  bool contains(std::string_view name) const noexcept;

  /**
   * @brief Get or create a direct child.
   *
   * @param name Child name. Must not be empty.
   * @return Reference to the shared owner stored by this node. Use
   * @ref clock_tree::ensure when a value-owned `std::shared_ptr` is preferred.
   *
   * @throws std::invalid_argument when @p name is empty.
   *
   * The reference is invalidated by @ref erase or @ref clear (enlarging the
   * child list keeps references to existing children valid).
   */
  std::shared_ptr<clock_node>& ensure(std::string_view name);

  /**
   * @brief Erase a direct child.
   *
   * The child is detached from its parent before it is removed. A caller that
   * still holds a shared pointer to it keeps a valid, parentless node.
   * Unknown names are ignored.
   */
  void erase(std::string_view name) noexcept;

  /** @brief Detach and remove every direct child. */
  void clear() noexcept;

private:
  friend class clock_tree;

  /** @brief Find a direct child without creating it. */
  std::shared_ptr<clock_node> find_child(std::string_view name) const noexcept;

  /** @brief Get or create a direct child and return its shared owner. */
  std::shared_ptr<clock_node> ensure_child(std::string_view name);

  std::string name_;
  std::deque<std::shared_ptr<clock_node>> children_;
  std::weak_ptr<clock_node> parent_;

  bool local_paused_ = false;
  double local_time_scale_ = 1.0;
  double frame_delta_ = 0.0;
  double total_time_ = 0.0;
};

} // namespace runtime
} // namespace cyberverse

/**
 * @file clock_tree.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Hierarchical clock tree
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <memory>
#include <string_view>

#include "cyberverse/runtime/clock_node.hpp"

namespace cyberverse {
namespace runtime {

/**
 * @brief Root-owned tree of hierarchical clocks.
 *
 * Paths are slash-separated names relative to the root (`"game"`,
 * `"game/ui"`, `"/game/ui"`). Empty path segments are ignored, so `""` and
 * `"/"` refer to the root. The root always exists and cannot be erased.
 *
 * The tree is the bridge between the wall clock and frame stages: call
 * @ref update once per frame with the real delta, then read each stage clock's
 * @ref clock_node::delta. A game clock may be paused or slowed while a UI
 * clock keeps running at its own rate.
 *
 * This class is not thread-safe. Mutate and update it on the loop thread.
 */
class clock_tree {
public:
  /** @brief Construct a tree with a root node named `"root"`. */
  clock_tree();

  ~clock_tree() = default;

  clock_tree(const clock_tree&) = delete;
  clock_tree& operator=(const clock_tree&) = delete;
  clock_tree(clock_tree&&) = delete;
  clock_tree& operator=(clock_tree&&) = delete;

  /** @brief Return the root clock. Never null. */
  std::shared_ptr<clock_node> root() const noexcept;

  /**
   * @brief Look up a path without creating nodes.
   *
   * @return The node, or `nullptr` when any segment is missing. `""` returns
   * the root.
   */
  std::shared_ptr<clock_node> lookup(std::string_view path) const noexcept;

  /**
   * @brief Get or create every node along a path.
   *
   * @return The leaf node. `""` returns the root. Never returns `nullptr` for
   * a valid tree.
   */
  std::shared_ptr<clock_node> ensure(std::string_view path);

  /** @brief Return whether a path exists. */
  bool contains(std::string_view path) const noexcept;

  /**
   * @brief Erase the leaf node at a path.
   *
   * The root cannot be erased; empty or missing paths are ignored. The erased
   * node is detached from its parent, so an external shared pointer keeps
   * working as a parentless clock.
   */
  void erase(std::string_view path) noexcept;

  /** @brief Remove every node below the root. The root is kept. */
  void clear() noexcept;

  /**
   * @brief Update every clock in the tree with one real frame delta.
   *
   * The root and each descendant compute their effective delta and total time.
   * Negative and non-finite deltas are treated as zero.
   */
  void update(double delta) noexcept;

private:
  std::shared_ptr<clock_node> root_;
};

} // namespace runtime
} // namespace cyberverse

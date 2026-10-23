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

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cyberverse/runtime/clock_node.hpp"

namespace cyberverse {
namespace runtime {

/**
 * @brief Read-only state of one clock node at snapshot time.
 */
struct clock_node_snapshot {
  /** Slash-separated path; empty for the root. */
  std::string path;

  /** Node debug name. */
  std::string name;

  bool local_paused = false;
  bool paused = false;
  double local_time_scale = 1.0;
  double effective_time_scale = 1.0;
  double delta = 0.0;
  double total_time = 0.0;
};

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
   * @brief Return whether a slash-separated path is valid.
   *
   * Empty segments are ignored. `.` and `..` segments and embedded NUL bytes
   * are rejected.
   */
  static bool is_valid_path(std::string_view path) noexcept;

  /** @brief Pause or resume the root clock, and therefore every clock. */
  void set_paused(bool paused) noexcept;

  /** @brief Return whether the root clock is paused. */
  bool is_paused() const noexcept;

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
   * @return The leaf node. `""` returns the root. Returns `nullptr` when the
   * path is invalid.
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

  /**
   * @brief Move an existing node under a different parent.
   *
   * @return `std::errc::invalid_argument` for an invalid path, missing nodes,
   * an attempt to move the root, or a move into the node's own subtree;
   * `std::errc::file_exists` when the new parent already has a child with the
   * same name.
   */
  std::expected<void, std::error_code> reparent(std::string_view path, std::string_view new_parent_path);

  /**
   * @brief Deep-copy a node subtree to a new path.
   *
   * Missing target parents are created. Local pause, local time scale, and all
   * descendants are copied; deltas and total times start at zero.
   *
   * @return The cloned node, `std::errc::invalid_argument` for invalid or
   * missing source paths, or `std::errc::file_exists` when the target exists.
   */
  std::expected<std::shared_ptr<clock_node>, std::error_code> clone(std::string_view source_path,
                                                                    std::string_view target_path);

  /** @brief Read the whole tree in deterministic depth-first order. */
  std::vector<clock_node_snapshot> snapshot() const;

  /** @brief Human-readable dump of the tree, one node per line. */
  std::string to_text() const;

  /** @brief JSON dump of the tree snapshot. */
  std::string to_json() const;

private:
  static std::shared_ptr<clock_node> clone_subtree(const std::shared_ptr<clock_node>& source);

  static void snapshot_subtree(const std::shared_ptr<clock_node>& node,
                               std::string_view path,
                               std::vector<clock_node_snapshot>& out);

  std::shared_ptr<clock_node> root_;
};

} // namespace runtime
} // namespace cyberverse

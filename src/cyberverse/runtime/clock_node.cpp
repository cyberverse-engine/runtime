/**
 * @file clock_node.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Clock node implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/clock_node.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cyberverse::runtime {
namespace {

double sanitize_delta(double delta) { return std::isfinite(delta) && delta > 0.0 ? delta : 0.0; }

} // namespace

clock_node::clock_node(std::string_view name) : name_(name) {}

std::string_view clock_node::name() const noexcept { return name_; }

void clock_node::set_paused(bool paused) noexcept { local_paused_ = paused; }

void clock_node::set_time_scale(double scale) noexcept {
  if (!std::isfinite(scale)) {
    scale = 1.0;
  }
  local_time_scale_ = std::clamp(scale, 0.0, 100.0);
}

bool clock_node::is_paused_locally() const noexcept { return local_paused_; }

double clock_node::local_time_scale() const noexcept { return local_time_scale_; }

bool clock_node::is_paused() const noexcept {
  if (local_paused_) {
    return true;
  }
  const std::shared_ptr<clock_node> parent = parent_.lock();
  return parent ? parent->is_paused() : false;
}

double clock_node::effective_time_scale() const noexcept {
  const std::shared_ptr<clock_node> parent = parent_.lock();
  const double parent_scale = parent ? parent->effective_time_scale() : 1.0;
  return local_time_scale_ * parent_scale;
}

double clock_node::delta() const noexcept { return frame_delta_; }

double clock_node::total_time() const noexcept { return total_time_; }

void clock_node::update(double real_delta) noexcept {
  const double delta = sanitize_delta(real_delta);
  frame_delta_ = is_paused() ? 0.0 : delta * effective_time_scale();
  total_time_ += frame_delta_;

  for (const std::shared_ptr<clock_node>& child : children_) {
    child->update(delta);
  }
}

bool clock_node::contains(std::string_view name) const noexcept { return find_child(name) != nullptr; }

std::shared_ptr<clock_node>& clock_node::ensure(std::string_view name) {
  if (name.empty()) {
    throw std::invalid_argument("clock_node::ensure: empty name");
  }

  const auto it = std::find_if(children_.begin(), children_.end(), [name](const std::shared_ptr<clock_node>& child) {
    return child->name() == name;
  });
  if (it != children_.end()) {
    return *it;
  }

  children_.push_back(std::make_shared<clock_node>(name));
  children_.back()->parent_ = weak_from_this();
  return children_.back();
}

std::shared_ptr<clock_node> clock_node::ensure_child(std::string_view name) {
  if (name.empty()) {
    return nullptr;
  }
  return ensure(name);
}

void clock_node::erase(std::string_view name) noexcept {
  const auto it = std::find_if(children_.begin(), children_.end(), [name](const std::shared_ptr<clock_node>& child) {
    return child->name() == name;
  });
  if (it == children_.end()) {
    return;
  }

  (*it)->parent_.reset();
  children_.erase(it);
}

void clock_node::clear() noexcept {
  for (const std::shared_ptr<clock_node>& child : children_) {
    child->parent_.reset();
  }
  children_.clear();
}

std::shared_ptr<clock_node> clock_node::find_child(std::string_view name) const noexcept {
  for (const std::shared_ptr<clock_node>& child : children_) {
    if (child->name() == name) {
      return child;
    }
  }
  return nullptr;
}

} // namespace cyberverse::runtime

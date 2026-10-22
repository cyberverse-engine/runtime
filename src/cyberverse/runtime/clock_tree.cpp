/**
 * @file clock_tree.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Clock tree implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/clock_tree.hpp"

#include <string>
#include <vector>

namespace cyberverse::runtime {
namespace {

std::vector<std::string_view> split_path(std::string_view path) {
  std::vector<std::string_view> parts;
  std::size_t begin = 0;

  while (begin <= path.size()) {
    const std::size_t end = path.find('/', begin);
    const std::size_t length = (end == std::string_view::npos ? path.size() : end) - begin;
    if (length > 0) {
      parts.push_back(path.substr(begin, length));
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }

  return parts;
}

} // namespace

clock_tree::clock_tree() : root_(std::make_shared<clock_node>("root")) {}

std::shared_ptr<clock_node> clock_tree::root() const noexcept { return root_; }

std::shared_ptr<clock_node> clock_tree::lookup(std::string_view path) const noexcept {
  std::shared_ptr<clock_node> current = root_;
  for (const std::string_view part : split_path(path)) {
    current = current->find_child(part);
    if (!current) {
      return nullptr;
    }
  }
  return current;
}

std::shared_ptr<clock_node> clock_tree::ensure(std::string_view path) {
  std::shared_ptr<clock_node> current = root_;
  for (const std::string_view part : split_path(path)) {
    current = current->ensure_child(part);
  }
  return current;
}

bool clock_tree::contains(std::string_view path) const noexcept { return lookup(path) != nullptr; }

void clock_tree::erase(std::string_view path) noexcept {
  const std::vector<std::string_view> parts = split_path(path);
  if (parts.empty()) {
    return;
  }

  std::shared_ptr<clock_node> parent = root_;
  for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
    parent = parent->find_child(parts[i]);
    if (!parent) {
      return;
    }
  }

  parent->erase(parts.back());
}

void clock_tree::clear() noexcept { root_->clear(); }

void clock_tree::update(double delta) noexcept { root_->update(delta); }

} // namespace cyberverse::runtime

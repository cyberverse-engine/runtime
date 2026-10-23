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

#include <algorithm>
#include <iomanip>
#include <sstream>
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

std::string child_path(std::string_view parent, std::string_view name) {
  if (parent.empty()) {
    return std::string(name);
  }
  std::string result(parent);
  result.push_back('/');
  result.append(name);
  return result;
}

bool last_segment(std::string_view path, std::string_view& leaf, std::string_view& parent) {
  std::size_t end = path.size();
  while (end > 0 && path[end - 1] == '/') {
    --end;
  }
  if (end == 0) {
    return false;
  }

  const std::size_t slash = path.rfind('/', end - 1);
  const std::size_t start = slash == std::string_view::npos ? 0 : slash + 1;
  leaf = path.substr(start, end - start);
  parent = path.substr(0, start);
  return true;
}

std::string json_escape(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    switch (character) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(character) < 0x20u) {
        constexpr char hex[] = "0123456789abcdef";
        result += "\\u00";
        result.push_back(hex[(static_cast<unsigned char>(character) >> 4) & 0x0fu]);
        result.push_back(hex[static_cast<unsigned char>(character) & 0x0fu]);
      } else {
        result.push_back(character);
      }
      break;
    }
  }
  return result;
}

} // namespace

clock_tree::clock_tree() : root_(std::make_shared<clock_node>("root")) {}

std::shared_ptr<clock_node> clock_tree::root() const noexcept { return root_; }

bool clock_tree::is_valid_path(std::string_view path) noexcept {
  if (path.find('\0') != std::string_view::npos) {
    return false;
  }

  std::size_t begin = 0;
  while (begin <= path.size()) {
    const std::size_t end = path.find('/', begin);
    const std::size_t length = (end == std::string_view::npos ? path.size() : end) - begin;
    if (length > 0) {
      const std::string_view part = path.substr(begin, length);
      if (part == "." || part == "..") {
        return false;
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return true;
}

void clock_tree::set_paused(bool paused) noexcept { root_->set_paused(paused); }

bool clock_tree::is_paused() const noexcept { return root_->is_paused(); }

std::shared_ptr<clock_node> clock_tree::lookup(std::string_view path) const noexcept {
  if (!is_valid_path(path)) {
    return nullptr;
  }

  std::shared_ptr<clock_node> current = root_;
  std::size_t begin = 0;
  while (begin <= path.size()) {
    const std::size_t end = path.find('/', begin);
    const std::size_t length = (end == std::string_view::npos ? path.size() : end) - begin;
    if (length > 0) {
      current = current->find_child(path.substr(begin, length));
      if (!current) {
        return nullptr;
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return current;
}

std::shared_ptr<clock_node> clock_tree::ensure(std::string_view path) {
  if (!is_valid_path(path)) {
    return nullptr;
  }

  std::shared_ptr<clock_node> current = root_;
  for (const std::string_view part : split_path(path)) {
    current = current->ensure_child(part);
  }
  return current;
}

bool clock_tree::contains(std::string_view path) const noexcept { return lookup(path) != nullptr; }

void clock_tree::erase(std::string_view path) noexcept {
  if (!is_valid_path(path)) {
    return;
  }

  std::string_view leaf;
  std::string_view parent_path;
  if (!last_segment(path, leaf, parent_path)) {
    return;
  }

  std::shared_ptr<clock_node> parent = lookup(parent_path);
  if (parent) {
    parent->erase(leaf);
  }
}

void clock_tree::clear() noexcept { root_->clear(); }

void clock_tree::update(double delta) noexcept { root_->update(delta); }

std::expected<void, std::error_code> clock_tree::reparent(std::string_view path, std::string_view new_parent_path) {
  if (!is_valid_path(path) || !is_valid_path(new_parent_path)) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  const std::vector<std::string_view> parts = split_path(path);
  if (parts.empty()) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  std::shared_ptr<clock_node> old_parent = root_;
  for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
    old_parent = old_parent->find_child(parts[i]);
    if (!old_parent) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }

  const std::shared_ptr<clock_node> source = old_parent->find_child(parts.back());
  const std::shared_ptr<clock_node> new_parent = lookup(new_parent_path);
  if (!source || !new_parent) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  for (std::shared_ptr<clock_node> ancestor = new_parent; ancestor; ancestor = ancestor->parent_.lock()) {
    if (ancestor == source) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }

  if (old_parent == new_parent) {
    return {};
  }
  if (new_parent->contains(source->name())) {
    return std::unexpected(std::make_error_code(std::errc::file_exists));
  }

  const auto it = std::find(old_parent->children_.begin(), old_parent->children_.end(), source);
  if (it != old_parent->children_.end()) {
    old_parent->children_.erase(it);
  }
  source->parent_.reset();

  new_parent->children_.push_back(source);
  source->parent_ = new_parent->weak_from_this();
  return {};
}

std::expected<std::shared_ptr<clock_node>, std::error_code> clock_tree::clone(std::string_view source_path,
                                                                              std::string_view target_path) {
  if (!is_valid_path(source_path) || !is_valid_path(target_path)) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  const std::shared_ptr<clock_node> source = lookup(source_path);
  if (!source) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  const std::vector<std::string_view> source_parts = split_path(source_path);
  const std::vector<std::string_view> target_parts = split_path(target_path);
  if (!source_parts.empty() && target_parts.size() > source_parts.size() &&
      std::equal(source_parts.begin(), source_parts.end(), target_parts.begin())) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (target_parts.empty()) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  std::string parent_path;
  for (std::size_t i = 0; i + 1 < target_parts.size(); ++i) {
    parent_path = child_path(parent_path, target_parts[i]);
  }

  const std::shared_ptr<clock_node> target_parent = ensure(parent_path);
  if (!target_parent) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  for (std::shared_ptr<clock_node> ancestor = target_parent; ancestor; ancestor = ancestor->parent_.lock()) {
    if (ancestor == source) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }
  const std::string_view target_name = target_parts.back();
  if (target_parent->contains(target_name)) {
    return std::unexpected(std::make_error_code(std::errc::file_exists));
  }

  std::shared_ptr<clock_node> copy = clone_subtree(source);
  copy->name_ = std::string(target_name);
  copy->parent_ = target_parent->weak_from_this();
  target_parent->children_.push_back(copy);
  return copy;
}

std::vector<clock_node_snapshot> clock_tree::snapshot() const {
  std::vector<clock_node_snapshot> result;
  snapshot_subtree(root_, std::string_view{}, result);
  return result;
}

std::string clock_tree::to_text() const {
  std::ostringstream output;
  output << std::setprecision(9);
  for (const clock_node_snapshot& node : snapshot()) {
    output << "path=" << (node.path.empty() ? "/" : node.path) << " name=" << node.name
           << " local_paused=" << (node.local_paused ? 1 : 0) << " paused=" << (node.paused ? 1 : 0)
           << " local_scale=" << node.local_time_scale << " effective_scale=" << node.effective_time_scale
           << " delta=" << node.delta << " total=" << node.total_time << '\n';
  }
  return output.str();
}

std::string clock_tree::to_json() const {
  std::ostringstream output;
  output << std::setprecision(17) << "{\"nodes\":[";
  bool first = true;
  for (const clock_node_snapshot& node : snapshot()) {
    if (!first) {
      output << ',';
    }
    first = false;
    output << "{\"path\":\"" << json_escape(node.path) << "\",\"name\":\"" << json_escape(node.name)
           << "\",\"local_paused\":" << (node.local_paused ? "true" : "false")
           << ",\"paused\":" << (node.paused ? "true" : "false") << ",\"local_time_scale\":" << node.local_time_scale
           << ",\"effective_time_scale\":" << node.effective_time_scale << ",\"delta\":" << node.delta
           << ",\"total_time\":" << node.total_time << '}';
  }
  output << "]}";
  return output.str();
}

std::shared_ptr<clock_node> clock_tree::clone_subtree(const std::shared_ptr<clock_node>& source) {
  auto copy = std::make_shared<clock_node>(source->name_);
  copy->local_paused_ = source->local_paused_;
  copy->local_time_scale_ = source->local_time_scale_;

  for (const std::shared_ptr<clock_node>& child : source->children_) {
    std::shared_ptr<clock_node> child_copy = clone_subtree(child);
    child_copy->parent_ = copy->weak_from_this();
    copy->children_.push_back(child_copy);
  }
  return copy;
}

void clock_tree::snapshot_subtree(const std::shared_ptr<clock_node>& node,
                                  std::string_view path,
                                  std::vector<clock_node_snapshot>& out) {
  out.push_back(clock_node_snapshot{ std::string(path),
                                     std::string(node->name_),
                                     node->local_paused_,
                                     node->is_paused(),
                                     node->local_time_scale_,
                                     node->effective_time_scale(),
                                     node->frame_delta_,
                                     node->total_time_ });
  for (const std::shared_ptr<clock_node>& child : node->children_) {
    snapshot_subtree(child, child_path(path, child->name_), out);
  }
}

} // namespace cyberverse::runtime

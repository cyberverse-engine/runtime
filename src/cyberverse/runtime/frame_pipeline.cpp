/**
 * @file frame_pipeline.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Stage-driven frame loop implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/frame_pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

namespace cyberverse::runtime {
namespace {

using clock_type = std::chrono::steady_clock;

double seconds_between(clock_type::time_point begin, clock_type::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

double fallback_budget(const frame_schedule& schedule) {
  const double target = schedule.target_frame_time();
  if (target > 0.0) {
    return target;
  }
  const double logic = schedule.logic_step();
  return logic > 0.0 ? logic : 1.0 / 60.0;
}

} // namespace

frame_pipeline::frame_pipeline(frame_schedule schedule, std::shared_ptr<clock_tree> p_clock_tree)
    : schedule_(schedule)
    , stats_(0.1, fallback_budget(schedule))
    , clock_tree_(p_clock_tree ? std::move(p_clock_tree) : std::make_shared<clock_tree>()) {}

void frame_pipeline::run() {
  while (!should_quit()) {
    if (should_quit_requested()) {
      request_quit();
      break;
    }

    const double frame_time = tick_clock();

    if (schedule_.should_throttle(frame_time)) {
      const double remaining = schedule_.target_frame_time() - frame_time;
      if (remaining > 0.0) {
        on_throttle_wait(remaining);
      }
    }

    if (!step_frame(frame_time)) {
      break;
    }
  }
}

bool frame_pipeline::step_frame(double frame_delta) {
  if (should_quit()) {
    return false;
  }

  on_poll();
  if (should_quit()) {
    return false;
  }

  clock_tree_->update(frame_delta);

  on_frame_begin(frame_delta);
  if (should_quit()) {
    return false;
  }

  for (const std::size_t index : update_order_) {
    stages_[index].stage->on_frame_begin(frame_delta);
    if (should_quit()) {
      return false;
    }
  }

  int fixed_steps = 0;

  const clock_type::time_point logic_start = clock_type::now();
  for (const std::size_t index : update_order_) {
    stage_entry& entry = stages_[index];
    if (!entry.options.receive_update) {
      continue;
    }

    const double stage_delta = entry.clock ? entry.clock->delta() : 0.0;

    if (entry.stage->use_fixed_step()) {
      const step_result result = entry.scheduler.step(stage_delta);
      fixed_steps += result.logic_steps;
      entry.last_alpha = result.alpha;

      for (int i = 0; i < result.logic_steps; ++i) {
        entry.stage->on_update(entry.scheduler.logic_step(), entry.tick++);
        ++tick_;

        if (should_quit()) {
          break;
        }
      }
    } else {
      entry.stage->on_update(stage_delta, entry.tick++);
      ++tick_;
      entry.last_alpha = 1.0;
    }

    if (should_quit()) {
      break;
    }
  }
  const double logic_time = seconds_between(logic_start, clock_type::now());

  if (should_quit()) {
    return false;
  }

  const clock_type::time_point render_start = clock_type::now();
  on_render_frame_begin();

  if (!should_quit()) {
    for (const std::size_t index : render_order_) {
      stages_[index].stage->on_render_frame_begin();
      if (should_quit()) {
        break;
      }
    }
  }

  if (!should_quit()) {
    for (const std::size_t index : render_order_) {
      stage_entry& entry = stages_[index];
      if (!entry.options.receive_render) {
        continue;
      }

      const double stage_delta = entry.clock ? entry.clock->delta() : 0.0;
      entry.stage->on_render(entry.last_alpha, stage_delta);

      if (should_quit()) {
        break;
      }
    }
  }

  for (const std::size_t index : render_order_) {
    stages_[index].stage->on_render_frame_end();
  }
  on_render_frame_end();
  on_present();
  const double render_time = seconds_between(render_start, clock_type::now());

  stats_.record_frame({ frame_delta, logic_time, render_time, fixed_steps });
  schedule_.observe(stats_);
  refresh_budget();

  return !should_quit();
}

void frame_pipeline::request_quit() noexcept { quit_.store(true, std::memory_order_release); }

bool frame_pipeline::should_quit() const noexcept { return quit_.load(std::memory_order_acquire); }

void frame_pipeline::on_frame_begin(double) noexcept {}

void frame_pipeline::on_render_frame_begin() noexcept {}

void frame_pipeline::on_render_frame_end() noexcept {}

void frame_pipeline::on_throttle_wait(double seconds) {
  if (std::isfinite(seconds) && seconds > 0.0) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  }
}

const frame_stats& frame_pipeline::stats() const noexcept { return stats_; }

frame_schedule& frame_pipeline::schedule() noexcept { return schedule_; }

const frame_schedule& frame_pipeline::schedule() const noexcept { return schedule_; }

clock_tree& frame_pipeline::clocks() noexcept { return *clock_tree_; }

const clock_tree& frame_pipeline::clocks() const noexcept { return *clock_tree_; }

uint64_t frame_pipeline::tick_count() const noexcept { return tick_; }

std::expected<void, std::error_code> frame_pipeline::add_stage(std::string_view name,
                                                               std::unique_ptr<frame_stage> stage,
                                                               std::shared_ptr<clock_node> clock,
                                                               frame_stage_options options) {
  if (name.empty() || !stage) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (has_stage(name)) {
    return std::unexpected(std::make_error_code(std::errc::file_exists));
  }

  if (!clock) {
    clock = clock_tree_->root();
  }

  stages_.push_back(stage_entry{ std::string(name), std::move(stage), std::move(clock), options, &schedule_ });
  rebuild_order_lists();
  return {};
}

std::expected<void, std::error_code> frame_pipeline::erase_stage(std::string_view name) {
  const auto it = std::find_if(stages_.begin(), stages_.end(), [name](const stage_entry& entry) {
    return entry.name == name;
  });
  if (it == stages_.end()) {
    return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
  }

  stages_.erase(it);
  rebuild_order_lists();
  return {};
}

void frame_pipeline::clear_stages() noexcept {
  stages_.clear();
  update_order_.clear();
  render_order_.clear();
}

std::size_t frame_pipeline::stage_count() const noexcept { return stages_.size(); }

bool frame_pipeline::has_stage(std::string_view name) const noexcept {
  return std::any_of(stages_.begin(), stages_.end(), [name](const stage_entry& entry) {
    return entry.name == name;
  });
}

void frame_pipeline::rebuild_order_lists() {
  update_order_.resize(stages_.size());
  render_order_.resize(stages_.size());

  for (std::size_t i = 0; i < stages_.size(); ++i) {
    update_order_[i] = i;
    render_order_[i] = i;
  }

  std::sort(update_order_.begin(), update_order_.end(), [this](std::size_t lhs, std::size_t rhs) {
    if (stages_[lhs].options.update_order != stages_[rhs].options.update_order) {
      return stages_[lhs].options.update_order < stages_[rhs].options.update_order;
    }
    return lhs < rhs;
  });

  std::sort(render_order_.begin(), render_order_.end(), [this](std::size_t lhs, std::size_t rhs) {
    if (stages_[lhs].options.render_order != stages_[rhs].options.render_order) {
      return stages_[lhs].options.render_order < stages_[rhs].options.render_order;
    }
    return lhs < rhs;
  });
}

void frame_pipeline::refresh_budget() noexcept { stats_.set_budget(fallback_budget(schedule_)); }

} // namespace cyberverse::runtime

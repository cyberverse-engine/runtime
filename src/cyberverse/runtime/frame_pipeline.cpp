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
#include <utility>

#include "cyberverse/runtime/this_thread.hpp"

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

frame_schedule::fixed_step_overrides scheduler_overrides(const frame_stage_options& options) {
  frame_schedule::fixed_step_overrides overrides;
  if (std::isfinite(options.target_logic_hz) && options.target_logic_hz > 0.0) {
    overrides.target_logic_hz = options.target_logic_hz;
  }
  if (std::isfinite(options.max_frame_time) && options.max_frame_time > 0.0) {
    overrides.max_frame_time = options.max_frame_time;
  }
  if (options.max_logic_steps_per_frame > 0) {
    overrides.max_logic_steps_per_frame = options.max_logic_steps_per_frame;
  }
  return overrides;
}

frame_schedule::fixed_step_scheduler make_stage_scheduler(const frame_schedule& schedule,
                                                          const frame_stage_options& options) {
  return frame_schedule::fixed_step_scheduler(&schedule, scheduler_overrides(options));
}

} // namespace

frame_pipeline::frame_pipeline(frame_schedule schedule,
                               std::shared_ptr<clock_tree> p_clock_tree,
                               std::shared_ptr<frame_clock> p_pacing_clock)
    : schedule_(schedule)
    , stats_(0.1, fallback_budget(schedule))
    , clock_tree_(p_clock_tree ? std::move(p_clock_tree) : std::make_shared<clock_tree>())
    , pacing_clock_(p_pacing_clock ? std::move(p_pacing_clock) : std::make_shared<steady_frame_clock>()) {}

void frame_pipeline::run() {
  double next_deadline = 0.0;
  bool has_deadline = false;

  while (!should_quit()) {
    if (should_quit_requested()) {
      request_quit();
      break;
    }

    const double frame_time = tick_clock();
    frame_budget budget = frame_budget::unlimited();

    if (schedule_.is_unlimited()) {
      has_deadline = false;
    } else {
      const double target = schedule_.target_frame_time();
      const double now = pacing_clock_->now();
      if (!has_deadline) {
        next_deadline = now;
        has_deadline = true;
      }
      next_deadline += target;

      if (next_deadline > now) {
        on_throttle_wait(next_deadline - now);
      }

      // After a severe stall, resynchronize instead of accumulating an unbounded
      // catch-up backlog. Small oversleeps intentionally shorten the next wait.
      const double after = pacing_clock_->now();
      if (after - next_deadline > target) {
        next_deadline = after;
      }

      // Work is allowed until the following deadline. Starting a fresh target
      // here would hide small oversleeps from the budget.
      const double remaining = std::clamp(next_deadline + target - after, 0.0, target);
      budget = frame_budget(remaining, *pacing_clock_);
    }

    if (!step_frame(frame_time, budget)) {
      break;
    }
  }
}

bool frame_pipeline::run_preludes(double frame_delta, const frame_budget& budget) {
  for (frame_prelude_stats& stats : prelude_stats_) {
    stats.time = 0.0;
  }

  for (const std::size_t index : prelude_order_) {
    const clock_type::time_point prelude_start = clock_type::now();
    preludes_[index].prelude->on_frame_start(frame_delta, budget);
    prelude_stats_[index].time = seconds_between(prelude_start, clock_type::now());

    if (should_quit()) {
      return false;
    }
  }
  return true;
}

bool frame_pipeline::step_frame(double frame_delta) { return step_frame(frame_delta, frame_budget::unlimited()); }

bool frame_pipeline::step_frame(double frame_delta, frame_budget budget) {
  if (should_quit() || stepping_) {
    return false;
  }

  struct stepping_guard {
    explicit stepping_guard(bool& flag) : flag_(flag) { flag_ = true; }
    ~stepping_guard() { flag_ = false; }
    bool& flag_;
  } guard(stepping_);

  set_this_as_render_thread();

  on_poll();
  if (should_quit()) {
    return false;
  }

  clock_tree_->update(frame_delta);

  const bool has_preludes = !preludes_.empty();
  const clock_type::time_point prelude_start = clock_type::now();
  if (!run_preludes(frame_delta, budget)) {
    return false;
  }
  const double prelude_time = has_preludes ? seconds_between(prelude_start, clock_type::now()) : 0.0;

  on_frame_begin(frame_delta);
  if (should_quit()) {
    return false;
  }

  for (const std::size_t index : update_order_) {
    if (!stages_[index].options.enabled) {
      continue;
    }
    stages_[index].stage->on_frame_begin(frame_delta);
    if (should_quit()) {
      return false;
    }
  }

  for (std::size_t index = 0; index < stages_.size(); ++index) {
    const stage_entry& entry = stages_[index];
    frame_stage_stats& stage = stage_stats_[index];
    stage.clock_delta = entry.clock ? entry.clock->delta() : 0.0;
    stage.group = entry.options.group;
    stage.enabled = entry.options.enabled;
    stage.receive_update = entry.options.receive_update;
    stage.receive_render = entry.options.receive_render;
    stage.update_time = 0.0;
    stage.render_time = 0.0;
    stage.fixed_steps = 0;
    stage.alpha = entry.last_alpha;
    stage.tick = entry.tick;
  }

  int fixed_steps = 0;

  const clock_type::time_point logic_start = clock_type::now();
  for (const std::size_t index : update_order_) {
    stage_entry& entry = stages_[index];
    frame_stage_stats& stage = stage_stats_[index];

    if (!entry.options.enabled || !entry.options.receive_update) {
      continue;
    }

    const double stage_delta = entry.clock ? entry.clock->delta() : 0.0;
    const clock_type::time_point stage_start = clock_type::now();

    if (entry.stage->use_fixed_step()) {
      // Resolve the step once so a schedule change cannot split one batch of
      // logic steps across two different dt values.
      const double logic_step = entry.scheduler.logic_step();
      const step_result result = entry.scheduler.step(stage_delta);
      fixed_steps += result.logic_steps;
      entry.last_alpha = result.alpha;
      stage.fixed_steps = result.logic_steps;

      for (int i = 0; i < result.logic_steps; ++i) {
        entry.stage->on_update(logic_step, entry.tick++, budget);
        ++tick_;

        if (should_quit()) {
          break;
        }
      }
    } else {
      entry.stage->on_update(stage_delta, entry.tick++, budget);
      ++tick_;
      entry.last_alpha = 1.0;
    }

    stage.update_time = seconds_between(stage_start, clock_type::now());
    stage.alpha = entry.last_alpha;
    stage.tick = entry.tick;

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
      if (!stages_[index].options.enabled) {
        continue;
      }
      stages_[index].stage->on_render_frame_begin();
      if (should_quit()) {
        break;
      }
    }
  }

  if (!should_quit()) {
    for (const std::size_t index : render_order_) {
      stage_entry& entry = stages_[index];
      if (!entry.options.enabled || !entry.options.receive_render) {
        continue;
      }

      const clock_type::time_point stage_start = clock_type::now();
      const double stage_delta = entry.clock ? entry.clock->delta() : 0.0;
      entry.stage->on_render(stage_delta, entry.last_alpha, budget);
      stage_stats_[index].render_time = seconds_between(stage_start, clock_type::now());

      if (should_quit()) {
        break;
      }
    }
  }

  for (const std::size_t index : render_order_) {
    if (!stages_[index].options.enabled) {
      continue;
    }
    stages_[index].stage->on_render_frame_end();
  }
  on_render_frame_end();
  on_present();
  const double render_time = seconds_between(render_start, clock_type::now());

  stats_.record_frame({ frame_delta, logic_time, render_time, fixed_steps, prelude_time });
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
    pacing_clock_->sleep_until(pacing_clock_->now() + seconds);
  }
}

const frame_stats& frame_pipeline::stats() const noexcept { return stats_; }

frame_schedule& frame_pipeline::schedule() noexcept { return schedule_; }

const frame_schedule& frame_pipeline::schedule() const noexcept { return schedule_; }

clock_tree& frame_pipeline::clocks() noexcept { return *clock_tree_; }

const clock_tree& frame_pipeline::clocks() const noexcept { return *clock_tree_; }

frame_clock& frame_pipeline::pacing_clock() noexcept { return *pacing_clock_; }

const frame_clock& frame_pipeline::pacing_clock() const noexcept { return *pacing_clock_; }

const std::vector<frame_prelude_stats>& frame_pipeline::prelude_stats() const noexcept { return prelude_stats_; }

const std::vector<frame_stage_stats>& frame_pipeline::stage_stats() const noexcept { return stage_stats_; }

uint64_t frame_pipeline::tick_count() const noexcept { return tick_; }

uint64_t frame_pipeline::stage_id(std::string_view name) const noexcept {
  const auto it = std::find_if(stages_.begin(), stages_.end(), [name](const stage_entry& entry) {
    return entry.name == name;
  });
  return it == stages_.end() ? invalid_stage_id : it->id;
}

std::expected<frame_stage_options, std::error_code>
frame_pipeline::stage_options(std::string_view name) const noexcept {
  const auto it = std::find_if(stages_.begin(), stages_.end(), [name](const stage_entry& entry) {
    return entry.name == name;
  });
  if (it == stages_.end()) {
    return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
  }
  return it->options;
}

std::expected<void, std::error_code>
frame_pipeline::add_prelude(std::string_view name, std::unique_ptr<frame_prelude> prelude, int order) {
  if (stepping_) {
    return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
  }
  if (name.empty() || !prelude) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (has_prelude(name)) {
    return std::unexpected(std::make_error_code(std::errc::file_exists));
  }

  preludes_.emplace_back(std::string(name), std::move(prelude), order);
  rebuild_prelude_order();
  rebuild_prelude_stats();
  return {};
}

std::expected<void, std::error_code> frame_pipeline::erase_prelude(std::string_view name) {
  if (stepping_) {
    return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
  }

  const auto it = std::find_if(preludes_.begin(), preludes_.end(), [name](const prelude_entry& entry) {
    return entry.name == name;
  });
  if (it == preludes_.end()) {
    return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
  }

  preludes_.erase(it);
  rebuild_prelude_order();
  rebuild_prelude_stats();
  return {};
}

void frame_pipeline::clear_preludes() noexcept {
  if (stepping_) {
    return;
  }
  preludes_.clear();
  prelude_order_.clear();
  prelude_stats_.clear();
}

std::size_t frame_pipeline::prelude_count() const noexcept { return preludes_.size(); }

bool frame_pipeline::has_prelude(std::string_view name) const noexcept {
  return std::any_of(preludes_.begin(), preludes_.end(), [name](const prelude_entry& entry) {
    return entry.name == name;
  });
}

std::expected<void, std::error_code> frame_pipeline::add_stage(std::string_view name,
                                                               std::unique_ptr<frame_stage> stage,
                                                               std::shared_ptr<clock_node> clock,
                                                               frame_stage_options options) {
  if (stepping_) {
    return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
  }
  if (name.empty() || !stage) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (has_stage(name)) {
    return std::unexpected(std::make_error_code(std::errc::file_exists));
  }

  if (!clock) {
    clock = clock_tree_->root();
  }

  stages_.push_back(stage_entry{ std::string(name),
                                 std::move(stage),
                                 std::move(clock),
                                 options,
                                 next_stage_id_++,
                                 make_stage_scheduler(schedule_, options) });
  rebuild_order_lists();
  rebuild_stage_stats();
  return {};
}

std::expected<void, std::error_code> frame_pipeline::erase_stage(std::string_view name) {
  if (stepping_) {
    return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
  }

  const auto it = std::find_if(stages_.begin(), stages_.end(), [name](const stage_entry& entry) {
    return entry.name == name;
  });
  if (it == stages_.end()) {
    return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
  }

  stages_.erase(it);
  rebuild_order_lists();
  rebuild_stage_stats();
  return {};
}

std::expected<void, std::error_code> frame_pipeline::set_stage_options(std::string_view name,
                                                                       frame_stage_options options) {
  if (stepping_) {
    return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
  }

  const auto it = std::find_if(stages_.begin(), stages_.end(), [name](const stage_entry& entry) {
    return entry.name == name;
  });
  if (it == stages_.end()) {
    return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
  }

  const frame_stage_options previous = it->options;
  const bool scheduler_changed = previous.target_logic_hz != options.target_logic_hz ||
                                 previous.max_frame_time != options.max_frame_time ||
                                 previous.max_logic_steps_per_frame != options.max_logic_steps_per_frame;

  it->options = options;
  if (scheduler_changed) {
    it->scheduler = make_stage_scheduler(schedule_, options);
    it->last_alpha = 0.0;
  }

  rebuild_order_lists();
  rebuild_stage_stats();
  return {};
}

std::expected<void, std::error_code> frame_pipeline::set_stage_enabled(std::string_view name, bool enabled) {
  if (stepping_) {
    return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
  }

  const auto it = std::find_if(stages_.begin(), stages_.end(), [name](const stage_entry& entry) {
    return entry.name == name;
  });
  if (it == stages_.end()) {
    return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
  }

  if (it->options.enabled != enabled) {
    it->options.enabled = enabled;
    rebuild_stage_stats();
  }
  return {};
}

std::size_t frame_pipeline::set_group_enabled(int group, bool enabled) {
  if (stepping_) {
    return 0;
  }

  std::size_t changed = 0;
  for (stage_entry& entry : stages_) {
    if (entry.options.group == group && entry.options.enabled != enabled) {
      entry.options.enabled = enabled;
      ++changed;
    }
  }
  if (changed != 0) {
    rebuild_stage_stats();
  }
  return changed;
}

void frame_pipeline::clear_stages() noexcept {
  if (stepping_) {
    return;
  }
  stages_.clear();
  update_order_.clear();
  render_order_.clear();
  stage_stats_.clear();
}

std::size_t frame_pipeline::stage_count() const noexcept { return stages_.size(); }

bool frame_pipeline::has_stage(std::string_view name) const noexcept {
  return std::any_of(stages_.begin(), stages_.end(), [name](const stage_entry& entry) {
    return entry.name == name;
  });
}

void frame_pipeline::rebuild_prelude_order() {
  prelude_order_.resize(preludes_.size());
  for (std::size_t i = 0; i < preludes_.size(); ++i) {
    prelude_order_[i] = i;
  }

  std::sort(prelude_order_.begin(), prelude_order_.end(), [this](std::size_t lhs, std::size_t rhs) {
    if (preludes_[lhs].order != preludes_[rhs].order) {
      return preludes_[lhs].order < preludes_[rhs].order;
    }
    return lhs < rhs;
  });
}

void frame_pipeline::rebuild_prelude_stats() {
  // Do not read the previous snapshots here: their names view preludes_ strings
  // that may have moved during insertion/erase. Timing starts fresh after a
  // registration change.
  std::vector<frame_prelude_stats> next(preludes_.size());

  for (std::size_t i = 0; i < preludes_.size(); ++i) {
    next[i].name = preludes_[i].name;
    next[i].order = preludes_[i].order;
  }

  prelude_stats_ = std::move(next);
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

void frame_pipeline::rebuild_stage_stats() {
  std::vector<frame_stage_stats> next(stages_.size());

  for (std::size_t i = 0; i < stages_.size(); ++i) {
    const stage_entry& entry = stages_[i];
    frame_stage_stats& snapshot = next[i];
    snapshot.name = entry.name;
    snapshot.stage_id = entry.id;
    snapshot.group = entry.options.group;
    snapshot.enabled = entry.options.enabled;
    snapshot.receive_update = entry.options.receive_update;
    snapshot.receive_render = entry.options.receive_render;

    const auto previous =
        std::find_if(stage_stats_.begin(), stage_stats_.end(), [&entry](const frame_stage_stats& old) {
          return old.stage_id == entry.id;
        });
    if (previous != stage_stats_.end()) {
      snapshot.update_time = previous->update_time;
      snapshot.render_time = previous->render_time;
      snapshot.fixed_steps = previous->fixed_steps;
      snapshot.alpha = previous->alpha;
      snapshot.clock_delta = previous->clock_delta;
      snapshot.tick = previous->tick;
    }
  }

  stage_stats_ = std::move(next);
}

void frame_pipeline::refresh_budget() noexcept { stats_.set_budget(fallback_budget(schedule_)); }

} // namespace cyberverse::runtime

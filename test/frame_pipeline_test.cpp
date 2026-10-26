#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cyberverse/runtime/clock_tree.hpp"
#include "cyberverse/runtime/frame_clock.hpp"
#include "cyberverse/runtime/frame_pipeline.hpp"
#include "cyberverse/runtime/frame_schedule.hpp"
#include "cyberverse/runtime/frame_stage.hpp"

using Catch::Approx;
using cyberverse::runtime::clock_node;
using cyberverse::runtime::clock_tree;
using cyberverse::runtime::frame_budget;
using cyberverse::runtime::frame_clock;
using cyberverse::runtime::frame_pipeline;
using cyberverse::runtime::frame_prelude;
using cyberverse::runtime::frame_schedule;
using cyberverse::runtime::frame_stage;
using cyberverse::runtime::frame_stage_options;
using cyberverse::runtime::schedule_config;

namespace {

class manual_frame_clock final : public frame_clock {
public:
  double now() noexcept override { return now_; }

  void sleep_until(double deadline) override {
    if (deadline > now_) {
      now_ = deadline;
      ++sleeps;
    }
  }

  void advance(double seconds) { now_ += seconds; }

  double now_ = 0.0;
  int sleeps = 0;
};

class test_stage final : public frame_stage {
public:
  explicit test_stage(bool use_fixed_step) : frame_stage(use_fixed_step) {}

  void on_frame_begin(double frame_delta) noexcept override {
    ++frame_begins;
    last_frame_delta = frame_delta;
    if (lifecycle) {
      lifecycle->push_back(name + ":fb");
    }
  }

  void on_render_frame_begin() noexcept override {
    ++render_frame_begins;
    if (lifecycle) {
      lifecycle->push_back(name + ":rb");
    }
  }

  void on_render_frame_end() noexcept override {
    ++render_frame_ends;
    if (lifecycle) {
      lifecycle->push_back(name + ":re");
    }
  }

  void on_update(double dt, uint64_t tick, const frame_budget& budget) override {
    ++updates;
    last_dt = dt;
    last_tick = tick;
    update_budget_seen = true;
    update_budget_unlimited = budget.is_unlimited();
    update_budget_remaining = budget.remaining_seconds();
    update_budget_used = budget.used_seconds();
    ticks.push_back(tick);
    if (events) {
      events->push_back(name + ":u");
    }
    if (lifecycle) {
      lifecycle->push_back(name + ":u");
    }
    if (owner && quit_after_updates >= 0 && updates >= quit_after_updates) {
      owner->request_quit();
    }
    if (update_hook) {
      update_hook();
    }
  }

  void on_render(double dt, double alpha, const frame_budget& budget) override {
    ++renders;
    last_alpha = alpha;
    render_dt = dt;
    render_budget_seen = true;
    render_budget_unlimited = budget.is_unlimited();
    render_budget_remaining = budget.remaining_seconds();
    render_budget_used = budget.used_seconds();
    if (events) {
      events->push_back(name + ":r");
    }
    if (lifecycle) {
      lifecycle->push_back(name + ":r");
    }
    if (owner && quit_after_renders >= 0 && renders >= quit_after_renders) {
      owner->request_quit();
    }
  }

  frame_pipeline* owner = nullptr;
  std::vector<std::string>* events = nullptr;
  std::vector<std::string>* lifecycle = nullptr;
  std::function<void()> update_hook;
  std::string name;
  int frame_begins = 0;
  int render_frame_begins = 0;
  int render_frame_ends = 0;
  int updates = 0;
  int renders = 0;
  double last_frame_delta = -1.0;
  double last_dt = -1.0;
  double last_alpha = -1.0;
  double render_dt = -1.0;
  double update_budget_remaining = -1.0;
  double update_budget_used = -1.0;
  double render_budget_remaining = -1.0;
  double render_budget_used = -1.0;
  bool update_budget_seen = false;
  bool render_budget_seen = false;
  bool update_budget_unlimited = false;
  bool render_budget_unlimited = false;
  uint64_t last_tick = 0;
  std::vector<uint64_t> ticks;
  int quit_after_updates = -1;
  int quit_after_renders = -1;
};

class test_prelude final : public frame_prelude {
public:
  void on_frame_start(double frame_delta, const frame_budget& budget) override {
    ++calls;
    last_frame_delta = frame_delta;
    budget_seen = true;
    budget_unlimited = budget.is_unlimited();
    budget_remaining = budget.remaining_seconds();
    budget_used = budget.used_seconds();
    if (lifecycle) {
      lifecycle->push_back(name);
    }
    if (hook) {
      hook();
    }
  }

  std::function<void()> hook;
  std::vector<std::string>* lifecycle = nullptr;
  std::string name;
  int calls = 0;
  double last_frame_delta = -1.0;
  double budget_remaining = -1.0;
  double budget_used = -1.0;
  bool budget_seen = false;
  bool budget_unlimited = false;
};

class throwing_prelude final : public frame_prelude {
public:
  void on_frame_start(double, const frame_budget&) override { throw std::runtime_error("prelude failure"); }
};

class test_pipeline final : public frame_pipeline {
public:
  explicit test_pipeline(frame_schedule schedule,
                         std::shared_ptr<clock_tree> tree = nullptr,
                         std::shared_ptr<frame_clock> pacing = nullptr)
      : frame_pipeline(std::move(schedule), std::move(tree), std::move(pacing)) {}

  double tick_clock() noexcept override {
    ++clock_calls;
    return next_delta;
  }

  void on_poll() noexcept override {
    ++polls;
    if (lifecycle) {
      lifecycle->push_back("poll");
    }
    if (quit_after_polls >= 0 && polls >= quit_after_polls) {
      request_quit();
    }
  }

  void on_frame_begin(double frame_delta) noexcept override {
    ++frame_begins;
    last_frame_delta = frame_delta;
    if (lifecycle) {
      lifecycle->push_back("pipeline:fb");
    }
  }

  void on_render_frame_begin() noexcept override {
    ++render_frame_begins;
    if (lifecycle) {
      lifecycle->push_back("pipeline:rb");
    }
  }

  void on_render_frame_end() noexcept override {
    ++render_frame_ends;
    if (lifecycle) {
      lifecycle->push_back("pipeline:re");
    }
  }

  void on_present() noexcept override {
    ++presents;
    if (lifecycle) {
      lifecycle->push_back("present");
    }
    if (quit_after_presents >= 0 && presents >= quit_after_presents) {
      request_quit();
    }
  }

  bool should_quit_requested() noexcept override { return external_quit; }

  void on_throttle_wait(double seconds) override {
    ++waits;
    wait_times.push_back(seconds);
    last_wait = seconds;
    if (manual_clock) {
      manual_clock->advance(seconds + oversleep);
    }
  }

  int polls = 0;
  int presents = 0;
  int waits = 0;
  int clock_calls = 0;
  int frame_begins = 0;
  int render_frame_begins = 0;
  int render_frame_ends = 0;
  double next_delta = 1.0 / 60.0;
  double last_wait = -1.0;
  double last_frame_delta = -1.0;
  double oversleep = 0.0;
  manual_frame_clock* manual_clock = nullptr;
  bool external_quit = false;
  int quit_after_polls = -1;
  int quit_after_presents = -1;
  std::vector<double> wait_times;
  std::vector<std::string>* lifecycle = nullptr;
};

test_stage* add_stage(frame_pipeline& pipeline,
                      std::string name,
                      bool use_fixed_step,
                      std::shared_ptr<clock_node> clock = nullptr,
                      frame_stage_options options = {},
                      std::vector<std::string>* events = nullptr,
                      std::vector<std::string>* lifecycle = nullptr) {
  auto stage = std::make_unique<test_stage>(use_fixed_step);
  test_stage* raw = stage.get();
  raw->owner = &pipeline;
  raw->events = events;
  raw->lifecycle = lifecycle;
  raw->name = name;

  auto result = pipeline.add_stage(name, std::move(stage), std::move(clock), options);
  REQUIRE(result.has_value());
  return raw;
}

test_prelude*
add_prelude(frame_pipeline& pipeline, std::string name, int order = 0, std::vector<std::string>* lifecycle = nullptr) {
  auto prelude = std::make_unique<test_prelude>();
  test_prelude* raw = prelude.get();
  raw->name = name;
  raw->lifecycle = lifecycle;

  auto result = pipeline.add_prelude(name, std::move(prelude), order);
  REQUIRE(result.has_value());
  return raw;
}

} // namespace

TEST_CASE("frame_pipeline executes a complete fixed frame", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  CHECK(pipeline.step_frame(1.0 / 60.0));

  CHECK(pipeline.polls == 1);
  CHECK(pipeline.presents == 1);
  CHECK(game->updates == 1);
  CHECK(game->renders == 1);
  CHECK(game->last_tick == 0);
  CHECK(game->last_dt == Approx(1.0 / 60.0));
  CHECK(game->last_alpha == Approx(0.0).margin(1e-9));
  CHECK(game->render_dt == Approx(1.0 / 60.0));
  CHECK(pipeline.tick_count() == 1);
  CHECK(pipeline.stats().total_frames() == 1);
  CHECK(pipeline.stats().budget() == Approx(1.0 / 60.0));
  CHECK(pipeline.stats().smoothed_prelude_time() == Approx(0.0));
}

TEST_CASE("frame_pipeline runs preludes in order before frame hooks", "[runtime][pipeline][prelude]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  std::vector<std::string> events;
  pipeline.lifecycle = &events;

  auto* late = add_prelude(pipeline, "late", 10, &events);
  auto* first = add_prelude(pipeline, "first", 0, &events);
  auto* second = add_prelude(pipeline, "second", 0, &events);
  add_stage(pipeline, "game", true, tree->ensure("game"), {}, nullptr, &events);

  CHECK(pipeline.step_frame(1.0 / 60.0));

  CHECK(events == std::vector<std::string>{ "poll",
                                            "first",
                                            "second",
                                            "late",
                                            "pipeline:fb",
                                            "game:fb",
                                            "game:u",
                                            "pipeline:rb",
                                            "game:rb",
                                            "game:r",
                                            "game:re",
                                            "pipeline:re",
                                            "present" });
  CHECK(first->calls == 1);
  CHECK(second->calls == 1);
  CHECK(late->calls == 1);
}

TEST_CASE("frame_pipeline preludes use an unlimited budget by default", "[runtime][pipeline][prelude]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* prelude = add_prelude(pipeline, "prelude");

  CHECK(pipeline.step_frame(1.0 / 60.0));

  CHECK(prelude->calls == 1);
  CHECK(prelude->budget_seen);
  CHECK(prelude->budget_unlimited);
  CHECK(prelude->budget_remaining == std::numeric_limits<double>::max());
  CHECK(prelude->budget_used == Approx(0.0));
  CHECK(prelude->last_frame_delta == Approx(1.0 / 60.0));
}

TEST_CASE("frame_pipeline preludes receive the injected budget", "[runtime][pipeline][prelude]") {
  manual_frame_clock budget_clock;
  frame_budget budget(1.0, budget_clock);
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* prelude = add_prelude(pipeline, "prelude");
  prelude->hook = [&budget_clock] {
    budget_clock.advance(0.2);
  };
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  CHECK(pipeline.step_frame(1.0 / 60.0, budget));

  CHECK_FALSE(prelude->budget_unlimited);
  CHECK(prelude->budget_used == Approx(0.0));
  CHECK(prelude->budget_remaining == Approx(1.0));
  CHECK(game->update_budget_used == Approx(0.2));
  CHECK(game->update_budget_remaining == Approx(0.8));
}

TEST_CASE("frame_pipeline reports prelude timing statistics", "[runtime][pipeline][prelude][stats]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  add_prelude(pipeline, "first", 2);
  add_prelude(pipeline, "second", 1);

  CHECK(pipeline.step_frame(1.0 / 60.0));

  const auto& stats = pipeline.prelude_stats();
  REQUIRE(stats.size() == 2);
  CHECK(stats[0].name == "first");
  CHECK(stats[0].order == 2);
  CHECK(stats[0].time >= 0.0);
  CHECK(stats[1].name == "second");
  CHECK(stats[1].order == 1);
  CHECK(stats[1].time >= 0.0);
  CHECK(pipeline.stats().total_frames() == 1);
  CHECK(pipeline.stats().smoothed_prelude_time() >= 0.0);
  CHECK(pipeline.stats().prelude_utilization() >= 0.0);
}

TEST_CASE("frame_pipeline preludes run even when the frame budget is exhausted", "[runtime][pipeline][prelude]") {
  manual_frame_clock budget_clock;
  frame_budget exhausted(0.0, budget_clock);
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* first = add_prelude(pipeline, "first");
  auto* second = add_prelude(pipeline, "second");
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  CHECK(pipeline.step_frame(0.05, exhausted));

  CHECK(first->calls == 1);
  CHECK(second->calls == 1);
  CHECK(game->updates == 3);
  CHECK(game->renders == 1);
  CHECK(game->update_budget_remaining == Approx(0.0));
}

TEST_CASE("frame_pipeline prelude quit stops the rest of the frame", "[runtime][pipeline][prelude]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* prelude = add_prelude(pipeline, "prelude");
  prelude->hook = [&pipeline] {
    pipeline.request_quit();
  };
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  CHECK_FALSE(pipeline.step_frame(1.0 / 60.0));

  CHECK(prelude->calls == 1);
  CHECK(pipeline.polls == 1);
  CHECK(pipeline.presents == 0);
  CHECK(game->frame_begins == 0);
  CHECK(game->updates == 0);
  CHECK(game->renders == 0);
}

TEST_CASE("frame_pipeline rejects prelude mutation during a frame", "[runtime][pipeline][prelude]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);

  bool add_rejected = false;
  bool erase_rejected = false;
  bool clear_was_noop = false;
  auto* outer = add_prelude(pipeline, "outer");
  outer->hook = [&] {
    auto added = pipeline.add_prelude("inner", std::make_unique<test_prelude>());
    add_rejected = !added && added.error() == std::make_error_code(std::errc::operation_in_progress);

    auto erased = pipeline.erase_prelude("outer");
    erase_rejected = !erased && erased.error() == std::make_error_code(std::errc::operation_in_progress);

    pipeline.clear_preludes();
    clear_was_noop = pipeline.prelude_count() == 1;
  };

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(add_rejected);
  CHECK(erase_rejected);
  CHECK(clear_was_noop);
  CHECK(pipeline.prelude_count() == 1);
}

TEST_CASE("frame_pipeline validates prelude registration", "[runtime][pipeline][prelude]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);

  auto empty = pipeline.add_prelude("", std::make_unique<test_prelude>());
  CHECK_FALSE(empty.has_value());
  CHECK(empty.error() == std::make_error_code(std::errc::invalid_argument));

  auto null = pipeline.add_prelude("null", nullptr);
  CHECK_FALSE(null.has_value());
  CHECK(null.error() == std::make_error_code(std::errc::invalid_argument));

  CHECK(pipeline.add_prelude("valid", std::make_unique<test_prelude>()).has_value());
  auto duplicate = pipeline.add_prelude("valid", std::make_unique<test_prelude>());
  CHECK_FALSE(duplicate.has_value());
  CHECK(duplicate.error() == std::make_error_code(std::errc::file_exists));
  CHECK(pipeline.prelude_count() == 1);
  CHECK(pipeline.has_prelude("valid"));

  auto missing = pipeline.erase_prelude("missing");
  CHECK_FALSE(missing.has_value());
  CHECK(missing.error() == std::make_error_code(std::errc::no_such_file_or_directory));

  pipeline.clear_preludes();
  CHECK(pipeline.prelude_count() == 0);
  CHECK(pipeline.prelude_stats().empty());
  CHECK_FALSE(pipeline.has_prelude("valid"));
}

TEST_CASE("frame_pipeline propagates prelude exceptions", "[runtime][pipeline][prelude]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  CHECK(pipeline.add_prelude("throwing", std::make_unique<throwing_prelude>()).has_value());

  CHECK_THROWS_AS(pipeline.step_frame(1.0 / 60.0), std::runtime_error);
  CHECK(pipeline.presents == 0);
  CHECK(pipeline.stats().total_frames() == 0);
}

TEST_CASE("frame_pipeline gives callbacks an unlimited budget by default", "[runtime][pipeline][budget]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  CHECK(pipeline.step_frame(1.0 / 60.0));

  CHECK(game->update_budget_seen);
  CHECK(game->render_budget_seen);
  CHECK(game->update_budget_unlimited);
  CHECK(game->render_budget_unlimited);
  CHECK(game->update_budget_remaining == std::numeric_limits<double>::max());
  CHECK(game->render_budget_remaining == std::numeric_limits<double>::max());
  CHECK(game->update_budget_used == Approx(0.0));
  CHECK(game->render_budget_used == Approx(0.0));
}

TEST_CASE("frame_pipeline shares an injected budget across stages", "[runtime][pipeline][budget]") {
  manual_frame_clock budget_clock;
  frame_budget budget(1.0, budget_clock);
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* first = add_stage(pipeline, "first", true, tree->ensure("first"));
  auto* second = add_stage(pipeline, "second", true, tree->ensure("second"));
  first->update_hook = [&budget_clock] {
    budget_clock.advance(0.25);
  };

  CHECK(pipeline.step_frame(1.0 / 60.0, budget));

  CHECK_FALSE(first->update_budget_unlimited);
  CHECK(first->update_budget_used == Approx(0.0));
  CHECK(first->render_budget_used == Approx(0.25));
  CHECK(second->update_budget_used == Approx(0.25));
  CHECK(second->update_budget_remaining == Approx(0.75));
  CHECK(second->render_budget_used == Approx(0.25));
}

TEST_CASE("frame_pipeline does not skip fixed steps when the budget is exhausted", "[runtime][pipeline][budget]") {
  manual_frame_clock budget_clock;
  frame_budget exhausted(0.0, budget_clock);
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  CHECK(pipeline.step_frame(0.05, exhausted));

  CHECK(game->updates == 3);
  CHECK(game->renders == 1);
  CHECK(game->update_budget_remaining == Approx(0.0));
}

TEST_CASE("frame_pipeline run creates a finite budget", "[runtime][pipeline][budget][pacing]") {
  auto tree = std::make_shared<clock_tree>();
  auto pacing = std::make_shared<manual_frame_clock>();
  test_pipeline pipeline(frame_schedule{}, tree, pacing);
  pipeline.manual_clock = pacing.get();
  pipeline.quit_after_presents = 1;
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  pipeline.run();

  CHECK(game->update_budget_seen);
  CHECK_FALSE(game->update_budget_unlimited);
  CHECK(game->update_budget_remaining == Approx(1.0 / 60.0));
}

TEST_CASE("frame_pipeline run uses an unlimited budget for an unlimited schedule",
          "[runtime][pipeline][budget][pacing]") {
  schedule_config config;
  config.target_render_hz = 0.0;
  auto tree = std::make_shared<clock_tree>();
  auto pacing = std::make_shared<manual_frame_clock>();
  test_pipeline pipeline(frame_schedule(config), tree, pacing);
  pipeline.manual_clock = pacing.get();
  pipeline.quit_after_presents = 1;
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  pipeline.run();

  CHECK(game->update_budget_seen);
  CHECK(game->update_budget_unlimited);
  CHECK(game->update_budget_remaining == std::numeric_limits<double>::max());
}

TEST_CASE("frame_pipeline renders without a logic step on fractional frames", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  CHECK(pipeline.step_frame(1.0 / 120.0));
  CHECK(game->updates == 0);
  CHECK(game->renders == 1);
  CHECK(game->last_alpha == Approx(0.5));

  CHECK(pipeline.step_frame(1.0 / 120.0));
  CHECK(game->updates == 1);
  CHECK(game->last_tick == 0);
  CHECK(pipeline.tick_count() == 1);
}

TEST_CASE("frame_pipeline catches up with multiple fixed steps", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  CHECK(pipeline.step_frame(0.05));

  CHECK(game->updates == 3);
  CHECK(game->renders == 1);
  CHECK(game->ticks == std::vector<uint64_t>{ 0, 1, 2 });
  CHECK(game->last_alpha == Approx(0.0).margin(1e-6));
  CHECK(pipeline.tick_count() == 3);
  CHECK(pipeline.stats().total_frames() == 1);
}

TEST_CASE("frame_pipeline drives variable-step stages once per frame", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* ui = add_stage(pipeline, "ui", false, tree->ensure("ui"));

  CHECK(pipeline.step_frame(0.05));

  CHECK(ui->updates == 1);
  CHECK(ui->renders == 1);
  CHECK(ui->last_dt == Approx(0.05));
  CHECK(ui->last_tick == 0);
  CHECK(ui->last_alpha == Approx(1.0));
  CHECK(ui->render_dt == Approx(0.05));
  CHECK(pipeline.tick_count() == 1);
}

TEST_CASE("frame_pipeline runs stages on independent clocks", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto game_clock = tree->ensure("game");
  auto ui_clock = tree->ensure("ui");
  auto* game = add_stage(pipeline, "game", true, game_clock);
  auto* ui = add_stage(pipeline, "ui", false, ui_clock);

  game_clock->set_paused(true);
  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(game->updates == 0);
  CHECK(game->renders == 1);
  CHECK(game->render_dt == Approx(0.0));
  CHECK(ui->updates == 1);
  CHECK(ui->last_dt == Approx(1.0 / 60.0));

  game_clock->set_paused(false);
  game_clock->set_time_scale(0.5);
  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(game->updates == 0);
  CHECK(game->last_alpha == Approx(0.5));
  CHECK(ui->updates == 2);

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(game->updates == 1);
  CHECK(game->last_alpha == Approx(0.0).margin(1e-9));
  CHECK(ui->updates == 3);
}

TEST_CASE("frame_pipeline keeps fixed-step accumulators independent", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto game_clock = tree->ensure("game");
  auto slow_clock = tree->ensure("game/slow");
  auto* game = add_stage(pipeline, "game", true, game_clock);
  auto* slow = add_stage(pipeline, "slow", true, slow_clock);

  slow_clock->set_time_scale(0.5);

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(game->updates == 1);
  CHECK(slow->updates == 0);
  CHECK(slow->last_alpha == Approx(0.5));

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(game->updates == 2);
  CHECK(slow->updates == 1);
}

TEST_CASE("frame_pipeline uses independent update and render priorities", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  std::vector<std::string> events;

  frame_stage_options a_options;
  a_options.update_order = 10;
  a_options.render_order = 1;
  frame_stage_options b_options;
  b_options.update_order = -1;
  b_options.render_order = 10;
  frame_stage_options c_options;
  c_options.update_order = 10;
  c_options.render_order = 1;

  add_stage(pipeline, "a", true, tree->ensure("a"), a_options, &events);
  add_stage(pipeline, "b", true, tree->ensure("b"), b_options, &events);
  add_stage(pipeline, "c", true, tree->ensure("c"), c_options, &events);

  CHECK(pipeline.step_frame(1.0 / 60.0));

  CHECK(events == std::vector<std::string>{ "b:u", "a:u", "c:u", "a:r", "c:r", "b:r" });
}

TEST_CASE("frame_pipeline receive flags gate update and render", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);

  frame_stage_options ui_options;
  ui_options.receive_update = false;
  auto* ui = add_stage(pipeline, "ui", false, tree->ensure("ui"), ui_options);

  frame_stage_options game_options;
  game_options.receive_render = false;
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"), game_options);

  CHECK(pipeline.step_frame(0.05));

  CHECK(ui->frame_begins == 1);
  CHECK(ui->updates == 0);
  CHECK(ui->renders == 1);
  CHECK(ui->render_frame_begins == 1);
  CHECK(ui->render_frame_ends == 1);

  CHECK(game->frame_begins == 1);
  CHECK(game->updates == 3);
  CHECK(game->renders == 0);
  CHECK(game->render_frame_begins == 1);
  CHECK(game->render_frame_ends == 1);

  CHECK(pipeline.tick_count() == 3);
}

TEST_CASE("frame_pipeline calls lifecycle hooks in order", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  std::vector<std::string> lifecycle;
  pipeline.lifecycle = &lifecycle;

  add_stage(pipeline, "game", true, tree->ensure("game"), {}, nullptr, &lifecycle);

  CHECK(pipeline.step_frame(1.0 / 60.0));

  CHECK(lifecycle == std::vector<std::string>{ "poll",
                                               "pipeline:fb",
                                               "game:fb",
                                               "game:u",
                                               "pipeline:rb",
                                               "game:rb",
                                               "game:r",
                                               "game:re",
                                               "pipeline:re",
                                               "present" });
  CHECK(pipeline.frame_begins == 1);
  CHECK(pipeline.render_frame_begins == 1);
  CHECK(pipeline.render_frame_ends == 1);
}

TEST_CASE("frame_pipeline updates all stages before rendering any", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  std::vector<std::string> events;
  add_stage(pipeline, "game", true, tree->ensure("game"), {}, &events);
  add_stage(pipeline, "ui", false, tree->ensure("ui"), {}, &events);

  CHECK(pipeline.step_frame(1.0 / 60.0));

  CHECK(events == std::vector<std::string>{ "game:u", "ui:u", "game:r", "ui:r" });
}

TEST_CASE("frame_pipeline rebuilds order lists after erase and add", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  std::vector<std::string> events;

  frame_stage_options a_options;
  a_options.update_order = 2;
  a_options.render_order = 2;
  frame_stage_options b_options;
  b_options.update_order = 1;
  b_options.render_order = 1;
  frame_stage_options c_options;
  c_options.update_order = 3;
  c_options.render_order = 3;

  add_stage(pipeline, "a", true, tree->ensure("a"), a_options, &events);
  add_stage(pipeline, "b", true, tree->ensure("b"), b_options, &events);
  add_stage(pipeline, "c", true, tree->ensure("c"), c_options, &events);

  CHECK(pipeline.erase_stage("b").has_value());

  frame_stage_options d_options;
  d_options.update_order = 0;
  d_options.render_order = 0;
  add_stage(pipeline, "d", true, tree->ensure("d"), d_options, &events);

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(events == std::vector<std::string>{ "d:u", "a:u", "c:u", "d:r", "a:r", "c:r" });
}

TEST_CASE("frame_pipeline adds and removes stages", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);

  CHECK(pipeline.stage_count() == 0);
  CHECK_FALSE(pipeline.has_stage("game"));

  auto invalid_name = pipeline.add_stage("", std::make_unique<test_stage>(true), nullptr, {});
  REQUIRE_FALSE(invalid_name.has_value());
  CHECK(invalid_name.error() == std::make_error_code(std::errc::invalid_argument));

  auto null_stage = pipeline.add_stage("game", nullptr, nullptr, {});
  REQUIRE_FALSE(null_stage.has_value());
  CHECK(null_stage.error() == std::make_error_code(std::errc::invalid_argument));

  auto* game = add_stage(pipeline, "game", true);
  CHECK(pipeline.stage_count() == 1);
  CHECK(pipeline.has_stage("game"));

  auto duplicate = pipeline.add_stage("game", std::make_unique<test_stage>(true), nullptr, {});
  REQUIRE_FALSE(duplicate.has_value());
  CHECK(duplicate.error() == std::make_error_code(std::errc::file_exists));

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(game->last_dt == Approx(1.0 / 60.0));

  auto missing = pipeline.erase_stage("missing");
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error() == std::make_error_code(std::errc::no_such_file_or_directory));

  CHECK(pipeline.erase_stage("game").has_value());
  CHECK(pipeline.stage_count() == 0);

  add_stage(pipeline, "a", true);
  add_stage(pipeline, "b", true);
  pipeline.clear_stages();
  CHECK(pipeline.stage_count() == 0);

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(pipeline.presents == 2);
}

TEST_CASE("frame_pipeline skips the frame when polling requests quit", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));
  pipeline.quit_after_polls = 1;

  CHECK_FALSE(pipeline.step_frame(1.0 / 60.0));

  CHECK(pipeline.polls == 1);
  CHECK(pipeline.presents == 0);
  CHECK(pipeline.frame_begins == 0);
  CHECK(game->frame_begins == 0);
  CHECK(game->updates == 0);
  CHECK(game->renders == 0);
  CHECK(game->render_dt == Approx(-1.0));
  CHECK(pipeline.stats().total_frames() == 0);
  CHECK(pipeline.tick_count() == 0);
}

TEST_CASE("frame_pipeline skips rendering when an update requests quit", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));
  game->quit_after_updates = 2;

  CHECK_FALSE(pipeline.step_frame(0.05));

  CHECK(game->updates == 2);
  CHECK(game->renders == 0);
  CHECK(pipeline.presents == 0);
  CHECK(pipeline.render_frame_begins == 0);
  CHECK(pipeline.stats().total_frames() == 0);
  CHECK(pipeline.tick_count() == 2);
}

TEST_CASE("frame_pipeline presents when a render requests quit", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));
  auto* ui = add_stage(pipeline, "ui", false, tree->ensure("ui"));
  game->quit_after_renders = 1;

  CHECK_FALSE(pipeline.step_frame(1.0 / 60.0));

  CHECK(game->renders == 1);
  CHECK(ui->renders == 0);
  CHECK(game->render_frame_ends == 1);
  CHECK(ui->render_frame_ends == 1);
  CHECK(pipeline.render_frame_ends == 1);
  CHECK(pipeline.presents == 1);
  CHECK(pipeline.stats().total_frames() == 1);
}

TEST_CASE("frame_pipeline records statistics when present requests quit", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  add_stage(pipeline, "game", true, tree->ensure("game"));
  pipeline.quit_after_presents = 1;

  CHECK_FALSE(pipeline.step_frame(1.0 / 60.0));

  CHECK(pipeline.presents == 1);
  CHECK(pipeline.stats().total_frames() == 1);
}

TEST_CASE("frame_pipeline ignores frames after quit", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));
  pipeline.request_quit();

  CHECK_FALSE(pipeline.step_frame(1.0 / 60.0));
  CHECK(pipeline.polls == 0);
  CHECK(pipeline.presents == 0);
  CHECK(game->updates == 0);
}

TEST_CASE("frame_pipeline respects the external quit request", "[runtime][pipeline]") {
  schedule_config config;
  config.target_render_hz = 0.0;
  test_pipeline pipeline{ frame_schedule(config) };
  pipeline.external_quit = true;

  pipeline.run();

  CHECK(pipeline.should_quit());
  CHECK(pipeline.clock_calls == 0);
  CHECK(pipeline.polls == 0);
}

TEST_CASE("frame_pipeline run loop uses tick_clock and stops at quit", "[runtime][pipeline]") {
  schedule_config config;
  config.target_render_hz = 0.0;
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule(config), tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));
  pipeline.next_delta = 1.0 / 60.0;
  pipeline.quit_after_polls = 3;

  pipeline.run();

  CHECK(pipeline.should_quit());
  CHECK(pipeline.clock_calls == 3);
  CHECK(pipeline.polls == 3);
  CHECK(pipeline.presents == 2);
  CHECK(pipeline.stats().total_frames() == 2);
  CHECK(game->updates == 2);
}

TEST_CASE("frame_pipeline run loop throttles with tick_clock deltas", "[runtime][pipeline]") {
  auto tree = std::make_shared<clock_tree>();
  auto pacing = std::make_shared<manual_frame_clock>();
  test_pipeline pipeline(frame_schedule{}, tree, pacing);
  pipeline.manual_clock = pacing.get();
  add_stage(pipeline, "game", true, tree->ensure("game"));
  pipeline.next_delta = 0.001;
  pipeline.quit_after_polls = 2;

  pipeline.run();

  CHECK(pipeline.clock_calls == 2);
  REQUIRE(pipeline.waits == 2);
  CHECK(pipeline.wait_times[0] == Approx(1.0 / 60.0));
  CHECK(pipeline.wait_times[1] == Approx(1.0 / 60.0));
}

TEST_CASE("frame_pipeline updates the statistics budget with the target rate", "[runtime][pipeline]") {
  test_pipeline pipeline(frame_schedule{});
  CHECK(pipeline.stats().budget() == Approx(1.0 / 60.0));

  pipeline.schedule().target_render_hz(30.0);
  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(pipeline.stats().budget() == Approx(1.0 / 30.0));

  pipeline.schedule().target_render_hz(0.0);
  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(pipeline.stats().budget() == Approx(1.0 / 60.0));
}

TEST_CASE("frame_pipeline feeds statistics into adaptive pacing", "[runtime][pipeline]") {
  schedule_config config;
  config.auto_adjust_interval = 1;
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule(config), tree);
  add_stage(pipeline, "game", true, tree->ensure("game"));

  for (int i = 0; i < 4; ++i) {
    CHECK(pipeline.step_frame(0.05));
  }

  CHECK(pipeline.schedule().target_render_hz() < pipeline.schedule().desired_render_hz());
  CHECK(pipeline.schedule().target_render_hz() >= pipeline.schedule().config().min_render_hz);
}

TEST_CASE("frame_pipeline exposes per-stage statistics", "[runtime][pipeline][stats]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));
  auto* ui = add_stage(pipeline, "ui", false, tree->ensure("ui"));

  CHECK(pipeline.step_frame(0.05));

  const auto& stats = pipeline.stage_stats();
  REQUIRE(stats.size() == 2);
  CHECK(stats[0].name == "game");
  CHECK(stats[0].stage_id != frame_pipeline::invalid_stage_id);
  CHECK(stats[0].stage_id == pipeline.stage_id("game"));
  CHECK(stats[0].fixed_steps == 3);
  CHECK(stats[0].alpha == Approx(0.0).margin(1e-6));
  CHECK(stats[0].clock_delta == Approx(0.05));
  CHECK(stats[0].tick == 3);
  CHECK(stats[0].update_time >= 0.0);
  CHECK(stats[0].render_time >= 0.0);

  CHECK(stats[1].name == "ui");
  CHECK(stats[1].stage_id == pipeline.stage_id("ui"));
  CHECK(stats[1].fixed_steps == 0);
  CHECK(stats[1].alpha == Approx(1.0));
  CHECK(stats[1].clock_delta == Approx(0.05));
  CHECK(stats[1].tick == 1);

  CHECK(pipeline.stage_id("missing") == frame_pipeline::invalid_stage_id);
  CHECK(game->updates == 3);
  CHECK(ui->updates == 1);
}

TEST_CASE("frame_pipeline updates stage options and keeps stable ids", "[runtime][pipeline][stage]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  std::vector<std::string> events;

  auto* a = add_stage(pipeline, "a", true, tree->ensure("a"), {}, &events);
  auto* b = add_stage(pipeline, "b", true, tree->ensure("b"), {}, &events);

  const uint64_t a_id = pipeline.stage_id("a");
  REQUIRE(a_id != frame_pipeline::invalid_stage_id);

  auto options = pipeline.stage_options("a");
  REQUIRE(options.has_value());
  options->update_order = -1;
  options->render_order = 1;
  CHECK(pipeline.set_stage_options("a", *options).has_value());

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(events == std::vector<std::string>{ "a:u", "b:u", "b:r", "a:r" });
  CHECK(pipeline.stage_id("a") == a_id);

  events.clear();
  CHECK(pipeline.set_stage_enabled("a", false).has_value());
  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(events == std::vector<std::string>{ "b:u", "b:r" });
  CHECK(pipeline.stage_stats()[0].enabled == false);
  CHECK(pipeline.stage_stats()[0].fixed_steps == 0);
  CHECK(a->updates == 1);
  CHECK(b->updates == 2);

  CHECK(pipeline.set_stage_enabled("a", true).has_value());
  events.clear();
  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(events == std::vector<std::string>{ "a:u", "b:u", "b:r", "a:r" });

  CHECK(pipeline.erase_stage("a").has_value());
  add_stage(pipeline, "a", true, tree->ensure("a"));
  CHECK(pipeline.stage_id("a") != a_id);

  auto missing = pipeline.stage_options("missing");
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error() == std::make_error_code(std::errc::no_such_file_or_directory));
  CHECK_FALSE(pipeline.set_stage_options("missing", {}).has_value());
  CHECK_FALSE(pipeline.set_stage_enabled("missing", true).has_value());
}

TEST_CASE("frame_pipeline groups can be enabled and disabled together", "[runtime][pipeline][stage]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);

  frame_stage_options group_a;
  group_a.group = 7;
  frame_stage_options group_b;
  group_b.group = 7;
  frame_stage_options other;
  other.group = 9;

  add_stage(pipeline, "a", true, tree->ensure("a"), group_a);
  add_stage(pipeline, "b", true, tree->ensure("b"), group_b);
  add_stage(pipeline, "c", true, tree->ensure("c"), other);

  CHECK(pipeline.set_group_enabled(7, false) == 2);
  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(pipeline.stage_stats()[0].enabled == false);
  CHECK(pipeline.stage_stats()[1].enabled == false);
  CHECK(pipeline.stage_stats()[2].enabled == true);

  CHECK(pipeline.set_group_enabled(7, true) == 2);
  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(pipeline.stage_stats()[0].enabled == true);
  CHECK(pipeline.stage_stats()[1].enabled == true);
}

TEST_CASE("frame_pipeline rejects stage mutation during a frame", "[runtime][pipeline][stage]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));

  bool add_rejected = false;
  bool erase_rejected = false;
  bool options_rejected = false;
  bool group_rejected = false;

  game->update_hook = [&] {
    auto add = pipeline.add_stage("extra", std::make_unique<test_stage>(true), nullptr, {});
    add_rejected = !add.has_value() && add.error() == std::make_error_code(std::errc::operation_in_progress);

    auto erase = pipeline.erase_stage("game");
    erase_rejected = !erase.has_value() && erase.error() == std::make_error_code(std::errc::operation_in_progress);

    auto options = pipeline.stage_options("game");
    if (options.has_value()) {
      auto set = pipeline.set_stage_options("game", *options);
      options_rejected = !set.has_value() && set.error() == std::make_error_code(std::errc::operation_in_progress);
    }

    group_rejected = pipeline.set_group_enabled(0, false) == 0;
    pipeline.clear_stages();
  };

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(add_rejected);
  CHECK(erase_rejected);
  CHECK(options_rejected);
  CHECK(group_rejected);
  CHECK(pipeline.stage_count() == 1);
  CHECK(pipeline.has_stage("game"));
}

TEST_CASE("frame_pipeline supports per-stage fixed frequencies", "[runtime][pipeline][stage]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);

  frame_stage_options physics_options;
  physics_options.target_logic_hz = 120.0;
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));
  auto* physics = add_stage(pipeline, "physics", true, tree->ensure("physics"), physics_options);

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(game->updates == 1);
  CHECK(physics->updates == 2);
  CHECK(game->last_dt == Approx(1.0 / 60.0));
  CHECK(physics->last_dt == Approx(1.0 / 120.0));
  CHECK(pipeline.stage_stats()[0].fixed_steps == 1);
  CHECK(pipeline.stage_stats()[1].fixed_steps == 2);

  frame_stage_options limited_options;
  limited_options.max_logic_steps_per_frame = 1;
  limited_options.max_frame_time = 1.0;
  auto* limited = add_stage(pipeline, "limited", true, tree->ensure("limited"), limited_options);
  CHECK(pipeline.step_frame(0.05));
  CHECK(limited->updates == 1);
  const auto& stats = pipeline.stage_stats();
  CHECK(stats[2].fixed_steps == 1);
}

TEST_CASE("frame_pipeline stages inherit schedule logic changes live", "[runtime][pipeline][stage]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);

  frame_stage_options physics_options;
  physics_options.target_logic_hz = 30.0;
  auto* game = add_stage(pipeline, "game", true, tree->ensure("game"));
  auto* physics = add_stage(pipeline, "physics", true, tree->ensure("physics"), physics_options);

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(game->updates == 1);
  CHECK(physics->updates == 0);

  pipeline.schedule().target_logic_hz(120.0);

  CHECK(pipeline.step_frame(1.0 / 60.0));
  CHECK(game->updates == 3);
  CHECK(physics->updates == 1);
  CHECK(physics->last_dt == Approx(1.0 / 30.0));

  pipeline.schedule().max_logic_steps_per_frame(1);
  pipeline.schedule().max_frame_time(0.01);

  CHECK(pipeline.step_frame(1.0));
  CHECK(game->updates == 4);
  CHECK(game->last_dt == Approx(1.0 / 120.0));
  CHECK(physics->updates == 1);
}

TEST_CASE("frame_pipeline snapshots disabled and filtered stage stats", "[runtime][pipeline][stats]") {
  auto tree = std::make_shared<clock_tree>();
  test_pipeline pipeline(frame_schedule{}, tree);

  frame_stage_options disabled_options;
  disabled_options.enabled = false;
  frame_stage_options no_update;
  no_update.receive_update = false;
  frame_stage_options no_render;
  no_render.receive_render = false;

  auto* disabled = add_stage(pipeline, "disabled", true, tree->ensure("disabled"), disabled_options);
  auto* no_update_stage = add_stage(pipeline, "no_update", false, tree->ensure("no_update"), no_update);
  auto* no_render_stage = add_stage(pipeline, "no_render", true, tree->ensure("no_render"), no_render);

  CHECK(pipeline.step_frame(0.05));

  CHECK(disabled->updates == 0);
  CHECK(disabled->renders == 0);
  CHECK(disabled->frame_begins == 0);
  CHECK(disabled->render_frame_begins == 0);
  CHECK(disabled->render_frame_ends == 0);

  CHECK(no_update_stage->updates == 0);
  CHECK(no_update_stage->renders == 1);
  CHECK(no_update_stage->frame_begins == 1);

  CHECK(no_render_stage->updates == 3);
  CHECK(no_render_stage->renders == 0);
  CHECK(no_render_stage->render_frame_begins == 1);
  CHECK(no_render_stage->render_frame_ends == 1);

  const auto& stats = pipeline.stage_stats();
  REQUIRE(stats.size() == 3);
  CHECK(stats[0].enabled == false);
  CHECK(stats[0].update_time == Approx(0.0));
  CHECK(stats[0].render_time == Approx(0.0));
  CHECK(stats[1].receive_update == false);
  CHECK(stats[1].update_time == Approx(0.0));
  CHECK(stats[1].fixed_steps == 0);
  CHECK(stats[2].receive_render == false);
  CHECK(stats[2].render_time == Approx(0.0));
  CHECK(stats[2].fixed_steps == 3);
}

TEST_CASE("frame_pipeline run supports common synthetic rates", "[runtime][pipeline][pacing]") {
  for (const double hz : { 30.0, 60.0, 144.0, 0.0 }) {
    schedule_config config;
    config.target_render_hz = hz;
    test_pipeline pipeline{ frame_schedule(config) };
    pipeline.next_delta = 1.0 / 60.0;
    pipeline.quit_after_polls = 3;

    pipeline.run();

    CHECK(pipeline.clock_calls == 3);
    CHECK(pipeline.polls == 3);
    CHECK(pipeline.presents == 2);
    CHECK(pipeline.stats().total_frames() == 2);
  }
}

TEST_CASE("frame_pipeline compensates pacing oversleep with an injected clock", "[runtime][pipeline][pacing]") {
  auto pacing = std::make_shared<manual_frame_clock>();
  test_pipeline pipeline(frame_schedule{}, nullptr, pacing);
  pipeline.manual_clock = pacing.get();
  pipeline.oversleep = 0.005;
  pipeline.next_delta = 0.0;
  pipeline.quit_after_polls = 2;

  pipeline.run();

  REQUIRE(pipeline.waits == 2);
  CHECK(pipeline.wait_times[0] == Approx(1.0 / 60.0));
  CHECK(pipeline.wait_times[1] == Approx(1.0 / 60.0 - 0.005));
  CHECK(pacing->sleeps == 0);
}

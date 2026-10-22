#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cyberverse/runtime/clock_tree.hpp"
#include "cyberverse/runtime/frame_pipeline.hpp"
#include "cyberverse/runtime/frame_schedule.hpp"
#include "cyberverse/runtime/frame_stage.hpp"

using Catch::Approx;
using cyberverse::runtime::clock_node;
using cyberverse::runtime::clock_tree;
using cyberverse::runtime::frame_pipeline;
using cyberverse::runtime::frame_schedule;
using cyberverse::runtime::frame_stage;
using cyberverse::runtime::frame_stage_options;
using cyberverse::runtime::schedule_config;

namespace {

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

  void on_update(double dt, uint64_t tick) override {
    ++updates;
    last_dt = dt;
    last_tick = tick;
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
  }

  void on_render(double alpha, double dt) override {
    ++renders;
    last_alpha = alpha;
    render_dt = dt;
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
  uint64_t last_tick = 0;
  std::vector<uint64_t> ticks;
  int quit_after_updates = -1;
  int quit_after_renders = -1;
};

class test_pipeline final : public frame_pipeline {
public:
  explicit test_pipeline(frame_schedule schedule, std::shared_ptr<clock_tree> tree = nullptr)
      : frame_pipeline(std::move(schedule), std::move(tree)) {}

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
    last_wait = seconds;
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
  bool external_quit = false;
  int quit_after_polls = -1;
  int quit_after_presents = -1;
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
  test_pipeline pipeline(frame_schedule{}, tree);
  add_stage(pipeline, "game", true, tree->ensure("game"));
  pipeline.next_delta = 0.001;
  pipeline.quit_after_polls = 2;

  pipeline.run();

  CHECK(pipeline.clock_calls == 2);
  CHECK(pipeline.waits >= 1);
  CHECK(pipeline.last_wait > 0.0);
  CHECK(pipeline.last_wait < 1.0 / 60.0);
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

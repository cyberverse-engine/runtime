#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "cyberverse/runtime/frame_schedule.hpp"
#include "cyberverse/runtime/frame_stats.hpp"

using Catch::Approx;
using cyberverse::runtime::frame_schedule;
using cyberverse::runtime::frame_stats;
using cyberverse::runtime::schedule_config;
using cyberverse::runtime::step_result;

namespace {

constexpr double kLogicStep = 1.0 / 60.0;
constexpr double kTolerance = 1e-9;

void record_over_budget_frames(frame_stats& stats, int count) {
  for (int i = 0; i < count; ++i) {
    stats.record_frame({ 0.025, 0.002, 0.002, 1 });
  }
}

} // namespace

TEST_CASE("frame_schedule default configuration", "[runtime][schedule]") {
  frame_schedule schedule;

  CHECK(schedule.logic_step() == Approx(kLogicStep));
  CHECK(schedule.target_render_hz() == Approx(60.0));
  CHECK(schedule.desired_render_hz() == Approx(60.0));
  CHECK_FALSE(schedule.is_unlimited());
  CHECK(schedule.target_frame_time() == Approx(kLogicStep));

  CHECK(schedule.should_throttle(0.0));
  CHECK(schedule.should_throttle(0.01));
  CHECK_FALSE(schedule.should_throttle(kLogicStep));
  CHECK_FALSE(schedule.should_throttle(1.0));
  CHECK_FALSE(schedule.should_throttle(std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("frame_schedule normalizes invalid configuration", "[runtime][schedule]") {
  schedule_config config;
  config.target_logic_hz = 0.0;
  config.max_frame_time = -1.0;
  config.max_logic_steps_per_frame = 0;
  config.target_render_hz = std::numeric_limits<double>::quiet_NaN();
  config.min_render_hz = -5.0;
  config.auto_adjust_interval = 0;

  frame_schedule schedule(config);

  CHECK(schedule.logic_step() == Approx(kLogicStep));
  CHECK(schedule.config().max_frame_time == Approx(0.25));
  CHECK(schedule.config().max_logic_steps_per_frame == 5);
  CHECK(schedule.config().auto_adjust_interval == 30);
  CHECK(schedule.is_unlimited());
  CHECK(schedule.target_frame_time() == Approx(0.0));
  CHECK_FALSE(schedule.should_throttle(0.0));
}

TEST_CASE("fixed_step_scheduler accumulates fixed logic steps", "[runtime][schedule]") {
  frame_schedule schedule;
  auto scheduler = schedule.new_fixed_step_scheduler();

  CHECK(scheduler.logic_step() == Approx(kLogicStep));

  const step_result first = scheduler.step(kLogicStep);
  CHECK(first.logic_steps == 1);
  CHECK(first.alpha == Approx(0.0).margin(kTolerance));
  CHECK_FALSE(first.dropped_backlog);

  scheduler.reset();
  const step_result partial = scheduler.step(kLogicStep / 2.0);
  CHECK(partial.logic_steps == 0);
  CHECK(partial.alpha == Approx(0.5));

  const step_result completed = scheduler.step(kLogicStep / 2.0);
  CHECK(completed.logic_steps == 1);
  CHECK(completed.alpha == Approx(0.0).margin(kTolerance));
}

TEST_CASE("fixed_step_scheduler catches up with several steps", "[runtime][schedule]") {
  frame_schedule schedule;
  auto scheduler = schedule.new_fixed_step_scheduler();

  const step_result result = scheduler.step(0.05);
  CHECK(result.logic_steps == 3);
  CHECK(result.alpha == Approx(0.0).margin(1e-6));
  CHECK_FALSE(result.dropped_backlog);
  CHECK(scheduler.accumulator() == Approx(0.0).margin(1e-6));
}

TEST_CASE("fixed_step_scheduler clamps the frame delta", "[runtime][schedule]") {
  schedule_config config;
  config.max_frame_time = 0.05;
  config.max_logic_steps_per_frame = 10;
  frame_schedule schedule(config);
  auto scheduler = schedule.new_fixed_step_scheduler();

  const step_result result = scheduler.step(10.0);
  CHECK(result.logic_steps == 3);
  CHECK(result.dropped_backlog);
  CHECK(result.alpha == Approx(0.0).margin(1e-6));
}

TEST_CASE("fixed_step_scheduler drops excess logic steps", "[runtime][schedule]") {
  schedule_config config;
  config.max_frame_time = 1.0;
  config.max_logic_steps_per_frame = 2;
  frame_schedule schedule(config);
  auto scheduler = schedule.new_fixed_step_scheduler();

  const step_result result = scheduler.step(5.0 * kLogicStep);
  CHECK(result.logic_steps == 2);
  CHECK(result.dropped_backlog);
  CHECK(result.alpha == Approx(0.0).margin(1e-6));
  CHECK(scheduler.accumulator() == Approx(0.0).margin(1e-6));
}

TEST_CASE("fixed_step_scheduler ignores invalid deltas", "[runtime][schedule]") {
  frame_schedule schedule;
  auto scheduler = schedule.new_fixed_step_scheduler();

  CHECK(scheduler.step(-1.0).logic_steps == 0);
  CHECK(scheduler.step(std::numeric_limits<double>::quiet_NaN()).logic_steps == 0);
  CHECK(scheduler.step(std::numeric_limits<double>::infinity()).logic_steps == 0);
  CHECK(scheduler.accumulator() == Approx(0.0));
}

TEST_CASE("fixed_step_scheduler reset clears the accumulator", "[runtime][schedule]") {
  frame_schedule schedule;
  auto scheduler = schedule.new_fixed_step_scheduler();

  scheduler.step(kLogicStep / 2.0);
  CHECK(scheduler.accumulator() == Approx(kLogicStep / 2.0));

  scheduler.reset();
  CHECK(scheduler.accumulator() == Approx(0.0));
  CHECK(scheduler.step(kLogicStep / 2.0).logic_steps == 0);
}

TEST_CASE("stage schedulers keep independent accumulators", "[runtime][schedule]") {
  frame_schedule schedule;
  auto game = schedule.new_fixed_step_scheduler();
  auto ui = schedule.new_fixed_step_scheduler();

  game.step(kLogicStep / 2.0);
  game.step(kLogicStep / 2.0);
  CHECK(game.accumulator() == Approx(0.0).margin(kTolerance));

  CHECK(ui.accumulator() == Approx(0.0));
  CHECK(ui.step(kLogicStep / 2.0).logic_steps == 0);
  CHECK(ui.accumulator() == Approx(kLogicStep / 2.0));

  CHECK(game.step(kLogicStep / 2.0).logic_steps == 0);
  CHECK(ui.step(kLogicStep / 2.0).logic_steps == 1);
}

TEST_CASE("fixed_step_scheduler works without an owner", "[runtime][schedule]") {
  frame_schedule::fixed_step_scheduler scheduler(nullptr);

  CHECK(scheduler.logic_step() == Approx(kLogicStep));
  CHECK(scheduler.step(kLogicStep).logic_steps == 1);
}

TEST_CASE("frame_schedule supports limited and unlimited render pacing", "[runtime][schedule]") {
  frame_schedule schedule;

  schedule.target_render_hz(30.0);
  CHECK(schedule.target_render_hz() == Approx(30.0));
  CHECK(schedule.desired_render_hz() == Approx(30.0));
  CHECK(schedule.target_frame_time() == Approx(1.0 / 30.0));
  CHECK(schedule.should_throttle(0.02));
  CHECK_FALSE(schedule.should_throttle(0.04));

  schedule.target_render_hz(0.0);
  CHECK(schedule.is_unlimited());
  CHECK(schedule.target_frame_time() == Approx(0.0));
  CHECK_FALSE(schedule.should_throttle(0.0));

  schedule.target_render_hz(-5.0);
  CHECK(schedule.is_unlimited());

  schedule.target_render_hz(std::numeric_limits<double>::quiet_NaN());
  CHECK(schedule.is_unlimited());
}

TEST_CASE("frame_schedule lowers the render rate when frames are over budget", "[runtime][schedule]") {
  schedule_config config;
  config.auto_adjust_interval = 1;
  frame_schedule schedule(config);

  frame_stats stats(0.5, kLogicStep);
  record_over_budget_frames(stats, 4);
  REQUIRE(stats.is_over_budget());

  schedule.observe(stats);

  CHECK(schedule.target_render_hz() < schedule.desired_render_hz());
  CHECK(schedule.target_render_hz() >= schedule.config().min_render_hz);
}

TEST_CASE("frame_schedule never drops below the minimum render rate", "[runtime][schedule]") {
  schedule_config config;
  config.auto_adjust_interval = 1;
  config.min_render_hz = 30.0;
  frame_schedule schedule(config);

  frame_stats stats(0.5, kLogicStep);
  record_over_budget_frames(stats, 4);

  for (int i = 0; i < 100; ++i) {
    schedule.observe(stats);
  }

  CHECK(schedule.target_render_hz() == Approx(config.min_render_hz));
}

TEST_CASE("frame_schedule recovers the render rate when there is headroom", "[runtime][schedule]") {
  schedule_config config;
  config.auto_adjust_interval = 1;
  frame_schedule schedule(config);

  frame_stats bad_stats(0.5, kLogicStep);
  record_over_budget_frames(bad_stats, 4);
  schedule.observe(bad_stats);
  const double lowered = schedule.target_render_hz();
  REQUIRE(lowered < schedule.desired_render_hz());

  frame_stats good_stats(0.5, kLogicStep);
  good_stats.record_frame({ kLogicStep, 0.001, 0.001, 1 });
  schedule.observe(good_stats);

  CHECK(schedule.target_render_hz() > lowered);
  CHECK(schedule.target_render_hz() <= schedule.desired_render_hz());
}

TEST_CASE("frame_schedule adaptive rate can be disabled", "[runtime][schedule]") {
  schedule_config config;
  config.auto_render_hz = false;
  config.auto_adjust_interval = 1;
  frame_schedule schedule(config);

  frame_stats stats(0.5, kLogicStep);
  record_over_budget_frames(stats, 4);
  for (int i = 0; i < 10; ++i) {
    schedule.observe(stats);
  }

  CHECK(schedule.target_render_hz() == Approx(60.0));
}

TEST_CASE("frame_schedule ignores empty statistics", "[runtime][schedule]") {
  schedule_config config;
  config.auto_adjust_interval = 1;
  frame_schedule schedule(config);
  frame_stats stats;

  schedule.observe(stats);

  CHECK(schedule.target_render_hz() == Approx(60.0));
}

TEST_CASE("frame_schedule unlimited mode never adapts", "[runtime][schedule]") {
  schedule_config config;
  config.target_render_hz = 0.0;
  config.auto_adjust_interval = 1;
  frame_schedule schedule(config);

  frame_stats stats(0.5, kLogicStep);
  record_over_budget_frames(stats, 4);
  schedule.observe(stats);

  CHECK(schedule.is_unlimited());
}

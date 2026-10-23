#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "cyberverse/runtime/frame_stats.hpp"

using Catch::Approx;
using cyberverse::runtime::frame_sample;
using cyberverse::runtime::frame_stats;

TEST_CASE("frame_stats starts empty", "[runtime][stats]") {
  frame_stats stats;

  CHECK(stats.smoothed_frame_time() == Approx(0.0));
  CHECK(stats.smoothed_logic_time() == Approx(0.0));
  CHECK(stats.smoothed_render_time() == Approx(0.0));
  CHECK(stats.average_fps() == Approx(0.0));
  CHECK(stats.logic_utilization() == Approx(0.0));
  CHECK(stats.render_utilization() == Approx(0.0));
  CHECK(stats.total_frames() == 0);
  CHECK_FALSE(stats.is_jittering());
  CHECK_FALSE(stats.is_over_budget());
  CHECK(stats.budget() == Approx(1.0 / 60.0));
}

TEST_CASE("frame_stats initializes the EMA from the first sample", "[runtime][stats]") {
  frame_stats stats(0.5, 1.0);
  stats.record_frame({ 0.020, 0.004, 0.006, 2 });

  CHECK(stats.smoothed_frame_time() == Approx(0.020));
  CHECK(stats.smoothed_logic_time() == Approx(0.004));
  CHECK(stats.smoothed_render_time() == Approx(0.006));
  CHECK(stats.average_fps() == Approx(50.0));
  CHECK(stats.total_frames() == 1);
  CHECK_FALSE(stats.is_jittering());
  CHECK_FALSE(stats.is_over_budget());
}

TEST_CASE("frame_stats smooths subsequent samples", "[runtime][stats]") {
  frame_stats stats(0.5, 1.0);
  stats.record_frame({ 0.010, 0.002, 0.003, 1 });
  stats.record_frame({ 0.020, 0.004, 0.006, 2 });

  CHECK(stats.smoothed_frame_time() == Approx(0.015));
  CHECK(stats.smoothed_logic_time() == Approx(0.003));
  CHECK(stats.smoothed_render_time() == Approx(0.0045));
  CHECK(stats.average_fps() == Approx(1.0 / 0.015));
  CHECK(stats.total_frames() == 2);
}

TEST_CASE("frame_stats normalizes the EMA alpha", "[runtime][stats]") {
  const double invalid_alphas[] = { 0.0, -0.5, 2.0, std::numeric_limits<double>::quiet_NaN() };

  for (const double alpha : invalid_alphas) {
    frame_stats stats(alpha, 1.0);
    stats.record_frame({ 0.010, 0.0, 0.0, 1 });
    stats.record_frame({ 0.020, 0.0, 0.0, 1 });
    CHECK(stats.smoothed_frame_time() == Approx(0.011));
  }
}

TEST_CASE("frame_stats reports utilization against the budget", "[runtime][stats]") {
  frame_stats stats(0.5, 0.020);
  stats.record_frame({ 0.020, 0.005, 0.010, 1 });

  CHECK(stats.budget() == Approx(0.020));
  CHECK(stats.logic_utilization() == Approx(0.25));
  CHECK(stats.render_utilization() == Approx(0.5));

  stats.set_budget(0.010);
  CHECK(stats.budget() == Approx(0.010));
  CHECK(stats.logic_utilization() == Approx(0.5));
  CHECK(stats.render_utilization() == Approx(1.0));

  stats.set_budget(-1.0);
  CHECK(stats.budget() == Approx(0.010));
  stats.set_budget(std::numeric_limits<double>::quiet_NaN());
  CHECK(stats.budget() == Approx(0.010));
}

TEST_CASE("frame_stats detects consecutive jitter", "[runtime][stats]") {
  frame_stats stats(0.5, 1.0);
  stats.record_frame({ 0.010, 0.0, 0.0, 1 });

  stats.record_frame({ 0.030, 0.0, 0.0, 1 });
  stats.record_frame({ 0.010, 0.0, 0.0, 1 });
  stats.record_frame({ 0.030, 0.0, 0.0, 1 });
  CHECK_FALSE(stats.is_jittering());

  stats.record_frame({ 0.010, 0.0, 0.0, 1 });
  CHECK(stats.is_jittering());

  for (int i = 0; i < 10; ++i) {
    stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  }
  CHECK_FALSE(stats.is_jittering());
}

TEST_CASE("frame_stats detects consecutive over-budget frames", "[runtime][stats]") {
  frame_stats stats(0.5, 0.010);
  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  CHECK_FALSE(stats.is_over_budget());

  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  CHECK_FALSE(stats.is_over_budget());

  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  CHECK(stats.is_over_budget());

  stats.record_frame({ 0.005, 0.0, 0.0, 1 });
  CHECK_FALSE(stats.is_over_budget());
}

TEST_CASE("frame_stats changing the budget clears the over-budget streak", "[runtime][stats]") {
  frame_stats stats(0.5, 0.010);
  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  REQUIRE(stats.is_over_budget());

  stats.set_budget(0.100);
  CHECK_FALSE(stats.is_over_budget());
  CHECK(stats.smoothed_frame_time() == Approx(0.020));
}

TEST_CASE("frame_stats sanitizes invalid samples", "[runtime][stats]") {
  frame_stats stats(0.5, 1.0);
  stats.record_frame({ -1.0, std::numeric_limits<double>::quiet_NaN(), -0.5, -3 });

  CHECK(stats.smoothed_frame_time() == Approx(0.0));
  CHECK(stats.smoothed_logic_time() == Approx(0.0));
  CHECK(stats.smoothed_render_time() == Approx(0.0));
  CHECK(stats.average_fps() == Approx(0.0));
  CHECK(stats.total_frames() == 1);
}

TEST_CASE("frame_stats reset keeps configuration", "[runtime][stats]") {
  frame_stats stats(0.5, 0.020);
  stats.record_frame({ 0.020, 0.005, 0.005, 1 });
  stats.record_frame({ 0.040, 0.005, 0.005, 1 });
  REQUIRE(stats.total_frames() == 2);

  stats.reset();

  CHECK(stats.smoothed_frame_time() == Approx(0.0));
  CHECK(stats.smoothed_logic_time() == Approx(0.0));
  CHECK(stats.smoothed_render_time() == Approx(0.0));
  CHECK(stats.average_fps() == Approx(0.0));
  CHECK(stats.total_frames() == 0);
  CHECK_FALSE(stats.is_jittering());
  CHECK_FALSE(stats.is_over_budget());
  CHECK(stats.budget() == Approx(0.020));
}

TEST_CASE("frame_stats keeps a fixed rolling window", "[runtime][stats][window]") {
  frame_stats stats(0.5, 0.01, 4);

  stats.record_frame({ 0.010, 0.0, 0.0, 1 });
  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  stats.record_frame({ 0.030, 0.0, 0.0, 1 });
  stats.record_frame({ 0.040, 0.0, 0.0, 1 });

  CHECK(stats.window_size() == 4);
  CHECK(stats.window_sample_count() == 4);
  CHECK(stats.min_frame_time() == Approx(0.010));
  CHECK(stats.max_frame_time() == Approx(0.040));
  CHECK(stats.percentile(0.0) == Approx(0.010));
  CHECK(stats.percentile(0.5) == Approx(0.025));
  CHECK(stats.percentile(1.0) == Approx(0.040));

  stats.record_frame({ 0.050, 0.0, 0.0, 1 });
  CHECK(stats.window_sample_count() == 4);
  CHECK(stats.min_frame_time() == Approx(0.020));
  CHECK(stats.max_frame_time() == Approx(0.050));
  CHECK(stats.percentile(0.5) == Approx(0.035));

  stats.record_frame({ 0.060, 0.0, 0.0, 1 });
  CHECK(stats.min_frame_time() == Approx(0.030));
  CHECK(stats.max_frame_time() == Approx(0.060));
}

TEST_CASE("frame_stats window capacity and reset", "[runtime][stats][window]") {
  frame_stats tiny(0.5, 0.01, 0);
  CHECK(tiny.window_size() == 1);
  tiny.record_frame({ 0.010, 0.0, 0.0, 1 });
  tiny.record_frame({ 0.020, 0.0, 0.0, 1 });
  CHECK(tiny.window_sample_count() == 1);
  CHECK(tiny.min_frame_time() == Approx(0.020));

  frame_stats huge(0.5, 0.01, 1000);
  CHECK(huge.window_size() == frame_stats::max_window_size);

  frame_stats stats(0.5, 0.01, 4);
  stats.record_frame({ 0.010, 0.0, 0.0, 1 });
  stats.record_frame({ 0.020, 0.0, 0.0, 1 });
  CHECK(stats.window_sample_count() == 2);

  stats.reset();
  CHECK(stats.window_sample_count() == 0);
  CHECK(stats.min_frame_time() == Approx(0.0));
  CHECK(stats.max_frame_time() == Approx(0.0));
  CHECK(stats.percentile(0.5) == Approx(0.0));
  CHECK(stats.window_size() == 4);
}

TEST_CASE("frame_stats reports budget margin and clamped percentiles", "[runtime][stats][window]") {
  frame_stats stats(0.5, 0.020, 4);
  stats.record_frame({ 0.010, 0.0, 0.0, 1 });

  CHECK(stats.budget_margin() == Approx(0.010));
  CHECK(stats.percentile(-1.0) == Approx(0.010));
  CHECK(stats.percentile(2.0) == Approx(0.010));
  CHECK(stats.percentile(std::numeric_limits<double>::quiet_NaN()) == Approx(0.010));
}

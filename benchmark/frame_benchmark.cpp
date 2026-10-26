#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "cyberverse/runtime.hpp"

using cyberverse::runtime::clock_tree;
using cyberverse::runtime::frame_budget;
using cyberverse::runtime::frame_pipeline;
using cyberverse::runtime::frame_sample;
using cyberverse::runtime::frame_schedule;
using cyberverse::runtime::frame_stage;
using cyberverse::runtime::frame_stage_options;
using cyberverse::runtime::frame_stats;
using cyberverse::runtime::schedule_config;

namespace {

class null_stage final : public frame_stage {
public:
  explicit null_stage(bool use_fixed_step) : frame_stage(use_fixed_step) {}

  void on_update(double, uint64_t, const frame_budget&) override {}
  void on_render(double, double, const frame_budget&) override {}
};

class null_pipeline final : public frame_pipeline {
public:
  using frame_pipeline::frame_pipeline;

  double tick_clock() noexcept override { return 1.0 / 60.0; }
  void on_poll() noexcept override {}
  void on_present() noexcept override {}
  bool should_quit_requested() noexcept override { return false; }
  void on_throttle_wait(double) override {}
};

void add_null_stage(frame_pipeline& pipeline,
                    std::string name,
                    bool use_fixed_step,
                    clock_tree& tree,
                    frame_stage_options options = {}) {
  auto stage = std::make_unique<null_stage>(use_fixed_step);
  auto result = pipeline.add_stage(name, std::move(stage), tree.ensure(name), options);
  if (!result.has_value()) {
    benchmark::DoNotOptimize(result);
  }
}

void fill_over_budget_stats(frame_stats& stats) {
  for (int i = 0; i < 8; ++i) {
    stats.record_frame({ 0.030, 0.012, 0.012, 1 });
  }
}

} // namespace

static void BM_SchedulerStep60(benchmark::State& state) {
  frame_schedule schedule;
  auto scheduler = schedule.new_fixed_step_scheduler();
  for (auto _ : state) {
    auto result = scheduler.step(1.0 / 60.0);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_SchedulerStep60);

static void BM_SchedulerStepCatchUp(benchmark::State& state) {
  frame_schedule schedule;
  auto scheduler = schedule.new_fixed_step_scheduler();
  for (auto _ : state) {
    auto result = scheduler.step(0.050);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_SchedulerStepCatchUp);

static void BM_SchedulerStepClamped(benchmark::State& state) {
  frame_schedule schedule;
  auto scheduler = schedule.new_fixed_step_scheduler();
  for (auto _ : state) {
    auto result = scheduler.step(1.0);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_SchedulerStepClamped);

static void BM_ClockTreeUpdate(benchmark::State& state) {
  clock_tree tree;
  for (int i = 0; i < 16; ++i) {
    tree.ensure("stage" + std::to_string(i));
  }

  for (auto _ : state) {
    tree.update(1.0 / 60.0);
    double root_delta = tree.root()->delta();
    benchmark::DoNotOptimize(root_delta);
  }
}
BENCHMARK(BM_ClockTreeUpdate);

static void BM_ClockQueries(benchmark::State& state) {
  clock_tree tree;
  auto clock = tree.ensure("game/ui");
  clock->set_time_scale(0.5);
  tree.update(1.0 / 60.0);

  for (auto _ : state) {
    double delta = clock->delta();
    double scale = clock->effective_time_scale();
    bool paused = clock->is_paused();
    benchmark::DoNotOptimize(delta);
    benchmark::DoNotOptimize(scale);
    benchmark::DoNotOptimize(paused);
  }
}
BENCHMARK(BM_ClockQueries);

static void BM_ScheduleThrottleDecision(benchmark::State& state) {
  frame_schedule schedule;
  for (auto _ : state) {
    bool throttle = schedule.should_throttle(0.010);
    benchmark::DoNotOptimize(throttle);
  }
}
BENCHMARK(BM_ScheduleThrottleDecision);

static void BM_ScheduleObserve(benchmark::State& state) {
  schedule_config config;
  config.auto_adjust_interval = 1;
  frame_schedule schedule(config);
  frame_stats stats(0.1, 1.0 / 60.0);
  fill_over_budget_stats(stats);

  for (auto _ : state) {
    schedule.observe(stats);
    double target_hz = schedule.target_render_hz();
    benchmark::DoNotOptimize(target_hz);
  }
}
BENCHMARK(BM_ScheduleObserve);

static void BM_StatsRecord(benchmark::State& state) {
  frame_stats stats;
  const frame_sample sample{ 1.0 / 60.0, 0.002, 0.004, 1 };
  for (auto _ : state) {
    stats.record_frame(sample);
    double smoothed = stats.smoothed_frame_time();
    benchmark::DoNotOptimize(smoothed);
  }
}
BENCHMARK(BM_StatsRecord);

static void BM_StatsQueries(benchmark::State& state) {
  frame_stats stats;
  for (int i = 0; i < 8; ++i) {
    stats.record_frame({ 1.0 / 60.0, 0.002, 0.004, 1 });
  }

  for (auto _ : state) {
    bool unhealthy = stats.is_jittering() || stats.is_over_budget();
    double fps = stats.average_fps();
    benchmark::DoNotOptimize(unhealthy);
    benchmark::DoNotOptimize(fps);
  }
}
BENCHMARK(BM_StatsQueries);

static void BM_StatsPercentile(benchmark::State& state) {
  frame_stats stats(0.1, 1.0 / 60.0, 120);
  for (int i = 0; i < 120; ++i) {
    stats.record_frame({ 0.010 + 0.0001 * static_cast<double>(i), 0.002, 0.004, 1 });
  }

  for (auto _ : state) {
    double p95 = stats.percentile(0.95);
    double low = stats.min_frame_time();
    double high = stats.max_frame_time();
    benchmark::DoNotOptimize(p95);
    benchmark::DoNotOptimize(low);
    benchmark::DoNotOptimize(high);
  }
}
BENCHMARK(BM_StatsPercentile);

static void BM_PipelineStepFixed(benchmark::State& state) {
  auto tree = std::make_shared<clock_tree>();
  null_pipeline pipeline(frame_schedule{}, tree);
  add_null_stage(pipeline, "game", true, *tree);

  for (auto _ : state) {
    bool running = pipeline.step_frame(1.0 / 60.0);
    benchmark::DoNotOptimize(running);
  }
}
BENCHMARK(BM_PipelineStepFixed);

static void BM_PipelineStepMixed(benchmark::State& state) {
  auto tree = std::make_shared<clock_tree>();
  null_pipeline pipeline(frame_schedule{}, tree);
  add_null_stage(pipeline, "game", true, *tree);
  add_null_stage(pipeline, "ui", false, *tree);

  for (auto _ : state) {
    bool running = pipeline.step_frame(1.0 / 60.0);
    benchmark::DoNotOptimize(running);
  }
}
BENCHMARK(BM_PipelineStepMixed);

static void BM_PipelineStepCatchUp(benchmark::State& state) {
  auto tree = std::make_shared<clock_tree>();
  null_pipeline pipeline(frame_schedule{}, tree);
  add_null_stage(pipeline, "game", true, *tree);

  for (auto _ : state) {
    bool running = pipeline.step_frame(0.050);
    benchmark::DoNotOptimize(running);
  }
}
BENCHMARK(BM_PipelineStepCatchUp);

static void BM_PipelineStepManyStages(benchmark::State& state) {
  auto tree = std::make_shared<clock_tree>();
  null_pipeline pipeline(frame_schedule{}, tree);
  const int stage_count = static_cast<int>(state.range(0));
  for (int i = 0; i < stage_count; ++i) {
    frame_stage_options options;
    options.update_order = i % 3;
    options.render_order = (stage_count - i) % 3;
    add_null_stage(pipeline, "stage" + std::to_string(i), i % 2 == 0, *tree, options);
  }

  for (auto _ : state) {
    bool running = pipeline.step_frame(1.0 / 60.0);
    benchmark::DoNotOptimize(running);
  }

  state.counters["stages"] = stage_count;
}
BENCHMARK(BM_PipelineStepManyStages)->Arg(1)->Arg(4)->Arg(16);

static void BM_PipelineStepFiltered(benchmark::State& state) {
  auto tree = std::make_shared<clock_tree>();
  null_pipeline pipeline(frame_schedule{}, tree);
  for (int i = 0; i < 8; ++i) {
    frame_stage_options options;
    options.update_order = i;
    options.render_order = 7 - i;
    options.receive_update = i % 2 == 0;
    options.receive_render = i % 3 != 0;
    add_null_stage(pipeline, "stage" + std::to_string(i), i % 2 == 0, *tree, options);
  }

  for (auto _ : state) {
    bool running = pipeline.step_frame(1.0 / 60.0);
    benchmark::DoNotOptimize(running);
  }
}
BENCHMARK(BM_PipelineStepFiltered);

static void BM_PipelineStepPerStageFrequency(benchmark::State& state) {
  auto tree = std::make_shared<clock_tree>();
  null_pipeline pipeline(frame_schedule{}, tree);

  frame_stage_options physics_options;
  physics_options.target_logic_hz = 120.0;
  add_null_stage(pipeline, "game", true, *tree);
  add_null_stage(pipeline, "physics", true, *tree, physics_options);

  for (auto _ : state) {
    bool running = pipeline.step_frame(1.0 / 60.0);
    benchmark::DoNotOptimize(running);
  }
}
BENCHMARK(BM_PipelineStepPerStageFrequency);

static void BM_PipelineStageStats(benchmark::State& state) {
  auto tree = std::make_shared<clock_tree>();
  null_pipeline pipeline(frame_schedule{}, tree);
  for (int i = 0; i < 8; ++i) {
    add_null_stage(pipeline, "stage" + std::to_string(i), i % 2 == 0, *tree);
  }
  bool running = pipeline.step_frame(1.0 / 60.0);
  benchmark::DoNotOptimize(running);

  for (auto _ : state) {
    const auto& stats = pipeline.stage_stats();
    std::size_t count = stats.size();
    uint64_t id = stats.front().stage_id;
    benchmark::DoNotOptimize(count);
    benchmark::DoNotOptimize(id);
  }
}
BENCHMARK(BM_PipelineStageStats);

BENCHMARK_MAIN();

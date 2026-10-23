# runtime

Cyberverse Engine frame runtime: hierarchical clocks, fixed-step and variable-step frame stages,
render pacing, automatic frame-rate stabilization, frame statistics, and a reusable frame loop.

`runtime` is a C++23 static library (`cyberverse::runtime::static_lib`). It owns no window,
renderer, scene, or platform state: callers provide the real frame delta and implement the
application hooks. The only external dependency is the C++ standard library. The umbrella header is
`cyberverse/runtime.hpp`.

## Features

- **Clock tree** — `clock_tree` / `clock_node` provide slash-separated, hierarchical clocks. A node
  can be paused or time-scaled locally; the effective pause and scale include every ancestor. Each
  node tracks its effective frame delta and accumulated total time. The tree supports validation,
  global pause, reparent, deep clone, snapshots, and text/JSON debugging exports.
- **Frame stages** — `frame_stage` moves update and render work out of the pipeline. A stage chooses
  fixed-step or variable-step updates and runs on its own clock node. A game stage can freeze while a
  UI stage keeps running.
- **Independent stage priorities** — `frame_stage_options` gives each stage separate update and
  render priorities. Lower values run first; ties keep registration order. `receive_update` and
  `receive_render` can gate either action without removing the stage; `enabled`, `group`, and
  per-stage fixed-step overrides are also available.
- **Per-stage statistics** — the pipeline exposes a zero-allocation `stage_stats()` view with each
  stage's id, group, update/render time, fixed steps, alpha, clock delta, and tick.
- **Frame lifecycle hooks** — the pipeline and each stage expose frame-begin, render-frame-begin and
  render-frame-end hooks around the update/render phases.
- **Per-stage fixed-step schedulers** — `frame_schedule::fixed_step_scheduler` owns an independent
  accumulator and inherits the schedule's logic configuration live. Every fixed-step stage gets its
  own scheduler, so different clock speeds or logic frequencies do not share catch-up state.
  `frame_schedule::fixed_step_overrides` pins only selected fields; everything else follows later
  `frame_schedule` changes.
- **Deterministic pacing** — a `frame_clock` drives wait deadlines. `steady_frame_clock` is the
  default; tests can inject a manual clock. `run()` keeps an absolute deadline, uses sleep-until, and
  compensates small oversleeps by shortening the next wait.
- **Render pacing** — a separate `target_render_hz` controls when the pipeline should wait before
  presenting. `0` means unlimited. Logic and render rates are independent, so a 60 Hz simulation can
  render at 30, 60, or 144 Hz.
- **Automatic frame-rate stabilization** — when enabled, `frame_schedule` lowers the active render
  rate after consistently missed frames and recovers towards the configured rate when there is CPU
  headroom. The target never drops below `min_render_hz` or rises above the configured rate.
- **Frame statistics** — `frame_stats` maintains EMAs for frame, logic, and render time, derives FPS
  and budget utilization, exposes jitter and over-budget flags, and keeps a fixed-size rolling
  window with min/max/percentile and budget-margin queries.
- **Frame loop** — `frame_pipeline` owns the clock tree and stages and implements
  tick/poll → frame lifecycle → prioritized stage updates → prioritized renders → present →
  statistics. `step_frame(dt)` executes a frame deterministically; `run()` asks `tick_clock()` for
  the real delta and performs pacing.

The parallel/multithreaded task framework is planned but intentionally not implemented yet. See the
repository [roadmap](docs/roadmap.md) and the workspace [roadmap](../docs/roadmap.md).

## Layout

```text
src/cyberverse/runtime.hpp                  Umbrella header
src/cyberverse/runtime/clock_node.hpp       Hierarchical clock node
src/cyberverse/runtime/clock_node.cpp       Pause/scale inheritance, delta, total time
src/cyberverse/runtime/clock_tree.hpp       Path-based clock tree, snapshots, debug export
src/cyberverse/runtime/clock_tree.cpp       Lookup/ensure/reparent/clone/update traversal
src/cyberverse/runtime/frame_clock.hpp      Pacing clock interface and steady-clock adapter
src/cyberverse/runtime/frame_clock.cpp      Steady-clock implementation
src/cyberverse/runtime/frame_schedule.hpp   Pacing policy and per-stage scheduler
src/cyberverse/runtime/frame_schedule.cpp   Pacing, adaptive rate, fixed-step accumulator
src/cyberverse/runtime/frame_stage.hpp      Fixed-step or variable-step stage interface
src/cyberverse/runtime/frame_stage.cpp      Stage base implementation
src/cyberverse/runtime/frame_pipeline.hpp   Stage-driven frame loop, ids, stats, pacing
src/cyberverse/runtime/frame_pipeline.cpp   Clock update, stage dispatch, quit, statistics
src/cyberverse/runtime/frame_stats.hpp      Frame/stage stats, rolling window, health flags
src/cyberverse/runtime/frame_stats.cpp      Statistics implementation
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

Tests and benchmarks are enabled by default and fetch Catch2 v3.5.2 and Google Benchmark v1.9.5 at
configure time. Disable them with `-DCYBERVERSE_RUNTIME_BUILD_TESTS=OFF` and
`-DCYBERVERSE_RUNTIME_BUILD_BENCHMARKS=OFF`.

### Test

Tests must be built and run outside the repository:

```bash
cmake -S . -B /tmp/cyberverse-runtime-test
cmake --build /tmp/cyberverse-runtime-test
ctest --test-dir /tmp/cyberverse-runtime-test --output-on-failure
```

`test/` builds `cyberverse_runtime_tests` (Catch2, discovered by CTest). It covers clock inheritance,
lifetime, path validation, global pause, reparent/clone, snapshots/export, per-stage accumulator
independence, fixed/variable stage dispatch, priority ordering, receive flags, stage ids/options/
groups/reentrancy, per-stage fixed frequencies, per-stage statistics, lifecycle hooks, quit paths,
pacing deadlines and oversleep compensation, adaptive rate changes, EMA smoothing, rolling windows,
min/max/percentiles, and jitter/over-budget detection. Tests are deterministic: no sleeps and no
wall-clock assertions.

### Benchmark

```bash
/tmp/cyberverse-runtime-test/benchmark/cyberverse_runtime_benchmarks
```

`benchmark/` builds `cyberverse_runtime_benchmarks` (Google Benchmark). It measures per-stage
scheduler steps, clock tree updates/queries, throttle decisions, adaptive observations, statistics
recording/queries/percentiles, stage-stat queries, full pipeline frames with one or many prioritized
stages, per-stage fixed frequencies, and stages filtered by `receive_update` / `receive_render`.

### Presets

`cmake --preset debug` and `cmake --preset release` write to `build/` and `build-release/`. Tests
should still be configured under `/tmp`.

## Example

```cpp
#include <chrono>
#include <memory>
#include <utility>

#include "cyberverse/runtime.hpp"

using namespace cyberverse::runtime;

class game_stage final : public frame_stage {
public:
  game_stage() : frame_stage(true) {} // fixed-step updates

  void on_update(double dt, uint64_t tick) override {
    (void)dt;   // Always the fixed logic step.
    (void)tick; // Stage-local tick, starting at 0.
  }

  void on_render(double alpha, double dt) override {
    (void)alpha; // Interpolate between the previous and next logic state.
    (void)dt;    // Effective game-clock delta for this frame.
  }
};

class ui_stage final : public frame_stage {
public:
  ui_stage() : frame_stage(false) {} // variable-step updates

  void on_update(double dt, uint64_t tick) override {
    (void)dt;   // Effective UI-clock delta; can keep running while the game is paused.
    (void)tick; // Stage-local update counter.
  }

  void on_render(double alpha, double dt) override {
    (void)alpha; // Always 1.0 for variable-step stages.
    (void)dt;
  }
};

class game_loop final : public frame_pipeline {
public:
  explicit game_loop(std::shared_ptr<clock_tree> clocks)
      : frame_pipeline(frame_schedule{}, std::move(clocks)) {}

  double tick_clock() noexcept override {
    const auto now = std::chrono::steady_clock::now();
    const double delta = std::chrono::duration<double>(now - last_).count();
    last_ = now;
    return delta;
  }

  void on_poll() noexcept override {
    // Poll the platform window/input for this frame.
  }

  void on_present() noexcept override {
    // Present the rendered frame.
  }

  bool should_quit_requested() noexcept override {
    return false; // Forward the window close state here.
  }

private:
  std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};

int main() {
  auto clocks = std::make_shared<clock_tree>();
  game_loop loop(clocks);

  frame_stage_options game_options;
  game_options.update_order = 0;
  game_options.render_order = 10;

  frame_stage_options ui_options;
  ui_options.update_order = 10;
  ui_options.render_order = 0;
  ui_options.receive_update = true;
  ui_options.receive_render = true;

  loop.add_stage("game",
                 std::make_unique<game_stage>(),
                 clocks->ensure("game"),
                 game_options);
  loop.add_stage("ui",
                 std::make_unique<ui_stage>(),
                 clocks->ensure("ui"),
                 ui_options);

  // The game clock can be paused independently of the UI clock.
  clocks->ensure("game")->set_paused(true);

  loop.run();
}
```

For a custom loop, create one scheduler per fixed-step stage:

```cpp
frame_schedule schedule;
auto scheduler = schedule.new_fixed_step_scheduler();

// Optional: pin only the logic frequency; the other limits keep inheriting.
frame_schedule::fixed_step_overrides fast;
fast.target_logic_hz = 120.0;
auto fast_scheduler = schedule.new_fixed_step_scheduler(fast);

schedule.max_frame_time(0.1); // both schedulers inherit the new clamp
schedule.target_logic_hz(30.0); // scheduler follows; fast_scheduler stays at 120 Hz

const step_result step = scheduler.step(stage_clock_delta);
for (int i = 0; i < step.logic_steps; ++i) {
  update(scheduler.logic_step());
}
render(step.alpha);
```

## Semantics

- Time is in seconds. Fixed logic steps are always `1 / target_logic_hz`; invalid schedule
  configuration values are normalized to the documented defaults.
- `clock_node::update(real_delta)` treats negative and non-finite deltas as zero. The effective delta
  is zero when the node or an ancestor is paused, otherwise `real_delta * effective_time_scale()`.
  Local time scales are clamped to `[0, 100]`; NaN falls back to 1. The effective scale is the
  product of the ancestor chain. `total_time()` accumulates effective deltas.
- `clock_tree` paths are slash-separated (`"game"`, `"game/ui"`, `"/game/ui"`). Empty segments are
  ignored; `""` and `"/"` refer to the root. `.` and `..` segments are rejected. The root is named
  `"root"` and cannot be erased. Erasing or clearing a node detaches it, so an external shared
  pointer remains a valid parentless clock. `reparent`/`clone` reject cycles, `set_paused` pauses
  the whole tree, and `snapshot`/`to_text`/`to_json` provide read-only debug output.
- Time-scale curves and per-node pause/scale callbacks are intentionally deferred. Callers can
  animate `set_time_scale` each frame; `total_time()` remains continuous because it integrates the
  effective delta and a scale change never resets it. Paused stages still render with a frozen/zero
  clock delta unless the stage is disabled or `receive_render` is false.
- `frame_schedule` owns render pacing and is a factory for `fixed_step_scheduler`. A scheduler
  inherits the normalized logic configuration live and owns its own accumulator; schedulers never
  share catch-up state. `target_logic_hz`, `max_frame_time`, `max_logic_steps_per_frame`, and
  `set_config` change what inheriting schedulers use on their next step without resetting their
  accumulators. `fixed_step_overrides` pins only engaged fields, so a scheduler with a 120 Hz
  override still inherits later `max_frame_time` or catch-up-limit changes. The schedule must outlive
  schedulers created from it. `step()` treats negative/non-finite deltas as zero, clamps to
  `max_frame_time`, and drops excess whole steps when more than `max_logic_steps_per_frame` are
  pending.
- Fixed-step stages receive zero or more `on_update(logic_step, stage_tick)` calls and exactly one
  `on_render(alpha, clock_delta)` call per frame. `alpha` is the scheduler's interpolation fraction in
  `[0, 1)`.
- Variable-step stages receive exactly one `on_update(clock_delta, stage_tick)` and one
  `on_render(1.0, clock_delta)` call per frame. They do not allocate or consume a fixed-step
  accumulator.
- `frame_stage_options` controls dispatch. Update phases run in ascending `update_order`; render
  phases run in ascending `render_order`. Equal priorities keep registration order. `receive_update`
  skips the stage's `on_update` and its fixed-step accumulator; `receive_render` skips only
  `on_render`. `enabled == false` skips every callback. `group` supports bulk enable/disable.
  `target_logic_hz`, `max_frame_time`, and `max_logic_steps_per_frame` provide per-stage fixed-step
  overrides; non-positive values inherit the pipeline schedule live. A null clock uses the pipeline
  root clock.
- The pipeline assigns each stage a stable id (never reused) and exposes `set_stage_options`,
  `set_stage_enabled`, `set_group_enabled`, and `stage_options`. Stage mutations are rejected with
  `std::errc::operation_in_progress` from inside a frame.
- Frame order is: poll → clock-tree update → pipeline `on_frame_begin` → stage `on_frame_begin`
  (update order) → stage updates (update order) → pipeline `on_render_frame_begin` → stage
  `on_render_frame_begin` (render order) → stage renders (render order) → stage
  `on_render_frame_end` (render order) → pipeline `on_render_frame_end` → present → statistics.
  Lifecycle hooks run for every enabled stage, including stages whose receive flag is false.
- Stage tick counters are stage-local; `frame_pipeline::tick_count()` is the total number of stage
  update calls that actually executed. `stage_stats()` is a zero-allocation, registration-ordered
  view of each stage's id, group, update/render time, fixed steps, alpha, clock delta, and tick.
- `target_render_hz == 0` (or a negative/NaN value assigned through the setter) means unlimited:
  `target_frame_time()` is zero and `should_throttle()` always returns false.
- Automatic stabilization is evaluated every `auto_adjust_interval` frames through
  `frame_schedule::observe`. It lowers the active rate by 10% when `frame_stats::is_over_budget()` is
  true, and raises it by 10% when the frame is not jittering and logic/render utilization is below
  70%. The active rate is bounded by `[min_render_hz, target_render_hz]`.
- `frame_stats` initializes its EMAs from the first sample and does not raise jitter or over-budget
  flags for that first frame. It keeps a fixed rolling window with `min_frame_time`,
  `max_frame_time`, `percentile`, and `budget_margin`; queries use stack storage and do no heap
  allocation. The pipeline records aggregate logic/render time and total fixed steps.
- `frame_pipeline::run()` calls `tick_clock()` once per iteration for the frame delta and uses the
  injected `frame_clock` only for wait deadlines. `steady_frame_clock` is the default; `run()` keeps
  an absolute deadline, calls `on_throttle_wait`, and lets small oversleeps shorten the next wait.
  A stall longer than one frame resynchronizes the deadline. `step_frame(dt)` never uses the pacing
  clock, never sleeps, and never throttles.
- A quit requested during `on_poll` skips the clock update, lifecycle hooks, stages, and present. A
  quit during a frame-begin hook or stage update skips the remaining updates, all rendering, and
  present for that frame. A quit during the render phase stops the remaining renders, still runs the
  render-frame-end hooks, and still presents and records statistics. A quit during `on_present` is
  observed after presentation.
- `on_throttle_wait(seconds)` defaults to waiting on the pacing clock with a sleep-until deadline.
  Override it to inject a synthetic wait; tests typically advance a manual `frame_clock` instead of
  sleeping.

## Thread safety

`frame_pipeline::request_quit()` and `should_quit()` are safe to call from any thread; the quit flag
is atomic. Every other operation — including the application hooks, clock tree, pacing clock, stages,
`step_frame`, `run`, the schedule, and the statistics collector — must run on the loop thread. The
runtime does not start a render thread or a worker pool.

See [AGENTS.md](AGENTS.md) for architecture and contribution rules. The detailed roadmap is in
[docs/roadmap.md](docs/roadmap.md); the workspace roadmap is in [../docs/roadmap.md](../docs/roadmap.md).

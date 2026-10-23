# CLAUDE.md

This file provides guidance to coding agents working in this repository.

## Repository

`runtime` is the frame runtime of Cyberverse Engine: C++23, standard library only, built as the
static library `cyberverse::runtime::static_lib`. It owns hierarchical clocks, frame stages,
fixed-step advancing, render pacing, automatic frame-rate stabilization, frame statistics, and the
reusable frame loop. It does **not** own windows, input, rendering, scenes, or assets. The real frame
delta is supplied by the application through `frame_pipeline::tick_clock()`.

Public API headers live under `src/cyberverse/runtime/`; `src/cyberverse/runtime.hpp` is the umbrella
header. `src/` is the include root, so consumers include `"cyberverse/runtime/frame_pipeline.hpp"`.

The library currently provides:

- `clock_node.hpp` / `.cpp` — one node with local pause and time scale, inherited effective state,
  effective delta, and accumulated total time.
- `clock_tree.hpp` / `.cpp` — root-owned, slash-separated clock hierarchy with lookup, ensure, erase,
  clear, reparent, clone, snapshots, text/JSON export, and one recursive per-frame update.
- `frame_clock.hpp` / `.cpp` — monotonic pacing-clock interface and `steady_frame_clock`.
- `frame_schedule.hpp` / `.cpp` — render pacing policy, adaptive rate, live logic configuration, and
  the per-stage `fixed_step_scheduler` factory with optional pinned overrides.
- `frame_stage.hpp` / `.cpp` — fixed-step or variable-step update/render stage interface and frame
  lifecycle hooks.
- `frame_pipeline.hpp` / `.cpp` — clock-tree update, stage ids/options/groups, dispatch, per-stage
  snapshots, poll/present, quit handling, statistics, and pacing.
- `frame_stats.hpp` / `.cpp` — aggregate frame and per-stage statistics, rolling window,
  min/max/percentiles, EMA, FPS/utilization, jitter and over-budget flags.

The parallel/multithreaded task framework is future work. Do not add worker pools, render threads, or
job dependencies until the roadmap milestone is explicitly scheduled.

## Build and test

Run commands from this directory. Tests must be built and run under `/tmp`, never inside the repo.

```bash
cmake -S . -B /tmp/cyberverse-runtime-test
cmake --build /tmp/cyberverse-runtime-test
ctest --test-dir /tmp/cyberverse-runtime-test --output-on-failure
```

- The configure step fetches Catch2 v3.5.2 and Google Benchmark v1.9.5 by default. Skip them with
  `-DCYBERVERSE_RUNTIME_BUILD_TESTS=OFF` and `-DCYBERVERSE_RUNTIME_BUILD_BENCHMARKS=OFF`.
- Presets: `cmake --preset debug` / `cmake --preset release`, then `cmake --build --preset debug` or
  `release`; outputs go to `build/` and `build-release/`. Tests still belong under `/tmp`.
- `test/` builds `cyberverse_runtime_tests` (Catch2, discovered by CTest). Tests are deterministic:
  no sleeps, no wall-clock assertions, and no writes outside `/tmp`.
- `benchmark/` builds `cyberverse_runtime_benchmarks` (Google Benchmark). It is not part of CTest.
- Format with the repository `.clang-format` (LLVM base, 2-space indent, 120 columns, attached
  braces, left-aligned pointers). Use `xcrun clang-format` on macOS when `clang-format` is not on
  `PATH`.

## Architecture

The runtime is layered so each piece can be tested without a window or renderer:

1. `clock_node` / `clock_tree` own hierarchical game time. They have no schedule, stage, or pipeline
   dependency. `clock_tree::update(delta)` recursively refreshes every node once per frame.
2. `frame_schedule` is a pure value object for render pacing and the logic-step configuration. It has
   no clock, accumulator, or threading. `fixed_step_scheduler` is the per-stage accumulator; it
   resolves inherited logic configuration from its owning schedule on every step, so later schedule
   changes apply without rebuilding the scheduler.
3. `frame_stats` consumes aggregate samples and owns a fixed rolling window; it has no clock or
   stage dependency.
4. `frame_stage` owns one update/render model and one clock. It knows nothing about the pipeline,
   other stages, or the clock tree traversal.
5. `frame_pipeline` is the only component that calls `tick_clock()` and owns the clock tree, pacing
   clock, schedule, stats collector, stage list/options/ids, and per-stage snapshots.

Keep the dependency direction one-way: `frame_pipeline` → `frame_schedule` / `clock_tree` /
`frame_clock` / `frame_stage` / `frame_stats`. Do not make the lower layers depend on the pipeline, the platform
layer, GLM, the renderer, or any engine scene type.

`step_frame(dt)` is the deterministic frame primitive. It executes exactly one frame and never
sleeps. `run()` is the convenience loop that calls `tick_clock()`, advances an absolute deadline on
the injected `frame_clock`, calls `on_throttle_wait`, and delegates to `step_frame`. Do not move
pacing or sleeping into `step_frame`.

Each stage must own its scheduler. A `fixed_step_scheduler` stores an accumulator and either a live
schedule reference or a standalone configuration; sharing one scheduler between stages would mix
their clock deltas and break pause/time-scale isolation. The owning schedule must outlive inherited
schedulers. `fixed_step_overrides` pins only engaged fields, so stage overrides stay fixed while the
other limits keep following the schedule. Variable-step stages do not use a scheduler.

`frame_pipeline` owns two order-index lists (`update_order_`, `render_order_`) rebuilt whenever a
stage is added or erased. `add_stage` requires a `frame_stage_options` value; the pipeline never
mutates the options after insertion. Keep the separate update/render ordering and the per-stage
`receive_update`/`receive_render` gates intact when changing dispatch. Stage ids are monotonically
assigned and never reused. Stage stats are stored in registration order and updated in place; their
name views are invalidated by add/erase/clear. Mutating stage state from a callback is rejected with
`operation_in_progress` via the `stepping_` guard.

## API semantics

These rules are part of the public contract; keep tests, README, and headers in sync when changing
them.

- Time is in seconds. `target_logic_hz` invalid values fall back to 60 Hz. `max_frame_time`
  invalid values fall back to 0.25 s. `max_logic_steps_per_frame` values below 1 fall back to 5.
- `clock_node::update(real_delta)` treats negative/non-finite deltas as zero. Its effective delta is
  zero when the node or an ancestor is paused; otherwise it is `real_delta * effective_time_scale()`.
  Local time scales are clamped to `[0, 100]`; NaN falls back to 1. Effective scale is the product of
  the ancestor chain. `total_time()` accumulates effective deltas.
- `clock_tree` paths are slash-separated. Empty segments are ignored; `""` and `"/"` refer to the
  root. `.`/`..` and embedded NULs are invalid. The root is named `"root"` and cannot be erased.
  Erasing/clearing detaches a node. `reparent` rejects cycles and name conflicts; `clone` deep-copies
  local pause/scale and rejects cycles/target conflicts; `snapshot`/`to_text`/`to_json` are read-only
  debug views. `set_paused` pauses the whole tree.
- `frame_schedule` owns render pacing. `target_render_hz == 0` means unlimited.
  `should_throttle(elapsed)` is false when unlimited or `elapsed` is non-finite; otherwise it is true
  while `elapsed < target_frame_time()`.
- Automatic stabilization is controlled by `schedule_config::auto_render_hz` (on by default) and
  runs through `observe(stats)` every `auto_adjust_interval` frames. It lowers the active rate by 10%
  when `is_over_budget()` is true, and raises it by 10% when the frame is not jittering and
  `max(logic_utilization, render_utilization) < 0.70`. The active rate is clamped to
  `[min_render_hz, target_render_hz]`. It is a no-op for unlimited schedules, disabled schedules,
  empty statistics, and configurations where the minimum equals the desired rate.
- `fixed_step_scheduler::step(delta)` treats negative/non-finite deltas as zero, clamps deltas above
  `max_frame_time`, and returns `logic_steps`, `alpha`, and `dropped_backlog`. If more whole steps are
  pending than `max_logic_steps_per_frame`, only that many are returned, the remaining whole steps are
  discarded, and `dropped_backlog` is set. The fractional remainder is kept so `alpha` stays in
  `[0, 1)`. Schedulers are independent.
- A scheduler created by `new_fixed_step_scheduler()` inherits every logic field live. Its
  `target_logic_hz`, `max_frame_time`, and `max_logic_steps_per_frame` re-resolve on each
  `step`/`logic_step` call, so schedule setters apply on the next step without clearing the
  accumulator. `fixed_step_overrides` engages only the fields that are set; engaged fields stay
  pinned while the rest keep following the schedule. A scheduler constructed from a `schedule_config`
  is standalone and never follows a schedule. The owning schedule must outlive an inherited
  scheduler; a null owner uses the default configuration.
- Fixed-step stages receive zero or more `on_update(logic_step, stage_tick)` calls and exactly one
  `on_render(alpha, clock_delta)` call per frame. Variable-step stages receive exactly one
  `on_update(clock_delta, stage_tick)` and one `on_render(1.0, clock_delta)` call per frame.
- `frame_stage_options` is stored per stage. Update order uses ascending `update_order`; render order
  uses ascending `render_order`; equal values keep registration order. `enabled == false` skips all
  callbacks; `group` supports bulk enable/disable; `target_logic_hz`, `max_frame_time`, and
  `max_logic_steps_per_frame` override fixed-step behavior per stage (non-positive inherits the
  schedule live).
  `set_stage_options`/`set_stage_enabled`/`set_group_enabled` mutate between frames only.
- `receive_update == false` skips the stage's `on_update` and its scheduler step; `receive_render ==
  false` skips only `on_render`. Lifecycle hooks still run for every enabled stage. Disabled stages
  run no callbacks, consume no ticks, and record zero update/render time.
- Stage ids are monotonically assigned and never reused. `stage_stats()` is a zero-allocation view in
  registration order; name views are invalidated by add/erase/clear. Stage mutations during a frame
  return `std::errc::operation_in_progress`; `clear_stages` is a no-op during a frame.
- Frame order is poll → clock-tree update → pipeline `on_frame_begin` → stage `on_frame_begin`
  (update order) → stage updates (update order) → pipeline `on_render_frame_begin` → stage
  `on_render_frame_begin` (render order) → stage renders (render order) → stage
  `on_render_frame_end` (render order) → pipeline `on_render_frame_end` → present → statistics.
  Stage ticks are stage-local; `frame_pipeline::tick_count()` counts update calls that actually ran.
- `frame_stats` initializes all EMAs from the first sample and skips jitter/over-budget evaluation
  for that sample. Later samples use `alpha * sample + (1 - alpha) * previous`. `alpha` must be in
  `(0, 1]`; invalid values fall back to 0.1. Budget invalid values fall back to 1/60 s. The rolling
  window is fixed at construction (clamped to `[1, max_window_size]`); `min_frame_time`,
  `max_frame_time`, `percentile`, and `budget_margin` query it without heap allocation.
- A frame is over budget when `frame_time > budget * 1.1` for 3 consecutive frames. A frame is jitter
  when `abs(frame_time - previous_smoothed_frame_time) > 0.2 * previous_smoothed_frame_time` for 4
  consecutive frames. `set_budget` resets the over-budget streak but keeps the EMAs.
- `frame_pipeline::step_frame` polls, updates the clock tree, runs frame/render lifecycle hooks,
  updates and renders stages in their order lists, presents, records aggregate statistics, and
  refreshes the statistics budget. A quit from `on_poll` skips the clock update, lifecycle hooks,
  stages, and present. A quit from a frame-begin hook or stage update skips remaining updates, all
  renders, and present. A quit from the render phase stops remaining renders, still runs
  render-frame-end hooks, and still presents and records statistics. A quit from `on_present` is
  observed after presentation.
- `tick_clock()` returns real seconds since its previous call; `run()` calls it once per iteration
  and uses the injected `frame_clock` only for deadlines. The first call may return zero.
  `step_frame(dt)` never calls it.
- `on_throttle_wait(seconds)` defaults to a sleep-until wait on `pacing_clock()`. Override it for
  synthetic waits; tests should advance a manual `frame_clock` instead of sleeping. `run()` keeps an
  absolute deadline, lets small oversleeps shorten the next wait, and resynchronizes after a stall
  longer than one frame. It checks `should_quit_requested()` once per frame before doing work.
- `request_quit()` and `should_quit()` use an atomic flag and are safe from any thread. All other
  runtime state is single-threaded.

## Threading

The current runtime is single-threaded. `frame_pipeline` must be driven from one thread; application
hooks and stage callbacks run on that thread. `request_quit()` is the only cross-thread entry point.
Do not add hidden background threads, global thread pools, or blocking synchronization to the frame
path. The future job system must be introduced behind an explicit API and a roadmap milestone, with
deterministic ordering tests.

## Style

- Public types and functions are `lower_snake_case` (`frame_schedule`, `should_throttle`,
  `fixed_step_scheduler`); private members end with `_` (`accumulator_`, `tick_`). Write the return
  type before the function name rather than using a trailing return type.
- New public functions use `lower_snake_case`; there is no legacy capitalized `Step` API anymore.
- Headers use `#pragma once` and keep the Doxygen `@file` block. API documentation and comments are
  English; the workspace roadmap is Chinese.
- Add new public headers to `HEADER_FILES` and the umbrella `src/cyberverse/runtime.hpp`; add new
  sources to `SOURCE_FILES` in `src/CMakeLists.txt`.
- Do not introduce a platform, GLM, renderer, or engine-scene dependency. Pass caller-provided
  deltas, callbacks, and value types instead.
- Prefer deterministic tests. Use `step_frame` with explicit deltas and a fake `tick_clock` for frame
  protocol and `run()` tests; do not test with sleeps or timing thresholds.

## Git

- The existing `origin` remote uses the local SSH alias `github-simon2077s.com`; do not rewrite it.
  This alias is local-only and is not valid in build-time fetch URLs.
- Build-time fetches must use the real public host, never the alias.
- Commit directly on `main` using Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`).

The planned direction is tracked in [docs/roadmap.md](docs/roadmap.md) and the workspace
[roadmap](../docs/roadmap.md).

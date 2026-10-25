#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>
#include <utility>

#include "cyberverse/runtime/frame_pipeline.hpp"
#include "cyberverse/runtime/frame_schedule.hpp"
#include "cyberverse/runtime/frame_stage.hpp"
#include "cyberverse/runtime/this_thread.hpp"

using cyberverse::runtime::frame_pipeline;
using cyberverse::runtime::frame_schedule;
using cyberverse::runtime::frame_stage;
using cyberverse::runtime::frame_stage_options;
using cyberverse::runtime::is_this_render_thread;
using cyberverse::runtime::set_this_as_render_thread;

namespace {

class minimal_pipeline final : public frame_pipeline {
public:
  using frame_pipeline::frame_pipeline;

  double tick_clock() noexcept override { return 1.0 / 60.0; }
  void on_poll() noexcept override {}
  void on_present() noexcept override {}
  bool should_quit_requested() noexcept override { return false; }
};

class probe_stage final : public frame_stage {
public:
  probe_stage() : frame_stage(true) {}

  void on_update(double, uint64_t) override { update_saw_render_thread = is_this_render_thread(); }

  void on_render(double, double) override { render_saw_render_thread = is_this_render_thread(); }

  bool update_saw_render_thread = false;
  bool render_saw_render_thread = false;
};

} // namespace

TEST_CASE("this_thread tracks the render role per thread", "[runtime][thread]") {
  bool initial = true;
  bool after_set = false;

  std::thread worker([&] {
    initial = is_this_render_thread();
    set_this_as_render_thread();
    after_set = is_this_render_thread();
  });
  worker.join();

  CHECK_FALSE(initial);
  CHECK(after_set);
}

TEST_CASE("frame_pipeline marks the stepping thread as a render thread", "[runtime][pipeline][thread]") {
  bool before_step = true;
  bool after_step = false;
  bool update_saw_render_thread = false;
  bool render_saw_render_thread = false;

  std::thread worker([&] {
    minimal_pipeline pipeline{ frame_schedule{} };
    auto stage = std::make_unique<probe_stage>();
    probe_stage* probe = stage.get();
    (void)pipeline.add_stage("probe", std::move(stage), nullptr, frame_stage_options{});

    before_step = is_this_render_thread();
    (void)pipeline.step_frame(1.0 / 60.0);
    after_step = is_this_render_thread();
    update_saw_render_thread = probe->update_saw_render_thread;
    render_saw_render_thread = probe->render_saw_render_thread;
  });
  worker.join();

  CHECK_FALSE(before_step);
  CHECK(after_step);
  CHECK(update_saw_render_thread);
  CHECK(render_saw_render_thread);
}

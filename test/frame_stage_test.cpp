#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cyberverse/runtime/frame_budget.hpp"
#include "cyberverse/runtime/frame_stage.hpp"

using cyberverse::runtime::frame_budget;
using cyberverse::runtime::frame_stage;

namespace {

class recording_stage final : public frame_stage {
public:
  explicit recording_stage(bool use_fixed_step) : frame_stage(use_fixed_step) {}

  void on_update(double dt, uint64_t tick, const frame_budget& budget) override {
    last_dt = dt;
    last_tick = tick;
    last_budget_unlimited = budget.is_unlimited();
    ++updates;
  }

  void on_render(double dt, double alpha, const frame_budget& budget) override {
    last_alpha = alpha;
    render_dt = dt;
    last_budget_unlimited = budget.is_unlimited();
    ++renders;
  }

  int updates = 0;
  int renders = 0;
  double last_dt = -1.0;
  double last_alpha = -1.0;
  double render_dt = -1.0;
  bool last_budget_unlimited = false;
  uint64_t last_tick = 0;
};

} // namespace

TEST_CASE("frame_stage exposes its update model", "[runtime][stage]") {
  recording_stage fixed(true);
  recording_stage variable(false);

  CHECK(fixed.use_fixed_step());
  CHECK_FALSE(variable.use_fixed_step());
}

TEST_CASE("frame_stage records update and render arguments", "[runtime][stage]") {
  recording_stage stage(true);

  stage.on_update(0.25, 7, frame_budget::unlimited());
  stage.on_render(0.125, 0.5, frame_budget::unlimited());

  CHECK(stage.updates == 1);
  CHECK(stage.renders == 1);
  CHECK(stage.last_dt == Catch::Approx(0.25));
  CHECK(stage.last_tick == 7);
  CHECK(stage.last_alpha == Catch::Approx(0.5));
  CHECK(stage.render_dt == Catch::Approx(0.125));
  CHECK(stage.last_budget_unlimited);
}

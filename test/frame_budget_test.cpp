#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "cyberverse/runtime/frame_budget.hpp"
#include "cyberverse/runtime/frame_clock.hpp"

using Catch::Approx;
using cyberverse::runtime::frame_budget;
using cyberverse::runtime::frame_clock;

namespace {

class manual_clock final : public frame_clock {
public:
  double now() noexcept override { return now_; }

  void sleep_until(double deadline) override {
    if (deadline > now_) {
      now_ = deadline;
    }
  }

  void advance(double seconds) { now_ += seconds; }

  double now_ = 0.0;
};

} // namespace

TEST_CASE("frame_budget counts down against a clock", "[runtime][budget]") {
  manual_clock clock;
  frame_budget budget(1.0, clock);

  CHECK_FALSE(budget.is_unlimited());
  CHECK(budget.has_remaining());
  CHECK(budget.remaining_seconds() == Approx(1.0));
  CHECK(budget.used_seconds() == Approx(0.0));
  CHECK(budget.deadline_seconds() == Approx(1.0));

  clock.advance(0.25);
  CHECK(budget.has_remaining());
  CHECK(budget.remaining_seconds() == Approx(0.75));
  CHECK(budget.used_seconds() == Approx(0.25));
  CHECK(budget.deadline_seconds() == Approx(1.0));

  clock.advance(0.80);
  CHECK_FALSE(budget.has_remaining());
  CHECK(budget.remaining_seconds() == Approx(0.0));
  CHECK(budget.used_seconds() == Approx(1.05));
}

TEST_CASE("frame_budget supports unlimited budgets", "[runtime][budget]") {
  manual_clock clock;
  frame_budget budget(-1.0, clock);

  CHECK(budget.is_unlimited());
  clock.advance(10.0);
  CHECK(budget.has_remaining());
  CHECK(budget.remaining_seconds() == std::numeric_limits<double>::max());
  CHECK(budget.used_seconds() == Approx(10.0));
  CHECK(budget.deadline_seconds() == std::numeric_limits<double>::infinity());

  const frame_budget child = budget.sub(0.25);
  CHECK(child.is_unlimited());
  CHECK(child.has_remaining());
  CHECK(child.remaining_seconds() == std::numeric_limits<double>::max());
  CHECK(child.used_seconds() == Approx(0.0));
  CHECK(child.deadline_seconds() == std::numeric_limits<double>::infinity());

  const frame_budget shared = budget.share(0.25);
  CHECK(shared.is_unlimited());
  CHECK(shared.used_seconds() == Approx(10.0));
  CHECK(shared.deadline_seconds() == std::numeric_limits<double>::infinity());

  frame_budget non_finite(std::numeric_limits<double>::quiet_NaN(), clock);
  CHECK(non_finite.is_unlimited());
  CHECK(non_finite.has_remaining());
  CHECK(non_finite.remaining_seconds() == std::numeric_limits<double>::max());
}

TEST_CASE("frame_budget supports unlimited budgets without a clock", "[runtime][budget]") {
  const frame_budget budget = frame_budget::unlimited();

  CHECK(budget.is_unlimited());
  CHECK(budget.has_remaining());
  CHECK(budget.remaining_seconds() == std::numeric_limits<double>::max());
  CHECK(budget.used_seconds() == Approx(0.0));
  CHECK(budget.deadline_seconds() == std::numeric_limits<double>::infinity());

  const frame_budget child = budget.sub(0.25);
  CHECK(child.is_unlimited());
  CHECK(child.has_remaining());
  CHECK(child.used_seconds() == Approx(0.0));

  const frame_budget shared = budget.share(0.25);
  CHECK(shared.is_unlimited());
  CHECK(shared.has_remaining());
  CHECK(shared.used_seconds() == Approx(0.0));
}

TEST_CASE("frame_budget sub-budgets cover part of the remainder", "[runtime][budget]") {
  manual_clock clock;
  frame_budget budget(1.0, clock);

  clock.advance(0.4);
  frame_budget half = budget.sub(0.5);

  CHECK(half.remaining_seconds() == Approx(0.3));
  CHECK(half.used_seconds() == Approx(0.0));
  CHECK(half.deadline_seconds() == Approx(0.7));
  CHECK(budget.remaining_seconds() == Approx(0.6));

  clock.advance(0.1);
  CHECK(half.remaining_seconds() == Approx(0.2));
  CHECK(half.used_seconds() == Approx(0.1));
  CHECK(half.deadline_seconds() == Approx(0.7));
  CHECK(budget.remaining_seconds() == Approx(0.5));
  CHECK(budget.used_seconds() == Approx(0.5));

  CHECK(budget.sub(2.0).remaining_seconds() == Approx(0.5));
  CHECK(budget.sub(0.0).remaining_seconds() == Approx(0.0));
  CHECK_FALSE(budget.sub(0.0).has_remaining());
  CHECK(budget.sub(std::numeric_limits<double>::quiet_NaN()).remaining_seconds() == Approx(0.0));
  CHECK_FALSE(budget.sub(-1.0).has_remaining());
}

TEST_CASE("frame_budget share keeps the parent start", "[runtime][budget]") {
  manual_clock clock;
  frame_budget budget(1.0, clock);

  clock.advance(0.4);
  frame_budget half = budget.share(0.5);

  CHECK(half.remaining_seconds() == Approx(0.1));
  CHECK(half.used_seconds() == Approx(0.4));
  CHECK(half.deadline_seconds() == Approx(0.5));
  CHECK(budget.remaining_seconds() == Approx(0.6));

  clock.advance(0.1);
  CHECK(half.remaining_seconds() == Approx(0.0));
  CHECK(half.used_seconds() == Approx(0.5));
  CHECK_FALSE(half.has_remaining());
  CHECK(budget.remaining_seconds() == Approx(0.5));

  CHECK(budget.share(2.0).remaining_seconds() == Approx(0.5));
  CHECK(budget.share(0.0).remaining_seconds() == Approx(0.0));
  CHECK_FALSE(budget.share(0.0).has_remaining());
  CHECK(budget.share(std::numeric_limits<double>::quiet_NaN()).remaining_seconds() == Approx(0.0));
  CHECK_FALSE(budget.share(-1.0).has_remaining());
}

TEST_CASE("frame_budget copies and sub-budgets share the clock", "[runtime][budget]") {
  manual_clock clock;
  frame_budget budget(1.0, clock);
  frame_budget copy = budget;

  clock.advance(0.2);
  CHECK(copy.used_seconds() == Approx(0.2));
  CHECK(copy.remaining_seconds() == Approx(0.8));
  CHECK(budget.used_seconds() == Approx(0.2));

  frame_budget shared = copy.share(0.5);
  clock.advance(0.1);
  CHECK(shared.used_seconds() == Approx(0.3));
  CHECK(shared.remaining_seconds() == Approx(0.2));
  CHECK(shared.deadline_seconds() == Approx(0.5));
}

TEST_CASE("frame_budget handles zero budgets", "[runtime][budget]") {
  manual_clock clock;
  frame_budget exhausted(0.0, clock);

  CHECK_FALSE(exhausted.is_unlimited());
  CHECK_FALSE(exhausted.has_remaining());
  CHECK(exhausted.remaining_seconds() == Approx(0.0));
  CHECK(exhausted.deadline_seconds() == Approx(0.0));
}

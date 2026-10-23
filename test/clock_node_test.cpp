#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

#include "cyberverse/runtime/clock_node.hpp"

using Catch::Approx;
using cyberverse::runtime::clock_node;

TEST_CASE("clock_node starts at full speed", "[runtime][clock]") {
  auto node = std::make_shared<clock_node>("game");

  CHECK(node->name() == "game");
  CHECK_FALSE(node->is_paused_locally());
  CHECK_FALSE(node->is_paused());
  CHECK(node->local_time_scale() == Approx(1.0));
  CHECK(node->effective_time_scale() == Approx(1.0));
  CHECK(node->delta() == Approx(0.0));
  CHECK(node->total_time() == Approx(0.0));
}

TEST_CASE("clock_node advances delta and total time", "[runtime][clock]") {
  auto node = std::make_shared<clock_node>("game");

  node->update(0.25);
  CHECK(node->delta() == Approx(0.25));
  CHECK(node->total_time() == Approx(0.25));

  node->update(0.50);
  CHECK(node->delta() == Approx(0.50));
  CHECK(node->total_time() == Approx(0.75));
}

TEST_CASE("clock_node pause freezes delta and total time", "[runtime][clock]") {
  auto node = std::make_shared<clock_node>("game");

  node->update(0.25);
  node->set_paused(true);
  REQUIRE(node->is_paused_locally());
  REQUIRE(node->is_paused());

  node->update(1.0);
  CHECK(node->delta() == Approx(0.0));
  CHECK(node->total_time() == Approx(0.25));

  node->set_paused(false);
  node->update(0.5);
  CHECK(node->delta() == Approx(0.5));
  CHECK(node->total_time() == Approx(0.75));
}

TEST_CASE("clock_node time scale is clamped and scaled", "[runtime][clock]") {
  auto node = std::make_shared<clock_node>("slow");

  node->set_time_scale(2.5);
  CHECK(node->local_time_scale() == Approx(2.5));
  node->update(0.1);
  CHECK(node->delta() == Approx(0.25));

  node->set_time_scale(-1.0);
  CHECK(node->local_time_scale() == Approx(0.0));
  node->update(1.0);
  CHECK(node->delta() == Approx(0.0));

  node->set_time_scale(1000.0);
  CHECK(node->local_time_scale() == Approx(100.0));

  node->set_time_scale(std::numeric_limits<double>::quiet_NaN());
  CHECK(node->local_time_scale() == Approx(1.0));
}

TEST_CASE("clock_node ignores invalid real deltas", "[runtime][clock]") {
  auto node = std::make_shared<clock_node>("game");
  node->update(0.25);

  node->update(-1.0);
  CHECK(node->delta() == Approx(0.0));
  node->update(std::numeric_limits<double>::quiet_NaN());
  CHECK(node->delta() == Approx(0.0));
  node->update(std::numeric_limits<double>::infinity());
  CHECK(node->delta() == Approx(0.0));
  CHECK(node->total_time() == Approx(0.25));
}

TEST_CASE("clock_node inherits ancestor pause and time scale", "[runtime][clock]") {
  auto root = std::make_shared<clock_node>("root");
  auto& game = root->ensure("game");
  auto& ui = root->ensure("ui");

  root->set_time_scale(0.5);
  game->set_time_scale(0.5);

  CHECK(game->effective_time_scale() == Approx(0.25));
  CHECK(ui->effective_time_scale() == Approx(0.5));

  root->update(1.0);
  CHECK(root->delta() == Approx(0.5));
  CHECK(game->delta() == Approx(0.25));
  CHECK(ui->delta() == Approx(0.5));

  const double game_total = game->total_time();
  root->set_paused(true);
  root->update(1.0);
  CHECK(root->delta() == Approx(0.0));
  CHECK(game->delta() == Approx(0.0));
  CHECK(ui->delta() == Approx(0.0));
  CHECK(game->total_time() == Approx(game_total));
}

TEST_CASE("clock_node sibling clocks stay independent", "[runtime][clock]") {
  auto root = std::make_shared<clock_node>("root");
  auto& game = root->ensure("game");
  auto& ui = root->ensure("ui");

  game->set_paused(true);
  root->update(0.5);

  CHECK(game->delta() == Approx(0.0));
  CHECK(ui->delta() == Approx(0.5));
}

TEST_CASE("clock_node ensure contains erase and clear", "[runtime][clock]") {
  auto root = std::make_shared<clock_node>("root");

  auto& game = root->ensure("game");
  CHECK(root->contains("game"));
  CHECK(root->ensure("game") == game);
  CHECK_THROWS_AS(root->ensure(""), std::invalid_argument);
  CHECK_THROWS_AS(root->ensure("a/b"), std::invalid_argument);

  root->ensure("ui");
  CHECK(root->contains("ui"));
  CHECK_FALSE(root->contains("missing"));

  root->erase("game");
  CHECK_FALSE(root->contains("game"));
  CHECK(root->contains("ui"));

  root->clear();
  CHECK_FALSE(root->contains("ui"));
}

TEST_CASE("clock_node detached child stops inheriting", "[runtime][clock]") {
  auto root = std::make_shared<clock_node>("root");
  auto child = root->ensure("child");

  root->set_paused(true);
  root->update(1.0);
  CHECK(child->delta() == Approx(0.0));

  root->erase("child");
  root->set_paused(false);
  root->set_time_scale(2.0);

  child->update(1.0);
  CHECK(child->delta() == Approx(1.0));
  CHECK(child->effective_time_scale() == Approx(1.0));
}

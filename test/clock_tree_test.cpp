#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <memory>

#include "cyberverse/runtime/clock_tree.hpp"

using Catch::Approx;
using cyberverse::runtime::clock_node;
using cyberverse::runtime::clock_tree;

TEST_CASE("clock_tree starts with a root node", "[runtime][clock]") {
  clock_tree tree;

  REQUIRE(tree.root() != nullptr);
  CHECK(tree.root()->name() == "root");
  CHECK(tree.lookup("") == tree.root());
  CHECK(tree.lookup("/") == tree.root());
  CHECK(tree.contains(""));
  CHECK_FALSE(tree.contains("missing"));
}

TEST_CASE("clock_tree ensures nested paths", "[runtime][clock]") {
  clock_tree tree;

  auto game = tree.ensure("game");
  auto ui = tree.ensure("game/ui");
  REQUIRE(game != nullptr);
  REQUIRE(ui != nullptr);

  CHECK(tree.contains("game"));
  CHECK(tree.contains("game/ui"));
  CHECK(tree.ensure("game/ui") == ui);
  CHECK(tree.lookup("game") == game);
  CHECK(tree.lookup("game/ui") == ui);
  CHECK(tree.lookup("/game//ui/") == ui);
}

TEST_CASE("clock_tree erases leaves and detaches them", "[runtime][clock]") {
  clock_tree tree;
  auto game = tree.ensure("game");
  auto ui = tree.ensure("game/ui");

  tree.erase("game/unknown");
  CHECK(tree.contains("game/ui"));

  tree.erase("game/ui");
  CHECK_FALSE(tree.contains("game/ui"));
  CHECK(game->contains("ui") == false);

  // The erased node is detached from its parent and keeps working standalone.
  game->set_time_scale(2.0);
  ui->update(0.5);
  CHECK(ui->delta() == Approx(0.5));
  CHECK(ui->effective_time_scale() == Approx(1.0));
}

TEST_CASE("clock_tree cannot erase the root and clears children", "[runtime][clock]") {
  clock_tree tree;
  tree.ensure("game/ui");
  tree.ensure("audio");

  tree.erase("");
  CHECK(tree.root() != nullptr);

  tree.clear();
  CHECK(tree.root() != nullptr);
  CHECK_FALSE(tree.contains("game"));
  CHECK_FALSE(tree.contains("audio"));
}

TEST_CASE("clock_tree updates every node with inherited state", "[runtime][clock]") {
  clock_tree tree;
  auto root = tree.root();
  auto game = tree.ensure("game");
  auto ui = tree.ensure("game/ui");
  auto audio = tree.ensure("audio");

  root->set_time_scale(0.5);
  game->set_time_scale(0.5);

  tree.update(1.0);
  CHECK(root->delta() == Approx(0.5));
  CHECK(game->delta() == Approx(0.25));
  CHECK(ui->delta() == Approx(0.25));
  CHECK(audio->delta() == Approx(0.5));
  CHECK(ui->total_time() == Approx(0.25));

  game->set_paused(true);
  tree.update(1.0);
  CHECK(game->delta() == Approx(0.0));
  CHECK(ui->delta() == Approx(0.0));
  CHECK(audio->delta() == Approx(0.5));
  CHECK(ui->total_time() == Approx(0.25));
}

TEST_CASE("clock_tree sanitizes invalid deltas", "[runtime][clock]") {
  clock_tree tree;
  auto node = tree.ensure("game");

  tree.update(-1.0);
  CHECK(node->delta() == Approx(0.0));
  tree.update(std::numeric_limits<double>::quiet_NaN());
  CHECK(node->delta() == Approx(0.0));
  tree.update(std::numeric_limits<double>::infinity());
  CHECK(node->delta() == Approx(0.0));
  CHECK(node->total_time() == Approx(0.0));
}

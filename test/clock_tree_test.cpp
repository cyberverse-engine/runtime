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

TEST_CASE("clock_tree validates paths", "[runtime][clock]") {
  CHECK(clock_tree::is_valid_path(""));
  CHECK(clock_tree::is_valid_path("/"));
  CHECK(clock_tree::is_valid_path("game/ui"));
  CHECK(clock_tree::is_valid_path("/game//ui/"));
  CHECK_FALSE(clock_tree::is_valid_path("."));
  CHECK_FALSE(clock_tree::is_valid_path(".."));
  CHECK_FALSE(clock_tree::is_valid_path("game/../ui"));
  CHECK_FALSE(clock_tree::is_valid_path("game/./ui"));
  CHECK_FALSE(clock_tree::is_valid_path(std::string_view("game\0ui", 7)));

  clock_tree tree;
  CHECK(tree.ensure("..") == nullptr);
  CHECK_FALSE(tree.contains(".."));
}

TEST_CASE("clock_tree global pause affects every clock", "[runtime][clock]") {
  clock_tree tree;
  auto game = tree.ensure("game");
  auto ui = tree.ensure("ui");

  tree.set_paused(true);
  CHECK(tree.is_paused());
  tree.update(1.0);
  CHECK(game->delta() == Approx(0.0));
  CHECK(ui->delta() == Approx(0.0));

  tree.set_paused(false);
  tree.update(0.5);
  CHECK(game->delta() == Approx(0.5));
  CHECK(ui->delta() == Approx(0.5));
}

TEST_CASE("clock_node total time stays continuous across scale changes", "[runtime][clock]") {
  auto node = std::make_shared<clock_node>("slow");
  node->set_time_scale(1.0);
  node->update(0.5);
  CHECK(node->total_time() == Approx(0.5));

  node->set_time_scale(2.0);
  node->update(0.25);
  CHECK(node->delta() == Approx(0.5));
  CHECK(node->total_time() == Approx(1.0));
}

TEST_CASE("clock_tree reparents nodes and rejects cycles", "[runtime][clock]") {
  clock_tree tree;
  auto game = tree.ensure("game");
  auto ui = tree.ensure("game/ui");
  auto audio = tree.ensure("audio");
  tree.ensure("ui");

  auto conflict = tree.reparent("game/ui", "");
  REQUIRE_FALSE(conflict.has_value());
  CHECK(conflict.error() == std::make_error_code(std::errc::file_exists));

  auto cycle = tree.reparent("game", "game/ui");
  REQUIRE_FALSE(cycle.has_value());
  CHECK(cycle.error() == std::make_error_code(std::errc::invalid_argument));

  auto missing = tree.reparent("missing", "audio");
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error() == std::make_error_code(std::errc::invalid_argument));

  CHECK(tree.reparent("", "audio").error() == std::make_error_code(std::errc::invalid_argument));

  CHECK(tree.reparent("game/ui", "audio").has_value());
  CHECK_FALSE(tree.contains("game/ui"));
  CHECK(tree.lookup("audio/ui") == ui);
  CHECK_FALSE(ui->is_root());

  game->set_time_scale(2.0);
  audio->set_time_scale(0.5);
  tree.update(1.0);
  CHECK(ui->delta() == Approx(0.5));
}

TEST_CASE("clock_tree clones subtrees", "[runtime][clock]") {
  clock_tree tree;
  auto game = tree.ensure("game");
  game->set_paused(true);
  game->set_time_scale(0.5);
  tree.ensure("game/ui");

  auto clone = tree.clone("game", "copy");
  REQUIRE(clone.has_value());
  CHECK(tree.contains("copy"));
  CHECK(tree.contains("copy/ui"));
  CHECK(tree.lookup("copy") == *clone);
  CHECK((*clone)->is_paused_locally());
  CHECK((*clone)->local_time_scale() == Approx(0.5));
  CHECK((*clone)->total_time() == Approx(0.0));

  auto duplicate = tree.clone("game", "copy");
  REQUIRE_FALSE(duplicate.has_value());
  CHECK(duplicate.error() == std::make_error_code(std::errc::file_exists));

  auto missing = tree.clone("missing", "copy2");
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error() == std::make_error_code(std::errc::invalid_argument));

  auto nested = tree.clone("game", "game/nested");
  REQUIRE_FALSE(nested.has_value());
  CHECK(nested.error() == std::make_error_code(std::errc::invalid_argument));

  tree.update(1.0);
  CHECK((*clone)->total_time() == Approx(0.0));

  tree.ensure("copy")->set_paused(false);
  tree.ensure("copy")->set_time_scale(2.0);
  tree.update(0.25);
  CHECK(tree.ensure("copy")->delta() == Approx(0.5));
  CHECK(tree.ensure("game")->delta() == Approx(0.0));
}

TEST_CASE("clock_tree snapshots and exports", "[runtime][clock]") {
  clock_tree tree;
  tree.ensure("game");
  tree.ensure("game/ui");
  tree.update(0.5);

  const auto snapshot = tree.snapshot();
  REQUIRE(snapshot.size() == 3);
  CHECK(snapshot[0].path.empty());
  CHECK(snapshot[0].name == "root");
  CHECK(snapshot[1].path == "game");
  CHECK(snapshot[2].path == "game/ui");
  CHECK(snapshot[2].delta == Approx(0.5));

  const std::string text = tree.to_text();
  CHECK(text.find("path=/") != std::string::npos);
  CHECK(text.find("path=game/ui") != std::string::npos);

  const std::string json = tree.to_json();
  CHECK(json.find("\"name\":\"root\"") != std::string::npos);
  CHECK(json.find("\"path\":\"game/ui\"") != std::string::npos);
}

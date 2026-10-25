#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <optional>
#include <thread>
#include <vector>

#include "cyberverse/runtime/task_queue.hpp"
#include "cyberverse/runtime/this_thread.hpp"

using cyberverse::runtime::set_this_as_render_thread;
using cyberverse::runtime::task_queue;

namespace {

struct move_only_task {
  explicit move_only_task(int value) : value(value) {}

  move_only_task(const move_only_task&) = delete;
  move_only_task& operator=(const move_only_task&) = delete;
  move_only_task(move_only_task&&) noexcept = default;
  move_only_task& operator=(move_only_task&&) noexcept = default;

  int value = 0;
};

} // namespace

TEST_CASE("task_queue prefers render-thread tasks", "[runtime][queue]") {
  task_queue<int> queue;
  std::thread producer([&] {
    queue.push(1);
    queue.push(2);
  });
  producer.join();

  std::vector<int> popped;
  std::thread render_thread([&] {
    set_this_as_render_thread();
    queue.push(3);

    int value = 0;
    while (queue.try_pop(value)) {
      popped.push_back(value);
    }
  });
  render_thread.join();

  CHECK(popped == std::vector<int>{ 3, 1, 2 });
}

TEST_CASE("task_queue keeps render-thread tasks on the render lane", "[runtime][queue]") {
  task_queue<int> queue;

  std::thread render_producer([&] {
    set_this_as_render_thread();
    queue.push(1);
  });
  render_producer.join();

  std::thread any_producer([&] {
    queue.push(2);
  });
  any_producer.join();

  std::vector<int> non_render_popped;
  std::thread non_render_consumer([&] {
    int value = 0;
    while (queue.try_pop(value)) {
      non_render_popped.push_back(value);
    }
  });
  non_render_consumer.join();

  CHECK(non_render_popped == std::vector<int>{ 2 });

  std::vector<int> render_popped;
  std::thread render_consumer([&] {
    set_this_as_render_thread();
    int value = 0;
    while (queue.try_pop(value)) {
      render_popped.push_back(value);
    }
  });
  render_consumer.join();

  CHECK(render_popped == std::vector<int>{ 1 });
}

TEST_CASE("task_queue pop_locker drains the any-thread lane", "[runtime][queue]") {
  task_queue<int> queue;

  std::thread producer([&] {
    for (int i = 0; i < 3; ++i) {
      queue.push(i);
    }
  });
  producer.join();

  std::vector<int> popped;
  int render_task = 0;
  std::thread render_thread([&] {
    set_this_as_render_thread();
    queue.push(99);

    {
      auto locker = queue.pop_locker();
      while (const std::optional<int> task = locker.try_pop()) {
        popped.push_back(*task);
      }
    }

    int value = 0;
    if (queue.try_pop(value)) {
      render_task = value;
    }
  });
  render_thread.join();

  CHECK(popped == std::vector<int>{ 0, 1, 2 });
  CHECK(render_task == 99);
}

TEST_CASE("task_queue preserves FIFO order per lane", "[runtime][queue]") {
  task_queue<int> queue;

  std::thread any_producer([&] {
    queue.push(10);
    queue.push(11);
  });
  any_producer.join();

  std::vector<int> popped;
  std::thread render_thread([&] {
    set_this_as_render_thread();
    queue.push(1);
    queue.push(2);

    int value = 0;
    while (queue.try_pop(value)) {
      popped.push_back(value);
    }
  });
  render_thread.join();

  CHECK(popped == std::vector<int>{ 1, 2, 10, 11 });
}

TEST_CASE("task_queue stores move-only tasks", "[runtime][queue]") {
  task_queue<move_only_task> queue;

  std::thread producer([&] {
    queue.push(move_only_task(7));
  });
  producer.join();

  int value = -1;
  std::thread render_thread([&] {
    set_this_as_render_thread();
    move_only_task task(0);
    if (queue.try_pop(task)) {
      value = task.value;
    }
  });
  render_thread.join();

  CHECK(value == 7);
}

TEST_CASE("task_queue pop_locker releases the any-thread lock", "[runtime][queue]") {
  task_queue<int> queue;
  std::atomic<bool> producer_started{ false };
  bool producer_finished = false;

  std::thread producer;
  {
    auto locker = queue.pop_locker();
    producer = std::thread([&] {
      producer_started.store(true, std::memory_order_release);
      queue.push(7);
      producer_finished = true;
    });

    while (!producer_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }
  producer.join();

  CHECK(producer_finished);

  int value = 0;
  {
    auto locker = queue.pop_locker();
    CHECK(locker.try_pop(value));
  }
  CHECK(value == 7);
}

TEST_CASE("task_queue accepts concurrent non-render producers", "[runtime][queue]") {
  constexpr int producer_count = 4;
  constexpr int tasks_per_producer = 100;

  task_queue<int> queue;
  std::vector<std::thread> producers;
  producers.reserve(producer_count);

  for (int producer = 0; producer < producer_count; ++producer) {
    producers.emplace_back([&queue, producer] {
      for (int i = 0; i < tasks_per_producer; ++i) {
        queue.push(producer * tasks_per_producer + i);
      }
    });
  }
  for (std::thread& producer : producers) {
    producer.join();
  }

  std::vector<int> popped;
  std::thread render_thread([&] {
    set_this_as_render_thread();
    int value = 0;
    while (queue.try_pop(value)) {
      popped.push_back(value);
    }
  });
  render_thread.join();

  REQUIRE(popped.size() == static_cast<std::size_t>(producer_count * tasks_per_producer));
  std::sort(popped.begin(), popped.end());
  for (int i = 0; i < producer_count * tasks_per_producer; ++i) {
    CHECK(popped[static_cast<std::size_t>(i)] == i);
  }
}

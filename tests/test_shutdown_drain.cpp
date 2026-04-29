// Regression tests for finding F2: drain-on-close semantics.
// Producers refuse new work after shutdown; consumers continue to drain
// remaining items and only signal "closed" when the queue is empty AND
// the container is inactive.

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <tsc/thread_safe_container.hpp>

using namespace std::chrono_literals;
using tsc::ShutdownException;
using tsc::ThreadSafeContainer;

TEST_CASE("after shutdown, consumers still drain remaining items (F2)",
          "[shutdown][drain][regression-F2]") {
  ThreadSafeContainer<int> q{10};
  for (int i = 0; i < 5; ++i) REQUIRE(q.tryAdd(i));

  q.shutdown();

  // Consumers can still extract every item that was already enqueued.
  for (int expected = 0; expected < 5; ++expected) {
    int v = -1;
    REQUIRE_NOTHROW(q.tryRemove(v));
    REQUIRE(v == expected);
  }

  // Once empty AND shut down, tryRemove(T&) signals ShutdownException.
  int sink = 0;
  REQUIRE_THROWS_AS(q.tryRemove(sink), ShutdownException);

  // Optional-returning variant simply returns nullopt (no throw).
  REQUIRE_FALSE(q.tryRemove().has_value());
}

TEST_CASE("waitRemove drains remaining items then unblocks with empty optional",
          "[shutdown][drain][regression-F2]") {
  ThreadSafeContainer<int> q{10};
  REQUIRE(q.tryAdd(1));
  REQUIRE(q.tryAdd(2));

  std::atomic<int> drained{0};
  std::thread consumer([&] {
    while (auto v = q.waitRemove()) {
      ++drained;
    }
  });

  // Give the consumer time to read both items, then shut down.
  std::this_thread::sleep_for(50ms);
  q.shutdown();
  consumer.join();

  REQUIRE(drained.load() == 2);
}

TEST_CASE("after shutdown, producers refuse new work", "[shutdown][producer]") {
  ThreadSafeContainer<int> q{4};
  q.shutdown();

  REQUIRE_THROWS_AS(q.tryAdd(1), ShutdownException);
  REQUIRE_THROWS_AS(q.waitAdd(2), ShutdownException);
  REQUIRE_THROWS_AS(q.tryEmplaceAdd(3), ShutdownException);
  REQUIRE_THROWS_AS(q.emplaceAdd(4), ShutdownException);
}

TEST_CASE("shutdown is idempotent", "[shutdown]") {
  ThreadSafeContainer<int> q{2};
  REQUIRE(q.isActive());
  q.shutdown();
  REQUIRE_FALSE(q.isActive());
  REQUIRE_NOTHROW(q.shutdown());  // Second call is a no-op.
  REQUIRE_FALSE(q.isActive());
}

TEST_CASE("shutdown unblocks waiting consumers immediately",
          "[shutdown][wakeup]") {
  ThreadSafeContainer<int> q{4};

  std::thread consumer([&] {
    auto v = q.waitRemove();  // Blocks: queue is empty, container active.
    REQUIRE_FALSE(
        v.has_value());  // After shutdown of empty queue, returns nullopt.
  });

  std::this_thread::sleep_for(20ms);
  q.shutdown();
  consumer.join();
}

TEST_CASE("shutdown unblocks waiting producers immediately",
          "[shutdown][wakeup]") {
  ThreadSafeContainer<int> q{1};
  REQUIRE(q.tryAdd(1));  // Now full.

  std::atomic<bool> threw{false};
  std::thread producer([&] {
    try {
      q.waitAdd(2);
    } catch (const ShutdownException&) {
      threw = true;
    }
  });

  std::this_thread::sleep_for(20ms);
  q.shutdown();
  producer.join();
  REQUIRE(threw.load());
}

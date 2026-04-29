#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <tsc/thread_safe_container.hpp>

using namespace std::chrono_literals;
using tsc::QueueStatus;

TEST_CASE("tryRemoveFor times out on empty active queue", "[timed]") {
  tsc::ThreadSafeContainer<int> q{2};
  int v = 0;
  const auto start = std::chrono::steady_clock::now();
  const auto status = q.tryRemoveFor(v, 25ms);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  REQUIRE(status == QueueStatus::Timeout);
  REQUIRE(elapsed >= 20ms);  // Allow for clock granularity.
}

TEST_CASE("tryAddFor times out on full queue", "[timed]") {
  tsc::ThreadSafeContainer<int> q{1};
  REQUIRE(q.tryAdd(1));
  const auto status = q.tryAddFor(2, 25ms);
  REQUIRE(status == QueueStatus::Full);
}

TEST_CASE("tryRemoveFor returns Closed once drained and shut down", "[timed]") {
  tsc::ThreadSafeContainer<int> q{2};
  q.shutdown();
  int v = 0;
  REQUIRE(q.tryRemoveFor(v, 5ms) == QueueStatus::Closed);
}

TEST_CASE("tryAddFor returns Closed when shut down", "[timed]") {
  tsc::ThreadSafeContainer<int> q{1};
  q.shutdown();
  REQUIRE(q.tryAddFor(99, 5ms) == QueueStatus::Closed);
}

TEST_CASE("tryRemoveFor succeeds when an item arrives during the wait",
          "[timed]") {
  tsc::ThreadSafeContainer<int> q{2};
  std::thread producer([&] {
    std::this_thread::sleep_for(10ms);
    REQUIRE(q.tryAdd(42));
  });

  int v = 0;
  REQUIRE(q.tryRemoveFor(v, 200ms) == QueueStatus::Ok);
  REQUIRE(v == 42);
  producer.join();
}

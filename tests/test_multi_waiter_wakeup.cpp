// Regression test for finding F1: lost-wakeup hazard with multiple waiters.
//
// With the original conditional notify_one() ("only notify when size == 1"),
// the following scenario could leave readers blocked even though the queue
// became non-empty: a producer pushes (0->1) and notifies one reader, then a
// second producer wins the lock race before the woken reader runs, pushes
// (1->2), and skips the notify because size != 1. The second and third
// blocked readers would never wake. With unconditional notify_one() this
// cannot happen.

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <tsc/thread_safe_container.hpp>

using namespace std::chrono_literals;
using tsc::ThreadSafeContainer;

TEST_CASE("multiple blocked consumers all wake up (regression-F1)",
          "[wakeup][regression-F1]") {
  constexpr int kReaders = 8;
  ThreadSafeContainer<int> q{16};

  std::atomic<int> woken{0};
  std::vector<std::thread> readers;
  readers.reserve(kReaders);
  for (int i = 0; i < kReaders; ++i) {
    readers.emplace_back([&] {
      auto v = q.waitRemove();
      if (v.has_value()) ++woken;
    });
  }

  // Give all readers time to actually block on not_empty_.
  std::this_thread::sleep_for(50ms);

  // Push one item per reader. Under the original buggy code, only the first
  // notification would fire reliably and the rest of the readers would stay
  // blocked. Under the fix every push notifies one waiter.
  for (int i = 0; i < kReaders; ++i) {
    REQUIRE(q.tryAdd(i));
  }

  for (auto& t : readers) t.join();
  REQUIRE(woken.load() == kReaders);
}

TEST_CASE("multiple blocked producers all wake up (regression-F1, full side)",
          "[wakeup][regression-F1]") {
  constexpr int kProducers = 8;
  ThreadSafeContainer<int> q{1};
  REQUIRE(q.tryAdd(0));  // Now full.

  std::atomic<int> pushed{0};
  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (int i = 0; i < kProducers; ++i) {
    producers.emplace_back([&, i] {
      q.waitAdd(i + 1);
      ++pushed;
    });
  }

  std::this_thread::sleep_for(50ms);

  // Drain one slot at a time. Under the original buggy code only the
  // empty-edge notification (size == max_size - 1) would fire reliably.
  for (int i = 0; i <= kProducers; ++i) {
    int v = 0;
    REQUIRE(q.tryRemove(v));
    std::this_thread::sleep_for(5ms);  // Let one producer push.
  }

  for (auto& t : producers) t.join();
  REQUIRE(pushed.load() == kProducers);
}

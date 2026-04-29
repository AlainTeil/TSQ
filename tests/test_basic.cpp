#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <tsc/thread_safe_container.hpp>

using tsc::QueueStatus;
using tsc::ShutdownException;
using tsc::ThreadSafeContainer;

TEST_CASE("constructor rejects zero capacity", "[basic][construction]") {
  REQUIRE_THROWS_AS(ThreadSafeContainer<int>(0), std::invalid_argument);
}

TEST_CASE("capacity is reported correctly", "[basic][observers]") {
  ThreadSafeContainer<int> q{5};
  REQUIRE(q.capacity() == 5);
  REQUIRE(q.size() == 0);
  REQUIRE(q.empty());
  REQUIRE_FALSE(q.full());
  REQUIRE(q.isActive());
}

TEST_CASE("tryAdd / tryRemove round-trip preserves FIFO order",
          "[basic][fifo]") {
  ThreadSafeContainer<int> q{3};
  REQUIRE(q.tryAdd(1));
  REQUIRE(q.tryAdd(2));
  REQUIRE(q.tryAdd(3));
  REQUIRE(q.full());

  // Adding to a full queue returns false, no exception.
  REQUIRE_FALSE(q.tryAdd(4));

  int v = 0;
  REQUIRE(q.tryRemove(v));
  REQUIRE(v == 1);
  REQUIRE(q.tryRemove(v));
  REQUIRE(v == 2);
  REQUIRE(q.tryRemove(v));
  REQUIRE(v == 3);
  REQUIRE(q.empty());

  // tryRemove on empty active queue returns false, no exception.
  REQUIRE_FALSE(q.tryRemove(v));
}

TEST_CASE("optional-returning tryRemove on empty active queue",
          "[basic][optional]") {
  ThreadSafeContainer<int> q{2};
  REQUIRE_FALSE(q.tryRemove().has_value());
  REQUIRE(q.tryAdd(42));
  auto v = q.tryRemove();
  REQUIRE(v.has_value());
  REQUIRE(*v == 42);
}

TEST_CASE("clear empties the queue and notifies producers", "[basic][clear]") {
  ThreadSafeContainer<int> q{2};
  REQUIRE(q.tryAdd(10));
  REQUIRE(q.tryAdd(20));
  REQUIRE(q.full());

  q.clear();

  REQUIRE(q.empty());
  REQUIRE_FALSE(q.full());
  // Container remains active after clear().
  REQUIRE(q.isActive());
  REQUIRE(q.tryAdd(99));
}

TEST_CASE("drain returns FIFO-ordered items and empties the queue",
          "[basic][drain]") {
  ThreadSafeContainer<int> q{4};
  for (int i = 1; i <= 4; ++i) REQUIRE(q.tryAdd(i));

  auto items = q.drain();
  REQUIRE(items.size() == 4);
  REQUIRE(items[0] == 1);
  REQUIRE(items[1] == 2);
  REQUIRE(items[2] == 3);
  REQUIRE(items[3] == 4);
  REQUIRE(q.empty());
}

TEST_CASE("capacity 1 boundary case", "[basic][boundary]") {
  ThreadSafeContainer<int> q{1};
  REQUIRE(q.tryAdd(7));
  REQUIRE(q.full());
  REQUIRE_FALSE(q.tryAdd(8));
  int v = 0;
  REQUIRE(q.tryRemove(v));
  REQUIRE(v == 7);
  REQUIRE(q.empty());
  REQUIRE(q.tryAdd(8));
}

TEST_CASE("emplaceAdd constructs in place", "[basic][emplace]") {
  ThreadSafeContainer<std::string> q{2};
  REQUIRE(q.tryEmplaceAdd(5, 'x'));  // std::string(5, 'x')
  auto v = q.tryRemove();
  REQUIRE(v.has_value());
  REQUIRE(*v == "xxxxx");
}

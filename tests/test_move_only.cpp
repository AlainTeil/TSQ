// Verifies that the container works with move-only types (e.g.,
// std::unique_ptr). The original code would fail to compile for these
// because tryRemove(T&) used copy-assignment.

#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <tsc/thread_safe_container.hpp>

TEST_CASE("move-only type round-trip via rvalue add and optional remove",
          "[move-only]") {
  tsc::ThreadSafeContainer<std::unique_ptr<int>> q{4};

  REQUIRE(q.tryAdd(std::make_unique<int>(11)));
  REQUIRE(q.tryAdd(std::make_unique<int>(22)));

  auto a = q.tryRemove();
  REQUIRE(a.has_value());
  REQUIRE(**a == 11);

  auto b = q.tryRemove();
  REQUIRE(b.has_value());
  REQUIRE(**b == 22);

  REQUIRE(q.empty());
}

TEST_CASE("emplaceAdd works for move-only types", "[move-only][emplace]") {
  tsc::ThreadSafeContainer<std::unique_ptr<int>> q{2};
  REQUIRE(q.tryEmplaceAdd(std::make_unique<int>(7)));
  auto v = q.tryRemove();
  REQUIRE(v.has_value());
  REQUIRE(**v == 7);
}

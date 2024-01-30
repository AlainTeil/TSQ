#pragma once

#include <random>

namespace RND {
auto pick(int from, int to) {
  static thread_local auto engine =
      std::default_random_engine{std::random_device{}()};
  auto distribution = std::uniform_int_distribution<>{from, to};

  return distribution(engine);
}
}  // namespace RND

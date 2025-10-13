#pragma once

#include <random>

/**
 * @namespace RND
 * @brief Provides utilities for random number generation.
 *
 * The RND namespace contains functions to generate random numbers,
 * including integers within a specified range. It utilizes thread-local
 * random engines to ensure thread safety and reproducibility.
 */
namespace RND {
///
/// @brief Returns a random integer in the range [from, to] (inclusive).
/// @param from The lower bound of the range.
/// @param to The upper bound of the range.
/// @return A random integer between from and to, inclusive.
///
auto pick(int from, int to) {
  static thread_local auto engine =
      std::default_random_engine{std::random_device{}()};
  auto distribution = std::uniform_int_distribution<>{from, to};

  return distribution(engine);
}
}  // namespace RND

#pragma once

#include <random>

namespace tsc {
namespace random {

/**
 * @brief Generates a random integer in the specified range.
 * @param min Lower bound (inclusive).
 * @param max Upper bound (inclusive).
 * @return Random integer between min and max.
 */
inline int uniform(int min, int max) {
  static thread_local std::mt19937 engine{std::random_device{}()};
  std::uniform_int_distribution<int> distribution{min, max};
  return distribution(engine);
}

}  // namespace random
}  // namespace tsc

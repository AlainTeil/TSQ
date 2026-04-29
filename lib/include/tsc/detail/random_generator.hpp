#pragma once

// Internal utility used by the test/example apps. Not part of the stable
// public API of the tsc library.

#include <random>
#include <type_traits>

namespace tsc {
namespace detail {
namespace random {

/**
 * @brief Generates a random integer in the specified inclusive range [min,max].
 *
 * Uses a thread_local Mersenne Twister to avoid lock contention across
 * threads.
 *
 * @tparam IntT Integral type. Defaults to int for backward compatibility.
 */
template <typename IntT = int>
inline IntT uniform(IntT min, IntT max) {
  static_assert(std::is_integral_v<IntT>, "uniform requires an integral type");
  static thread_local std::mt19937 engine{std::random_device{}()};
  std::uniform_int_distribution<IntT> distribution{min, max};
  return distribution(engine);
}

}  // namespace random
}  // namespace detail
}  // namespace tsc

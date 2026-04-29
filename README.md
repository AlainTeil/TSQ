# tsc — A C++17 Header-Only Thread-Safe Bounded FIFO

`tsc::ThreadSafeContainer<T>` is a header-only, bounded, multi-producer /
multi-consumer FIFO queue built on `std::mutex` and two
`std::condition_variable`s. It supports non-blocking, blocking, and
time-limited operations, and follows **drain-on-close** shutdown semantics so
no in-flight item is silently lost.

> Built and tested with C++20 (the library itself only requires C++17). All
> public API names from the original 1.0 release are preserved; new
> overloads (rvalue, emplace, optional-returning, timed, status-returning)
> are additive.

---

## Features

* `tryAdd` / `waitAdd` / `tryEmplaceAdd` / `emplaceAdd` — non-blocking and
  blocking insert, including in-place construction and rvalue overloads.
* `tryRemove` / `waitRemove` — non-blocking and blocking extract, with both
  out-parameter (legacy) and `std::optional<T>`-returning overloads.
* `tryAddFor` / `tryRemoveFor` — time-limited, non-throwing variants
  returning a `tsc::QueueStatus`.
* `shutdown()` + drain-on-close: producers refuse new work immediately;
  consumers continue to drain everything that was already enqueued.
* `clear()` / `drain()` — bulk removal (discard / collect).
* `size()` / `empty()` / `full()` / `isActive()` / `capacity()` — observers.

### Requirements on `T`

`T` must be a non-reference object type that is move-constructible and
destructible. These are enforced by `static_assert` so misuse produces a
clear diagnostic at the point of instantiation. The const-ref `tryAdd` /
`waitAdd` overloads additionally require `T` to be copy-constructible.

## Build

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

CMake options:

| Option | Default | Meaning |
|---|---|---|
| `TSC_BUILD_APP` | `ON` | Build the `tsc_test` integration stress test. |
| `TSC_BUILD_TESTS` | `ON` | Build Catch2 unit tests under `tests/`. |
| `TSC_BUILD_EXAMPLES` | `ON` | Build minimal usage examples. |
| `TSC_BUILD_BENCHMARKS` | `OFF` | Build microbenchmarks under `benchmarks/` (requires Google Benchmark; fetched automatically if absent). |
| `BUILD_TESTING` | `ON` | Standard CTest gate. |
| `TSC_WARNINGS_AS_ERRORS` | `OFF` | Treat compile warnings as errors. |
| `ENABLE_ASAN` | `OFF` | AddressSanitizer + UBSan. |
| `ENABLE_TSAN` | `OFF` | ThreadSanitizer (mutually exclusive with `ENABLE_ASAN`). |

Catch2 v3 is used for unit tests; if not installed, it is fetched
automatically via `FetchContent`.

### Sanitizer builds

```bash
# AddressSanitizer + UBSan
cmake -S . -B build-asan -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure

# ThreadSanitizer
cmake -S . -B build-tsan -DENABLE_TSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure
```

> **Ubuntu 24.04 + TSan note:** newer kernels use 32-bit ASLR entropy that
> conflicts with TSan's shadow-memory layout (`FATAL: ThreadSanitizer:
> unexpected memory mapping`). When `ENABLE_TSAN=ON` on Linux, the build
> system detects this automatically and wires `setarch -R` into
> `CMAKE_CROSSCOMPILING_EMULATOR` so that both the build-time
> `catch_discover_tests` step and `ctest` itself run through it
> transparently. Look for `TSan + Linux: tests will be launched via ...
> setarch -R` in the configure output. If `setarch` is not installed
> (`util-linux` package), CMake will warn and you must wrap test
> invocations manually. Note that `CMAKE_CROSSCOMPILING_EMULATOR` only
> applies to `ctest` (and the build-time test-discovery step); if you
> invoke a test binary directly, run it through `setarch -R` yourself,
> e.g. `setarch -R ./build-tsan/tests/tsc_unit_tests`.

## Code formatting

The repository ships a `.clang-format` file (LLVM/Google-derived, 80-col).
CI enforces it via `clang-format --dry-run --Werror`, so any non-conforming
change will fail the `format-check` job.

Two equivalent ways to apply formatting locally before committing:

```bash
# 1. Via the CMake target (uses whichever clang-format is on PATH)
cmake --build build --target format

# 2. Directly, matching the exact command CI runs
find lib/include app/src tests examples \
     -type f \( -name '*.hpp' -o -name '*.cpp' -o -name '*.h' \) -print0 \
     | xargs -0 clang-format -i

# Dry-run check (no edits, exits non-zero if anything would change)
find lib/include app/src tests examples \
     -type f \( -name '*.hpp' -o -name '*.cpp' -o -name '*.h' \) -print0 \
     | xargs -0 clang-format --dry-run --Werror
```

CI uses the `clang-format` shipped with `ubuntu-24.04` (currently 18.x); for
byte-identical results locally, install the same major version
(`sudo apt-get install clang-format` on Ubuntu 24.04).

## Minimal example

```cpp
#include <iostream>
#include <thread>
#include <tsc/thread_safe_container.hpp>

int main() {
  tsc::ThreadSafeContainer<int> q{8};

  std::thread producer([&]{
    for (int i = 0; i < 10; ++i) q.waitAdd(i);
    q.shutdown();          // Drain-on-close: consumers still see all items.
  });

  std::thread consumer([&]{
    while (auto v = q.waitRemove()) std::cout << *v << '\n';
  });

  producer.join();
  consumer.join();
}
```

See [`examples/producer_consumer.cpp`](examples/producer_consumer.cpp).

## Shutdown semantics (drain-on-close)

After `shutdown()`:

* **Producers** (`tryAdd`, `waitAdd`, `tryEmplaceAdd`, `emplaceAdd`) throw
  `tsc::ShutdownException`. The timed `tryAddFor` returns `QueueStatus::Closed`.
* **Consumers** continue to extract items already in the queue. They only
  signal "closed" once the queue is empty AND the container is inactive:
  * `tryRemove(T&)` and `waitRemove(T&)` throw `ShutdownException` when
    empty AND shut down.
  * `tryRemove()` (optional) returns `std::nullopt` whenever the queue is
    empty (no exception).
  * `waitRemove()` (optional) returns `std::nullopt` once drained AND shut
    down.
  * `tryRemoveFor(T&, timeout)` returns `QueueStatus::Closed`.

This prevents the silent data loss that the original 1.0 implementation
suffered from when items were in flight at shutdown.

## Lifetime / destructor contract

The destructor calls `shutdown()` and then `clear()`. It does **not** join
worker threads. The caller MUST ensure no thread is executing a member
function on the container at the moment of destruction; otherwise the
`std::mutex` / condition variables may be destroyed while in use.

Recommended patterns:

* Wrap the container in `std::shared_ptr` held by every worker thread.
* Or, join all workers BEFORE the container goes out of scope.

## Installation as a CMake package

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

In a downstream project:

```cmake
find_package(tsc 1.1 REQUIRED)
target_link_libraries(my_target PRIVATE tsc::tsc)
```

## Project layout

```
.
├── CMakeLists.txt
├── lib/
│   ├── CMakeLists.txt
│   ├── cmake/tscConfig.cmake.in
│   └── include/tsc/
│       ├── thread_safe_container.hpp
│       └── detail/random_generator.hpp   # internal utility
├── app/                        # integration stress test (`tsc_test`)
├── tests/                      # Catch2 unit tests (incl. F1/F2 regressions)
├── examples/                   # minimal producer/consumer demo
├── benchmarks/                 # Google Benchmark microbenchmarks (opt-in)
└── docs/                       # Doxygen configuration
```

## Benchmarks

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DTSC_BUILD_BENCHMARKS=ON
cmake --build build-bench -j
./build-bench/benchmarks/tsc_benchmarks --benchmark_min_time=1s
```

## API documentation

If [Doxygen](https://www.doxygen.nl/) is installed, a `docs` target is
registered automatically:

```bash
cmake --build build --target docs
xdg-open build/docs/html/index.html
```

## License

MIT — see [`LICENSE`](LICENSE).

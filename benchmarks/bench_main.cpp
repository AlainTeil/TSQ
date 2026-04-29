// Microbenchmarks for tsc::ThreadSafeContainer.
//
// Workloads:
//   * SPSC throughput
//   * MPMC throughput at varying producer/consumer counts
//   * Single-threaded push/pop overhead (mutex + cv cost in absence of
//     contention)
//
// Build & run:
//   cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DTSC_BUILD_BENCHMARKS=ON
//   cmake --build build-bench -j
//   ./build-bench/benchmarks/tsc_benchmarks --benchmark_min_time=2s

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

#include <tsc/thread_safe_container.hpp>

namespace {

constexpr std::size_t kQueueCapacity = 1024;

// -----------------------------------------------------------------------------
// Single-threaded push+pop: measures the raw mutex+cv overhead per op pair
// when there is no contention. Useful baseline for the notify-optimization
// work.
// -----------------------------------------------------------------------------
static void BM_SingleThreadPushPop(benchmark::State& state) {
  tsc::ThreadSafeContainer<std::uint64_t> q{kQueueCapacity};
  std::uint64_t i = 0;
  for (auto _ : state) {
    q.waitAdd(i++);
    auto v = q.waitRemove();
    benchmark::DoNotOptimize(v);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SingleThreadPushPop);

// -----------------------------------------------------------------------------
// SPSC throughput: one producer thread continuously enqueues while one
// consumer thread continuously dequeues for `state.range(0)` items per
// iteration. Reports items/sec.
// -----------------------------------------------------------------------------
static void BM_SPSC_Throughput(benchmark::State& state) {
  const std::size_t kItems = static_cast<std::size_t>(state.range(0));
  for (auto _ : state) {
    tsc::ThreadSafeContainer<std::uint64_t> q{kQueueCapacity};
    std::thread prod([&] {
      for (std::size_t i = 0; i < kItems; ++i) q.waitAdd(i);
      q.shutdown();
    });
    std::thread cons([&] {
      while (auto v = q.waitRemove()) benchmark::DoNotOptimize(v);
    });
    prod.join();
    cons.join();
  }
  state.SetItemsProcessed(state.iterations() * kItems);
}
BENCHMARK(BM_SPSC_Throughput)->Arg(100'000)->Unit(benchmark::kMillisecond);

// -----------------------------------------------------------------------------
// MPMC throughput: P producers and C consumers race for `state.range(2)`
// items per iteration. range(0)=producers, range(1)=consumers,
// range(2)=items per producer.
// -----------------------------------------------------------------------------
static void BM_MPMC_Throughput(benchmark::State& state) {
  const std::size_t P = static_cast<std::size_t>(state.range(0));
  const std::size_t C = static_cast<std::size_t>(state.range(1));
  const std::size_t kItemsPerProducer = static_cast<std::size_t>(state.range(2));

  for (auto _ : state) {
    tsc::ThreadSafeContainer<std::uint64_t> q{kQueueCapacity};

    std::vector<std::thread> producers;
    producers.reserve(P);
    for (std::size_t p = 0; p < P; ++p) {
      producers.emplace_back([&, p] {
        const std::uint64_t base = static_cast<std::uint64_t>(p) << 32U;
        for (std::size_t i = 0; i < kItemsPerProducer; ++i) {
          q.waitAdd(base | i);
        }
      });
    }

    std::vector<std::thread> consumers;
    consumers.reserve(C);
    std::atomic<std::size_t> consumed{0};
    for (std::size_t c = 0; c < C; ++c) {
      consumers.emplace_back([&] {
        while (auto v = q.waitRemove()) {
          benchmark::DoNotOptimize(v);
          consumed.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }

    for (auto& t : producers) t.join();
    q.shutdown();
    for (auto& t : consumers) t.join();
  }
  state.SetItemsProcessed(state.iterations() * P * kItemsPerProducer);
  state.counters["P"] = static_cast<double>(P);
  state.counters["C"] = static_cast<double>(C);
}

// MPMC sweeps: balanced and asymmetric.
BENCHMARK(BM_MPMC_Throughput)
    ->Args({1, 1, 50'000})
    ->Args({2, 2, 25'000})
    ->Args({4, 4, 12'500})
    ->Args({8, 8, 6'250})
    ->Args({16, 16, 3'125})
    ->Args({1, 8, 50'000})
    ->Args({8, 1, 50'000})
    ->Unit(benchmark::kMillisecond);

}  // namespace

BENCHMARK_MAIN();

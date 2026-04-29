// =============================================================================
// tsc::ThreadSafeContainer integration / stress test.
//
// Unlike the original version of this file, the test now ACTUALLY VERIFIES
// the central claim of the library: that under concurrent producers and
// consumers, every produced item is delivered exactly once, in per-producer
// FIFO order, with no items lost across shutdown ("drain on close").
//
// Verification:
//   * items_written == items_read
//   * No duplicates (each (producer_id, sequence) pair seen once)
//   * Per-producer FIFO ordering is preserved
//   * No items remain in the container after orderly shutdown + drain
//   * Returns non-zero exit code on failure (works in Release; does NOT
//     rely on assert()).
// =============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tsc/detail/random_generator.hpp>
#include <tsc/thread_safe_container.hpp>

namespace {

namespace cfg {
constexpr std::size_t kWriterThreads = 19;
constexpr std::size_t kReaderThreads = 19;
constexpr auto        kTestDuration  = std::chrono::seconds{3};
constexpr std::size_t kWriteAttempts = 9;
constexpr int         kWriteDelayMin = 100;
constexpr int         kWriteDelayMax = 200;
constexpr int         kReadDelayMin  = 5;
constexpr int         kReadDelayMax  = 20;
constexpr std::size_t kQueueCapacity = 70;
}  // namespace cfg

// Encoded item: high 32 bits = producer id, low 32 bits = sequence number.
// This lets us verify both no-duplicates and per-producer FIFO ordering
// without any bookkeeping inside the queue itself.
constexpr std::uint64_t encode(std::uint32_t producer, std::uint32_t seq) {
  return (static_cast<std::uint64_t>(producer) << 32) | seq;
}
constexpr std::uint32_t decode_producer(std::uint64_t v) { return static_cast<std::uint32_t>(v >> 32); }
constexpr std::uint32_t decode_seq(std::uint64_t v)      { return static_cast<std::uint32_t>(v & 0xFFFFFFFFu); }

struct Results {
  std::chrono::milliseconds duration{};
  std::size_t items_written{0};
  std::size_t items_read{0};
  std::size_t writer_exceptions{0};
  std::size_t reader_exceptions{0};
  std::size_t final_size{0};
  bool        no_duplicates{true};
  bool        fifo_per_producer{true};
};

class StressTest {
 public:
  explicit StressTest(std::size_t capacity) : container_{capacity} {}

  Results run() {
    std::cout << "=== ThreadSafeContainer Integration Test ===\n"
              << "Configuration:\n"
              << "  Writers:        " << cfg::kWriterThreads << "\n"
              << "  Readers:        " << cfg::kReaderThreads << "\n"
              << "  Duration:       " << cfg::kTestDuration.count() << "s\n"
              << "  Queue capacity: " << cfg::kQueueCapacity << "\n\n";

    const auto t_start = std::chrono::steady_clock::now();

    // Per-thread counts and per-reader collected items, to avoid contention
    // on shared structures during the stress phase. Each item is tagged
    // with a global, atomically-incremented dequeue order so that we can
    // reconstruct the exact global pop sequence afterwards (needed for the
    // per-producer FIFO check).
    writer_counts_.assign(cfg::kWriterThreads, 0);
    reader_buckets_.assign(cfg::kReaderThreads, {});

    std::vector<std::thread> writers;
    writers.reserve(cfg::kWriterThreads);
    for (std::uint32_t i = 0; i < cfg::kWriterThreads; ++i) {
      writers.emplace_back([this, i]() noexcept {
        try { writerTask(i); }
        catch (...) { writer_exceptions_.fetch_add(1, std::memory_order_relaxed); }
      });
    }

    std::vector<std::thread> readers;
    readers.reserve(cfg::kReaderThreads);
    for (std::uint32_t i = 0; i < cfg::kReaderThreads; ++i) {
      readers.emplace_back([this, i]() noexcept {
        try { readerTask(i); }
        catch (...) { reader_exceptions_.fetch_add(1, std::memory_order_relaxed); }
      });
    }

    std::this_thread::sleep_for(cfg::kTestDuration);

    std::cout << "Stopping producers...\n";
    keep_writing_.store(false, std::memory_order_release);
    for (auto& t : writers) t.join();

    // Producers have all stopped. Now shut down the container; consumers
    // will drain whatever remains and then exit. (drain-on-close.)
    std::cout << "Initiating shutdown (drain-on-close)...\n";
    container_.shutdown();
    for (auto& t : readers) t.join();

    const auto t_end = std::chrono::steady_clock::now();

    Results r;
    r.duration = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start);
    r.writer_exceptions = writer_exceptions_.load();
    r.reader_exceptions = reader_exceptions_.load();
    for (auto c : writer_counts_) r.items_written += c;

    // Anything left here would mean the drain-on-close contract was violated.
    r.final_size = container_.size();
    auto leftover = container_.drain();
    r.final_size += leftover.size();

    // Merge every dequeued item across all readers into one timeline
    // ordered by the global dequeue sequence, then verify no-duplicates
    // and per-producer FIFO order.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> timeline;
    timeline.reserve(r.items_written);
    for (const auto& bucket : reader_buckets_) {
      for (const auto& [order, value] : bucket) {
        timeline.emplace_back(order, value);
      }
    }
    // Items still queued at shutdown were drained above; assign them a
    // synthetic order at the end of the timeline (they were never consumed
    // by a reader). Their relative order is FIFO from the queue.
    std::uint64_t synth = dequeue_order_.load();
    for (auto v : leftover) timeline.emplace_back(++synth, v);

    std::sort(timeline.begin(), timeline.end(),
              [](auto& a, auto& b) { return a.first < b.first; });

    std::unordered_set<std::uint64_t> seen;
    seen.reserve(timeline.size());
    std::unordered_map<std::uint32_t, std::uint32_t> last_seq_per_producer;

    for (const auto& [order, v] : timeline) {
      ++r.items_read;
      if (!seen.insert(v).second) {
        r.no_duplicates = false;
      }
      const auto p = decode_producer(v);
      const auto s = decode_seq(v);
      auto it = last_seq_per_producer.find(p);
      if (it != last_seq_per_producer.end() && s <= it->second) {
        r.fifo_per_producer = false;
      }
      last_seq_per_producer[p] = s;
    }

    return r;
  }

 private:
  void writerTask(std::uint32_t producer_id) {
    const int delay_ms = tsc::detail::random::uniform(cfg::kWriteDelayMin, cfg::kWriteDelayMax);
    for (std::uint32_t i = 0; i < cfg::kWriteAttempts; ++i) {
      if (!keep_writing_.load(std::memory_order_acquire)) break;
      const std::uint64_t value = encode(producer_id, i);
      // Block until space is available; producer may stop early if the
      // container is shut down (which only happens after all writers join).
      container_.waitAdd(value);
      ++writer_counts_[producer_id];
      std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms});
    }
  }

  void readerTask(std::uint32_t reader_id) {
    const int delay_ms = tsc::detail::random::uniform(cfg::kReadDelayMin, cfg::kReadDelayMax);
    auto& bucket = reader_buckets_[reader_id];
    while (true) {
      auto opt = container_.waitRemove();
      if (!opt) break;  // Drained and closed.
      const std::uint64_t order =
          dequeue_order_.fetch_add(1, std::memory_order_relaxed);
      bucket.emplace_back(order, *opt);
      // Light per-item delay to keep the queue exercising the full/empty
      // edges without making the test wall-clock long.
      std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms});
    }
  }

  tsc::ThreadSafeContainer<std::uint64_t> container_;
  std::atomic<bool>        keep_writing_{true};
  std::atomic<std::size_t> writer_exceptions_{0};
  std::atomic<std::size_t> reader_exceptions_{0};
  std::atomic<std::uint64_t> dequeue_order_{0};
  std::vector<std::size_t>   writer_counts_;
  std::vector<std::vector<std::pair<std::uint64_t, std::uint64_t>>>
      reader_buckets_;
};

bool printAndJudge(const Results& r) {
  std::cout << "\n=== Test Results ===\n"
            << "Duration:           " << r.duration.count() << "ms\n"
            << "Items written:      " << r.items_written << "\n"
            << "Items read:         " << r.items_read << "\n"
            << "Writer exceptions:  " << r.writer_exceptions << "\n"
            << "Reader exceptions:  " << r.reader_exceptions << "\n"
            << "Final container sz: " << r.final_size << "\n"
            << "No duplicates:      " << (r.no_duplicates ? "yes" : "NO") << "\n"
            << "Per-producer FIFO:  " << (r.fifo_per_producer ? "yes" : "NO") << "\n";

  bool ok = true;
  if (r.items_written != r.items_read) {
    std::cerr << "FAIL: items_written != items_read\n"; ok = false;
  }
  if (r.final_size != 0) {
    std::cerr << "FAIL: container not empty after drain\n"; ok = false;
  }
  if (!r.no_duplicates) {
    std::cerr << "FAIL: duplicate items observed\n"; ok = false;
  }
  if (!r.fifo_per_producer) {
    std::cerr << "FAIL: per-producer FIFO ordering violated\n"; ok = false;
  }
  if (r.writer_exceptions != 0 || r.reader_exceptions != 0) {
    std::cerr << "FAIL: unexpected exceptions in worker threads\n"; ok = false;
  }

  std::cout << (ok ? "\nTest PASSED\n" : "\nTest FAILED\n");
  return ok;
}

}  // namespace

int main() {
  try {
    StressTest test{cfg::kQueueCapacity};
    const auto results = test.run();
    return printAndJudge(results) ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return EXIT_FAILURE;
  } catch (...) {
    std::cerr << "Unknown fatal error\n";
    return EXIT_FAILURE;
  }
}

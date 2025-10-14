#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <tsc/random_generator.hpp>
#include <tsc/thread_safe_container.hpp>
#include <vector>

namespace config {
constexpr size_t kWriterThreads = 19;
constexpr size_t kReaderThreads = 19;
constexpr auto kTestDuration = std::chrono::seconds{3};
constexpr size_t kWriteAttempts = 9;
constexpr int kWriteDelayMin = 100;
constexpr int kWriteDelayMax = 200;
constexpr int kReadDelayMin = 200;
constexpr int kReadDelayMax = 300;
constexpr size_t kQueueCapacity = 70;
}  // namespace config

class TestRunner {
 public:
  explicit TestRunner(size_t capacity)
      : container_{capacity}, keep_running_{true} {}

  struct TestResults {
    std::chrono::milliseconds duration;
    size_t writer_exceptions{0};
    size_t reader_exceptions{0};
    size_t items_written{0};
    size_t items_read{0};
    size_t final_size{0};
  };

  TestResults runTest() {
    std::cout << "=== ThreadSafeContainer Integration Test ===\n";
    std::cout << "Configuration:\n";
    std::cout << "  Writers: " << config::kWriterThreads << "\n";
    std::cout << "  Readers: " << config::kReaderThreads << "\n";
    std::cout << "  Duration: " << config::kTestDuration.count() << "s\n";
    std::cout << "  Queue capacity: " << config::kQueueCapacity << "\n\n";

    auto start_time = std::chrono::steady_clock::now();

    // Launch writer threads
    std::vector<std::future<size_t>> writer_futures;
    writer_futures.reserve(config::kWriterThreads);
    for (size_t i = 0; i < config::kWriterThreads; ++i) {
      writer_futures.emplace_back(std::async(
          std::launch::async, &TestRunner::writerTask, this,
          tsc::random::uniform(config::kWriteDelayMin, config::kWriteDelayMax),
          i));
    }

    // Launch reader threads
    std::vector<std::future<size_t>> reader_futures;
    reader_futures.reserve(config::kReaderThreads);
    for (size_t i = 0; i < config::kReaderThreads; ++i) {
      reader_futures.emplace_back(std::async(
          std::launch::async, &TestRunner::readerTask, this,
          tsc::random::uniform(config::kReadDelayMin, config::kReadDelayMax)));
    }

    // Let threads run for specified duration
    std::this_thread::sleep_for(config::kTestDuration);

    std::cout << "Initiating shutdown...\n";
    keep_running_.store(false, std::memory_order_release);

    // Give readers a moment to see the flag before shutting down container
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    container_.shutdown();

    // Collect results
    TestResults results;

    std::cout << "Waiting for writer threads...\n";
    for (auto& future : writer_futures) {
      try {
        results.items_written += future.get();
      } catch (const std::exception& e) {
        ++results.writer_exceptions;
        std::cerr << "Writer exception: " << e.what() << '\n';
      }
    }

    std::cout << "Waiting for reader threads...\n";
    for (auto& future : reader_futures) {
      try {
        results.items_read += future.get();
      } catch (const std::exception& e) {
        ++results.reader_exceptions;
        std::cerr << "Reader exception: " << e.what() << '\n';
      }
    }

    auto end_time = std::chrono::steady_clock::now();
    results.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    // Final cleanup
    container_.clear();
    results.final_size = container_.size();

    return results;
  }

 private:
  size_t writerTask(int delay_ms, size_t thread_id) {
    size_t items_written = 0;
    try {
      for (size_t i = 0; i < config::kWriteAttempts; ++i) {
        if (!keep_running_.load(std::memory_order_acquire)) {
          break;
        }

        const int value = static_cast<int>(thread_id * 1000 + i);
        if (!container_.tryAdd(value)) {
          container_.waitAdd(value);
        }
        ++items_written;
        std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms});
      }
    } catch (const tsc::ShutdownException&) {
      // Expected when container is shut down
    }
    return items_written;
  }

  size_t readerTask(int delay_ms) {
    size_t items_read = 0;
    int item;

    try {
      while (keep_running_.load(std::memory_order_acquire)) {
        if (container_.tryRemove(item)) {
          ++items_read;
          std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms});
        } else {
          // Brief sleep to avoid busy waiting when container is empty
          std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
      }
    } catch (const tsc::ShutdownException&) {
      // Container was shut down while we were operating - this is normal
    }

    return items_read;
  }

  tsc::ThreadSafeContainer<int> container_;
  std::atomic<bool> keep_running_;
};

void printResults(const TestRunner::TestResults& results) {
  std::cout << "\n=== Test Results ===\n";
  std::cout << "Duration: " << results.duration.count() << "ms\n";
  std::cout << "Items written: " << results.items_written << "\n";
  std::cout << "Items read: " << results.items_read << "\n";
  std::cout << "Writer exceptions: " << results.writer_exceptions << "\n";
  std::cout << "Reader exceptions: " << results.reader_exceptions << "\n";
  std::cout << "Final container size: " << results.final_size << "\n";

  bool success = (results.final_size == 0);

  if (success) {
    std::cout << "\n✓ Test PASSED - Container properly cleaned up\n";
  } else {
    std::cerr << "\n✗ Test FAILED - Container not empty after cleanup\n";
    std::cerr << "  Remaining items: " << results.final_size << "\n";
  }
}

int main() {
  try {
    TestRunner test{config::kQueueCapacity};
    auto results = test.runTest();
    printResults(results);

    assert(results.final_size == 0 &&
           "Container should be empty after cleanup");

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Unknown fatal error occurred" << std::endl;
    return 1;
  }
}

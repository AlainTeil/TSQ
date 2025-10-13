#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "RandomGenerator.hpp"
#include "ThreadSafeContainer.hpp"

constexpr size_t NB_WRITER_THREADS{19u};
constexpr size_t NB_READER_THREADS{19u};
constexpr std::chrono::seconds SLEEP{3};
// Defines the number of attempts per writer thread.
constexpr size_t NB_WRITE_ATTEMPTS{9u};
// Define lower and upper bounds for the random sleep
// duration in the writers and the readers threads.
constexpr int WRITE_LOWER_BOUND{100};
constexpr int WRITE_UPPER_BOUND{200};
constexpr int READ_LOWER_BOUND{200};
constexpr int READ_UPPER_BOUND{300};
// Defines a maximum capacity for the queue.
constexpr size_t NB_ITEMS{70u};

std::mutex excMtx;
std::vector<std::exception_ptr> exc;

void writerFunc(TSC::ThreadSafeContainer<int> &mtq, int ms) {
  bool status{false};

  try {
    for (size_t i{}; i < NB_WRITE_ATTEMPTS; ++i) {
      status = mtq.tryAdd(ms);
      if (!status) {
        mtq.waitAdd(ms);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{ms});
    }
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock{excMtx};

    exc.push_back(std::current_exception());
  }
}

void readerFunc(TSC::ThreadSafeContainer<int> &mtq, int ms) {
  bool status{false};
  int item;

  try {
    while (!mtq.empty()) {
      status = mtq.tryRemove(item);
      if (!status) {
        mtq.waitRemove(item);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{ms});
    }
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock{excMtx};

    exc.push_back(std::current_exception());
  }
}

/**
 * @brief Entry point for testing the ThreadSafeContainer functionality.
 *
 * This function initializes a thread-safe container and launches multiple
 * writer and reader threads to perform concurrent operations on the container.
 * It handles exceptions thrown by threads and ensures all threads are properly
 * joined before exiting. After all operations, it clears the container and
 * asserts that it is empty.
 *
 * Steps performed:
 * 1. Initializes a ThreadSafeContainer with a predefined number of items.
 * 2. Launches NB_WRITER_THREADS writer threads, each performing write
 * operations with random bounds.
 * 3. Launches NB_READER_THREADS reader threads, each performing read operations
 * with random bounds.
 * 4. Allows threads to operate for a specified duration (SLEEP), then signals
 * shutdown to the container.
 * 5. Joins all writer and reader threads.
 * 6. Catches and reports any exceptions thrown during thread execution.
 * 7. Rethrows and reports exceptions captured from threads.
 * 8. Clears the container and asserts it is empty before exiting.
 *
 * @return int Returns 0 on successful execution.
 */
int main() {
  TSC::ThreadSafeContainer<int> mtq{NB_ITEMS};
  std::vector<std::thread> writerThreads, readerThreads;

  try {
    for (size_t i{}; i < NB_WRITER_THREADS; ++i) {
      writerThreads.push_back(
          std::thread(writerFunc, std::ref(mtq),
                      RND::pick(WRITE_LOWER_BOUND, WRITE_UPPER_BOUND)));
    }

    for (size_t i{}; i < NB_READER_THREADS; ++i) {
      readerThreads.push_back(
          std::thread(readerFunc, std::ref(mtq),
                      RND::pick(READ_LOWER_BOUND, READ_UPPER_BOUND)));
    }

    std::this_thread::sleep_for(SLEEP);
    mtq.shutdown();

    for (auto &t : writerThreads) {
      t.join();
    }
    for (auto &t : readerThreads) {
      t.join();
    }
  } catch (const std::bad_alloc &e) {
    std::cerr << "bad_alloc caught in main: " << e.what() << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "exception caught in main: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "default exception caught in main" << std::endl;
  }

  for (auto &e : exc) {
    try {
      if (e != nullptr) {
        std::rethrow_exception(e);
      }
    } catch (const std::exception &e) {
      std::cerr << "thread exited with exception : " << e.what() << std::endl;
    }
  }

  mtq.clear();
  assert(mtq.size() == 0);

  return 0;
}

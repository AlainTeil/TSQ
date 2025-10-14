#pragma once

#include <condition_variable>
#include <exception>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>

namespace tsc {

/**
 * @brief Exception thrown when operations are attempted on a shutdown
 * container.
 */
class ShutdownException : public std::runtime_error {
 public:
  explicit ShutdownException(const std::string& message)
      : std::runtime_error("ThreadSafeContainer shutdown: " + message) {}
};

/**
 * @brief A thread-safe, bounded FIFO container supporting concurrent access.
 *
 * This class provides synchronized methods for adding and removing elements,
 * supporting both blocking and non-blocking operations.
 *
 * @tparam T The type of elements stored in the container.
 */
template <typename T>
class ThreadSafeContainer {
 public:
  using size_type = typename std::queue<T>::size_type;

  /**
   * @brief Constructs a container with the specified capacity.
   * @param capacity Maximum number of elements the container can hold.
   */
  explicit ThreadSafeContainer(size_type capacity);

  /**
   * @brief Destructor. Automatically shuts down and clears the container.
   */
  ~ThreadSafeContainer();

  // Disable copy and move operations
  ThreadSafeContainer(const ThreadSafeContainer&) = delete;
  ThreadSafeContainer& operator=(const ThreadSafeContainer&) = delete;
  ThreadSafeContainer(ThreadSafeContainer&&) = delete;
  ThreadSafeContainer& operator=(ThreadSafeContainer&&) = delete;

  /**
   * @brief Attempts to add an item without blocking.
   * @param item The item to add.
   * @return true if the item was added, false if the container is full.
   * @throws ShutdownException if the container is shut down.
   */
  bool tryAdd(const T& item);

  /**
   * @brief Adds an item, blocking if the container is full.
   * @param item The item to add.
   * @throws ShutdownException if the container is shut down.
   */
  void waitAdd(const T& item);

  /**
   * @brief Attempts to remove an item without blocking.
   * @param item Reference to store the removed item.
   * @return true if an item was removed, false if the container is empty.
   * @throws ShutdownException if the container is shut down.
   */
  bool tryRemove(T& item);

  /**
   * @brief Removes an item, blocking if the container is empty.
   * @param item Reference to store the removed item.
   * @throws ShutdownException if the container is shut down.
   */
  void waitRemove(T& item);

  /**
   * @brief Shuts down the container, unblocking all waiting threads.
   */
  void shutdown();

  /**
   * @brief Removes all items from the container (only works after shutdown).
   */
  void clear();

  /**
   * @brief Returns the current number of items.
   */
  size_type size() const;

  /**
   * @brief Checks if the container is empty.
   */
  bool empty() const;

  /**
   * @brief Checks if the container is full.
   */
  bool full() const;

  /**
   * @brief Checks if the container is active (not shut down).
   */
  bool isActive() const;

 private:
  mutable std::mutex mutex_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
  const size_type max_size_;
  std::queue<T> queue_;
  bool is_active_;
};

// Template implementation
template <typename T>
ThreadSafeContainer<T>::ThreadSafeContainer(size_type capacity)
    : max_size_{capacity}, is_active_{true} {}

template <typename T>
ThreadSafeContainer<T>::~ThreadSafeContainer() {
  shutdown();
  clear();
}

template <typename T>
bool ThreadSafeContainer<T>::tryAdd(const T& item) {
  std::lock_guard<std::mutex> lock{mutex_};

  if (!is_active_) {
    throw ShutdownException("cannot add to shutdown container");
  }

  if (queue_.size() == max_size_) {
    return false;
  }

  queue_.push(item);
  if (queue_.size() == 1) {
    not_empty_.notify_one();
  }
  return true;
}

template <typename T>
void ThreadSafeContainer<T>::waitAdd(const T& item) {
  std::unique_lock<std::mutex> lock{mutex_};

  not_full_.wait(lock,
                 [this] { return queue_.size() < max_size_ || !is_active_; });

  if (!is_active_) {
    throw ShutdownException("cannot add to shutdown container");
  }

  queue_.push(item);
  if (queue_.size() == 1) {
    not_empty_.notify_one();
  }
}

template <typename T>
bool ThreadSafeContainer<T>::tryRemove(T& item) {
  std::lock_guard<std::mutex> lock{mutex_};

  if (!is_active_) {
    throw ShutdownException("cannot remove from shutdown container");
  }

  if (queue_.empty()) {
    return false;
  }

  item = queue_.front();
  queue_.pop();
  if (queue_.size() == max_size_ - 1) {
    not_full_.notify_one();
  }
  return true;
}

template <typename T>
void ThreadSafeContainer<T>::waitRemove(T& item) {
  std::unique_lock<std::mutex> lock{mutex_};

  not_empty_.wait(lock, [this] { return !queue_.empty() || !is_active_; });

  if (!is_active_) {
    throw ShutdownException("cannot remove from shutdown container");
  }

  item = queue_.front();
  queue_.pop();
  if (queue_.size() == max_size_ - 1) {
    not_full_.notify_one();
  }
}

template <typename T>
void ThreadSafeContainer<T>::shutdown() {
  std::lock_guard<std::mutex> lock{mutex_};
  is_active_ = false;
  not_empty_.notify_all();
  not_full_.notify_all();
}

template <typename T>
void ThreadSafeContainer<T>::clear() {
  std::lock_guard<std::mutex> lock{mutex_};
  if (!is_active_) {
    while (!queue_.empty()) {
      queue_.pop();
    }
  }
}

template <typename T>
typename ThreadSafeContainer<T>::size_type ThreadSafeContainer<T>::size()
    const {
  std::lock_guard<std::mutex> lock{mutex_};
  return queue_.size();
}

template <typename T>
bool ThreadSafeContainer<T>::empty() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return queue_.empty();
}

template <typename T>
bool ThreadSafeContainer<T>::full() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return queue_.size() == max_size_;
}

template <typename T>
bool ThreadSafeContainer<T>::isActive() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return is_active_;
}

}  // namespace tsc

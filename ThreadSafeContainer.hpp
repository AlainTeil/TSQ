#pragma once

#include <condition_variable>
#include <exception>
#include <mutex>
#include <queue>
#include <string>

/**
 * @namespace TSC
 * @brief Contains thread-safe container utilities and related exceptions.
 */

/**
 * @class ShutdownException
 * @brief Exception thrown when operations are attempted on a shutdown
 * container.
 *
 * Inherits from std::exception and provides an error message describing the
 * shutdown event.
 */

/**
 * @class ThreadSafeContainer
 * @brief A thread-safe, bounded FIFO container supporting concurrent access.
 *
 * This class provides synchronized methods for adding and removing elements,
 * supporting both blocking and non-blocking operations. It uses a mutex and
 * condition variables to ensure thread safety and to manage capacity
 * constraints.
 *
 * @tparam T The type of elements stored in the container.
 *
 * @note Copy and move operations are disabled to prevent accidental sharing of
 * internal state.
 *
 * @threadsafe
 *
 * @section Methods
 * - ThreadSafeContainer(size_type capacity): Constructs a container with the
 * specified capacity.
 * - ~ThreadSafeContainer(): Destructor.
 * - bool tryAdd(const T &item): Attempts to add an item without blocking.
 * - void waitAdd(const T &item): Adds an item, blocking if the container is
 * full.
 * - bool tryRemove(T &item): Attempts to remove an item without blocking.
 * - void waitRemove(T &item): Removes an item, blocking if the container is
 * empty.
 * - void shutdown(): Shuts down the container, unblocking all waiting threads.
 * - void clear(): Removes all items from the container.
 * - size_type size() const: Returns the current number of items.
 * - bool empty() const: Checks if the container is empty.
 * - bool full() const: Checks if the container is full.
 */
namespace TSC {
class ShutdownException : public std::exception {
 public:
  ShutdownException(const std::string &message) : message{message} {}
  virtual const char *what() const noexcept override { return message.c_str(); }

 private:
  std::string message;
};

template <typename T>
class ThreadSafeContainer {
 private:
  mutable std::mutex mtx;
  std::condition_variable notFull;
  std::condition_variable notEmpty;
  typename std::queue<T>::size_type maxSize;
  std::queue<T> fifo;
  bool inUse;

 public:
  explicit ThreadSafeContainer(typename std::queue<T>::size_type capacity);

  virtual ~ThreadSafeContainer();

  ThreadSafeContainer(const ThreadSafeContainer<T> &src) = delete;

  ThreadSafeContainer<T> &operator=(const ThreadSafeContainer<T> &rhs) = delete;

  ThreadSafeContainer(ThreadSafeContainer<T> &&src) = delete;

  ThreadSafeContainer<T> &operator=(ThreadSafeContainer<T> &&rhs) = delete;

  bool tryAdd(const T &item);

  void waitAdd(const T &item);

  bool tryRemove(T &item);

  void waitRemove(T &item);

  void shutdown();

  void clear();

  typename std::queue<T>::size_type size() const;

  bool empty() const;

  bool full() const;
};
}  // namespace TSC

// Implementation is included here to allow template definitions in a separate
// file. This is required because template implementations must be visible to
// translation units that use them.
#include "ThreadSafeContainerPrivate.hpp"

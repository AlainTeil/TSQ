#pragma once

/**
 * @brief ThreadSafeContainer is a thread-safe, bounded FIFO queue.
 *
 * This container provides synchronized access for multiple producer and
 * consumer threads. It supports blocking and non-blocking operations for adding
 * and removing elements, as well as shutdown and clear operations for safe
 * resource management.
 *
 * @tparam T The type of elements stored in the container.
 *
 * @note All methods are thread-safe unless otherwise specified.
 *
 * @section Usage
 * - Use tryAdd() and tryRemove() for non-blocking operations.
 * - Use waitAdd() and waitRemove() for blocking operations.
 * - Call shutdown() to prevent further operations and unblock waiting threads.
 * - Call clear() to remove all elements after shutdown.
 *
 * @section Exception
 * Throws ShutdownException if operations are attempted after shutdown.
 */
namespace TSC {
template <typename T>
ThreadSafeContainer<T>::ThreadSafeContainer(
    typename std::queue<T>::size_type capacity)
    : maxSize{capacity}, inUse{true} {}

template <typename T>
ThreadSafeContainer<T>::~ThreadSafeContainer() {
  shutdown();
  clear();
}

// The tryAdd method returns true if tryAdd succeeds
// and false if tryAdd fails.
template <typename T>
bool ThreadSafeContainer<T>::tryAdd(const T &item) {
  std::lock_guard<std::mutex> lock{mtx};

  if (!inUse) {
    throw ShutdownException("shutdown");
  }

  if (fifo.size() == maxSize) {
    return false;
  } else {
    fifo.push(item);
    // We signal to potential readers in case
    // the queue was previously empty.
    if (fifo.size() == 1) {
      notEmpty.notify_all();
    }
    return true;
  }
}

template <typename T>
void ThreadSafeContainer<T>::waitAdd(const T &item) {
  std::unique_lock<std::mutex> lock{mtx};

  // Waits using a condition variable until the queue
  // is no longer full.
  notFull.wait(lock, [this] { return (fifo.size() < maxSize) || !inUse; });

  // This check is to ensure that if shutdown() was called while the queue was
  // full, any writers blocked on notFull are unblocked. This is a safeguard in
  // case shutdown is called while the queue is full and threads are waiting.
  if ((fifo.size() == maxSize) && !inUse) {
    notFull.notify_all();
  }

  if (!inUse) {
    throw ShutdownException("shutdown");
  }

  fifo.push(item);
  // We signal to potential readers in case
  // the queue was previously empty.
  if (fifo.size() == 1) {
    notEmpty.notify_all();
  }
}

// The tryRemove method returns true if tryRemove succeeds
// and false if tryRemove fails.
template <typename T>
bool ThreadSafeContainer<T>::tryRemove(T &item) {
  std::lock_guard<std::mutex> lock{mtx};

  if (!inUse) {
    throw ShutdownException("shutdown");
  }

  if (fifo.empty()) {
    return false;
  } else {
    item = fifo.front();
    fifo.pop();
    // We signal to potential writers in case
    // the queue was previously full.
    if (fifo.size() == (maxSize - 1)) {
      notFull.notify_all();
    }
    return true;
  }
}

template <typename T>
void ThreadSafeContainer<T>::waitRemove(T &item) {
  std::unique_lock<std::mutex> lock{mtx};

  // Waits using a condition variable until the queue
  // is no longer empty.
  notEmpty.wait(lock, [this] { return !fifo.empty() || !inUse; });

  // If the queue is empty and not in use, notify all to ensure any remaining
  // waiters are unblocked.
  if (fifo.empty() && !inUse) {
    notEmpty.notify_all();
  }

  if (!inUse) {
    throw ShutdownException("shutdown");
  }

  item = fifo.front();
  fifo.pop();
  // We signal to potential writers in case
  // the queue was previously full.
  if (fifo.size() == (maxSize - 1)) {
    notFull.notify_all();
  }
}

// The shutdown method prevents producer threads to
// add data to the queue, and prevents consumer
// threads to remove data from the queue.
template <typename T>
void ThreadSafeContainer<T>::shutdown() {
  std::lock_guard<std::mutex> lock{mtx};

  inUse = false;
  notEmpty.notify_all();
  notFull.notify_all();
}

// The clear method removes any elements present
// within the queue. This method will do nothing
// when called while the queue is still in use.
template <typename T>
void ThreadSafeContainer<T>::clear() {
  std::lock_guard<std::mutex> lock{mtx};

  if (!inUse) {
    while (!fifo.empty()) {
      fifo.pop();
    }
    notEmpty.notify_all();
    notFull.notify_all();
  }
}

template <typename T>
typename std::queue<T>::size_type ThreadSafeContainer<T>::size() const {
  std::lock_guard<std::mutex> lock{mtx};
  return fifo.size();
}

template <typename T>
bool ThreadSafeContainer<T>::empty() const {
  std::lock_guard<std::mutex> lock{mtx};
  return fifo.empty();
}

template <typename T>
bool ThreadSafeContainer<T>::full() const {
  std::lock_guard<std::mutex> lock{mtx};
  return fifo.size() == maxSize;
}
}  // namespace TSC

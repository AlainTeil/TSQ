#pragma once

// =============================================================================
// tsc::ThreadSafeContainer
//
// A header-only, thread-safe, bounded FIFO container for C++17 (and later).
//
// Design summary
// --------------
//   * Single std::mutex serializes all access.
//   * Two condition variables coordinate blocking:
//       - not_full_  : producers wait here when the queue is full
//       - not_empty_ : consumers wait here when the queue is empty
//   * notify_one() is issued after every successful push and pop, but only
//     when at least one waiter is actually parked on the relevant condition
//     variable (tracked via producers_waiting_ / consumers_waiting_, both
//     mutated under mutex_). This avoids the syscall overhead of an
//     unconditional notify in the common no-contention case while still
//     preserving the lost-wakeup fix from audit finding F1.
//   * Shutdown is "drain-on-close":
//       - Producers (tryAdd / waitAdd / emplaceAdd) refuse new work as soon
//         as the container is shut down (throw ShutdownException, or return
//         false / Closed status).
//       - Consumers (tryRemove / waitRemove) continue to drain remaining
//         items after shutdown. They only signal "closed" once the queue is
//         empty AND the container is inactive.
//
// Lifetime / destructor contract
// ------------------------------
// The destructor calls shutdown() which wakes all blocked threads, then
// clear()s any remaining items. It does NOT join callers. The caller MUST
// ensure no thread is executing any member function on the container at the
// time of destruction; otherwise behavior is undefined (the mutex / condvars
// would be destroyed while in use). Typical patterns:
//   * Wrap in std::shared_ptr held by all worker threads.
//   * Or, join all worker threads BEFORE the container goes out of scope.
//
// Public API stability
// --------------------
// All previously documented members (tryAdd, waitAdd, tryRemove, waitRemove,
// shutdown, clear, size, empty, full, isActive) remain available with the
// same signatures and observable behavior, except for the deliberate
// shutdown-semantics fix described above. New convenience overloads
// (rvalue, emplace, optional-returning, timed) and a status-returning API
// have been added without removing or renaming anything.
// =============================================================================

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace tsc {

/**
 * @brief Status code returned by non-throwing variants of queue operations.
 */
enum class QueueStatus {
  Ok,      ///< Operation completed successfully.
  Empty,   ///< try*-style remove found the queue empty.
  Full,    ///< try*-style add found the queue full.
  Timeout, ///< Timed wait expired before the operation could complete.
  Closed,  ///< Operation refused because the container has been shut down.
};

/**
 * @brief Exception thrown when an operation is attempted on a shut-down
 *        container in a context where the operation cannot be satisfied.
 *
 * Thrown by:
 *   - Any add operation (tryAdd / waitAdd / tryEmplaceAdd / emplaceAdd) once
 *     shutdown() has been called.
 *   - Any remove operation once shutdown() has been called AND the queue has
 *     been fully drained.
 */
class ShutdownException : public std::runtime_error {
 public:
  explicit ShutdownException(const std::string& message)
      : std::runtime_error("ThreadSafeContainer shutdown: " + message) {}
};

/**
 * @brief A thread-safe, bounded FIFO container supporting concurrent access.
 *
 * @tparam T Element type. Must be at least move-constructible. Copy support
 *           is required only for the const-ref overloads of add operations
 *           and for the legacy out-parameter remove overloads.
 */
template <typename T>
class ThreadSafeContainer {
 public:
  static_assert(!std::is_reference_v<T>,
                "tsc::ThreadSafeContainer<T>: T must not be a reference type");
  static_assert(std::is_object_v<T>,
                "tsc::ThreadSafeContainer<T>: T must be an object type");
  static_assert(std::is_move_constructible_v<T>,
                "tsc::ThreadSafeContainer<T>: T must be move-constructible");
  static_assert(std::is_destructible_v<T>,
                "tsc::ThreadSafeContainer<T>: T must be destructible");

  using value_type = T;
  using size_type = typename std::queue<T>::size_type;

  // --------------------------------------------------------------------------
  // Construction / destruction
  // --------------------------------------------------------------------------

  /**
   * @brief Constructs a container with the specified capacity.
   * @param capacity Maximum number of elements the container can hold.
   *                 Must be greater than zero.
   * @throws std::invalid_argument if @p capacity is zero.
   */
  explicit ThreadSafeContainer(size_type capacity);

  /**
   * @brief Destructor. Shuts the container down, wakes blocked threads, and
   *        clears remaining items. See lifetime contract in the file header:
   *        it is the caller's responsibility to ensure no other thread is
   *        executing a member function on this object at the moment of
   *        destruction.
   */
  ~ThreadSafeContainer();

  ThreadSafeContainer(const ThreadSafeContainer&) = delete;
  ThreadSafeContainer& operator=(const ThreadSafeContainer&) = delete;
  ThreadSafeContainer(ThreadSafeContainer&&) = delete;
  ThreadSafeContainer& operator=(ThreadSafeContainer&&) = delete;

  // --------------------------------------------------------------------------
  // Add operations (producers)
  // --------------------------------------------------------------------------

  /**
   * @brief Attempts to add an item without blocking.
   * @return true on success, false if the container is full.
   * @throws ShutdownException if the container has been shut down.
   */
  [[nodiscard]] bool tryAdd(const T& item);
  [[nodiscard]] bool tryAdd(T&& item);

  /**
   * @brief Constructs an item in place if there is room; non-blocking.
   * @return true on success, false if the container is full.
   * @throws ShutdownException if the container has been shut down.
   */
  template <typename... Args>
  [[nodiscard]] bool tryEmplaceAdd(Args&&... args);

  /**
   * @brief Adds an item, blocking if the container is full.
   * @throws ShutdownException if the container is/becomes shut down before
   *         the item can be added.
   */
  void waitAdd(const T& item);
  void waitAdd(T&& item);

  /**
   * @brief Constructs an item in place, blocking if the container is full.
   * @throws ShutdownException if the container is/becomes shut down before
   *         the item can be added.
   */
  template <typename... Args>
  void emplaceAdd(Args&&... args);

  /**
   * @brief Adds an item, waiting up to @p timeout for space.
   * @return Ok on success, Full on timeout, Closed if shut down.
   *         Never throws (in contrast with waitAdd).
   */
  template <class Rep, class Period>
  [[nodiscard]] QueueStatus tryAddFor(const T& item,
                                      const std::chrono::duration<Rep, Period>& timeout);
  template <class Rep, class Period>
  [[nodiscard]] QueueStatus tryAddFor(T&& item,
                                      const std::chrono::duration<Rep, Period>& timeout);

  // --------------------------------------------------------------------------
  // Remove operations (consumers)
  //
  // Drain-on-close: after shutdown(), remove operations continue to succeed
  // while items remain. They only fail (return false / Empty / Closed, or
  // throw ShutdownException) once the queue is empty AND inactive.
  // --------------------------------------------------------------------------

  /**
   * @brief Attempts to remove an item without blocking.
   * @return true on success, false if the queue is empty.
   * @throws ShutdownException if the container has been shut down AND is
   *         empty.
   */
  [[nodiscard]] bool tryRemove(T& item);

  /**
   * @brief Optional-returning non-blocking remove. Never throws.
   * @return Engaged optional on success.
   *         Empty optional if the queue is empty (whether active or shut
   *         down).
   */
  [[nodiscard]] std::optional<T> tryRemove();

  /**
   * @brief Removes an item, blocking if the queue is empty. After shutdown,
   *        drains any remaining items.
   * @throws ShutdownException once the container has been shut down AND
   *         drained.
   */
  void waitRemove(T& item);

  /**
   * @brief Optional-returning blocking remove. Never throws.
   * @return Engaged optional after a successful remove.
   *         Empty optional once the container has been shut down AND drained.
   */
  [[nodiscard]] std::optional<T> waitRemove();

  /**
   * @brief Removes an item, waiting up to @p timeout. Never throws.
   * @return Ok on success, Timeout if the wait expired with the queue empty,
   *         Closed if the container is shut down AND drained.
   */
  template <class Rep, class Period>
  [[nodiscard]] QueueStatus tryRemoveFor(T& item,
                                         const std::chrono::duration<Rep, Period>& timeout);

  // --------------------------------------------------------------------------
  // Lifecycle / bulk
  // --------------------------------------------------------------------------

  /**
   * @brief Marks the container as shut down and wakes every blocked thread.
   *        Idempotent. Items already in the queue remain accessible to
   *        consumers (drain-on-close).
   */
  void shutdown();

  /**
   * @brief Removes all items from the container. Wakes blocked producers
   *        because space has become available. Works whether the container
   *        is active or shut down.
   */
  void clear();

  /**
   * @brief Removes all items from the container and returns them to the
   *        caller in FIFO order. Works whether active or shut down.
   *
   * Useful after shutdown() to extract any items that were not consumed.
   */
  [[nodiscard]] std::vector<T> drain();

  // --------------------------------------------------------------------------
  // Observers (snapshots; values are inherently racy in concurrent contexts)
  // --------------------------------------------------------------------------

  [[nodiscard]] size_type size() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] bool full() const;
  [[nodiscard]] bool isActive() const;

  /// Maximum capacity (immutable, lock-free).
  [[nodiscard]] size_type capacity() const noexcept { return max_size_; }

 private:
  // Predicates (must be called with mutex_ held).
  bool can_push_locked() const { return queue_.size() < max_size_ || !is_active_; }
  bool can_pop_locked()  const { return !queue_.empty() || !is_active_; }

  // Notification policy: notify one waiter only when at least one is
  // actually parked on the relevant condition variable. The waiter counters
  // are mutated exclusively under mutex_, which is also held while the
  // notification decision is made -- this preserves the lost-wakeup fix
  // (F1) while avoiding a syscall when no thread is waiting. Worst case is
  // still a single spurious wakeup if a waiter has decremented its count
  // after returning from wait but has not yet released the lock; harmless.
  void notify_after_push() {
    if (consumers_waiting_ > 0) not_empty_.notify_one();
  }
  void notify_after_pop() {
    if (producers_waiting_ > 0) not_full_.notify_one();
  }

  // RAII helper to keep a waiter counter accurate across wait() exits via
  // exceptions (e.g. predicate throwing) as well as normal returns.
  struct WaiterGuard {
    std::size_t& counter;
    explicit WaiterGuard(std::size_t& c) noexcept : counter(c) { ++counter; }
    ~WaiterGuard() { --counter; }
    WaiterGuard(const WaiterGuard&) = delete;
    WaiterGuard& operator=(const WaiterGuard&) = delete;
  };

  mutable std::mutex mutex_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
  const size_type max_size_;
  std::queue<T> queue_;
  bool is_active_;
  std::size_t producers_waiting_{0};
  std::size_t consumers_waiting_{0};
};

// =============================================================================
// Implementation
// =============================================================================

template <typename T>
ThreadSafeContainer<T>::ThreadSafeContainer(size_type capacity)
    : max_size_{capacity}, is_active_{true} {
  if (capacity == 0) {
    throw std::invalid_argument(
        "ThreadSafeContainer: capacity must be greater than zero");
  }
}

template <typename T>
ThreadSafeContainer<T>::~ThreadSafeContainer() {
  // Best-effort: wake any blocked threads. The caller is responsible for
  // ensuring those threads have exited before destruction completes.
  shutdown();
  clear();
}

// ---------- Add ---------------------------------------------------------------

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
  notify_after_push();
  return true;
}

template <typename T>
bool ThreadSafeContainer<T>::tryAdd(T&& item) {
  std::lock_guard<std::mutex> lock{mutex_};
  if (!is_active_) {
    throw ShutdownException("cannot add to shutdown container");
  }
  if (queue_.size() == max_size_) {
    return false;
  }
  queue_.push(std::move(item));
  notify_after_push();
  return true;
}

template <typename T>
template <typename... Args>
bool ThreadSafeContainer<T>::tryEmplaceAdd(Args&&... args) {
  std::lock_guard<std::mutex> lock{mutex_};
  if (!is_active_) {
    throw ShutdownException("cannot add to shutdown container");
  }
  if (queue_.size() == max_size_) {
    return false;
  }
  queue_.emplace(std::forward<Args>(args)...);
  notify_after_push();
  return true;
}

template <typename T>
void ThreadSafeContainer<T>::waitAdd(const T& item) {
  std::unique_lock<std::mutex> lock{mutex_};
  {
    WaiterGuard g{producers_waiting_};
    not_full_.wait(lock, [this] { return can_push_locked(); });
  }
  if (!is_active_) {
    throw ShutdownException("cannot add to shutdown container");
  }
  queue_.push(item);
  notify_after_push();
}

template <typename T>
void ThreadSafeContainer<T>::waitAdd(T&& item) {
  std::unique_lock<std::mutex> lock{mutex_};
  {
    WaiterGuard g{producers_waiting_};
    not_full_.wait(lock, [this] { return can_push_locked(); });
  }
  if (!is_active_) {
    throw ShutdownException("cannot add to shutdown container");
  }
  queue_.push(std::move(item));
  notify_after_push();
}

template <typename T>
template <typename... Args>
void ThreadSafeContainer<T>::emplaceAdd(Args&&... args) {
  std::unique_lock<std::mutex> lock{mutex_};
  {
    WaiterGuard g{producers_waiting_};
    not_full_.wait(lock, [this] { return can_push_locked(); });
  }
  if (!is_active_) {
    throw ShutdownException("cannot add to shutdown container");
  }
  queue_.emplace(std::forward<Args>(args)...);
  notify_after_push();
}

template <typename T>
template <class Rep, class Period>
QueueStatus ThreadSafeContainer<T>::tryAddFor(
    const T& item, const std::chrono::duration<Rep, Period>& timeout) {
  std::unique_lock<std::mutex> lock{mutex_};
  bool ready = false;
  {
    WaiterGuard g{producers_waiting_};
    ready = not_full_.wait_for(lock, timeout,
                               [this] { return can_push_locked(); });
  }
  if (!ready) {
    return QueueStatus::Full;
  }
  if (!is_active_) {
    return QueueStatus::Closed;
  }
  queue_.push(item);
  notify_after_push();
  return QueueStatus::Ok;
}

template <typename T>
template <class Rep, class Period>
QueueStatus ThreadSafeContainer<T>::tryAddFor(
    T&& item, const std::chrono::duration<Rep, Period>& timeout) {
  std::unique_lock<std::mutex> lock{mutex_};
  bool ready = false;
  {
    WaiterGuard g{producers_waiting_};
    ready = not_full_.wait_for(lock, timeout,
                               [this] { return can_push_locked(); });
  }
  if (!ready) {
    return QueueStatus::Full;
  }
  if (!is_active_) {
    return QueueStatus::Closed;
  }
  queue_.push(std::move(item));
  notify_after_push();
  return QueueStatus::Ok;
}

// ---------- Remove (drain-on-close) -------------------------------------------

template <typename T>
bool ThreadSafeContainer<T>::tryRemove(T& item) {
  std::lock_guard<std::mutex> lock{mutex_};
  if (queue_.empty()) {
    if (!is_active_) {
      throw ShutdownException("cannot remove from shutdown empty container");
    }
    return false;
  }
  item = std::move(queue_.front());
  queue_.pop();
  notify_after_pop();
  return true;
}

template <typename T>
std::optional<T> ThreadSafeContainer<T>::tryRemove() {
  std::lock_guard<std::mutex> lock{mutex_};
  if (queue_.empty()) {
    return std::nullopt;
  }
  std::optional<T> result{std::move(queue_.front())};
  queue_.pop();
  notify_after_pop();
  return result;
}

template <typename T>
void ThreadSafeContainer<T>::waitRemove(T& item) {
  std::unique_lock<std::mutex> lock{mutex_};
  {
    WaiterGuard g{consumers_waiting_};
    not_empty_.wait(lock, [this] { return can_pop_locked(); });
  }
  if (queue_.empty()) {
    // Implies !is_active_ by the predicate above.
    throw ShutdownException("cannot remove from shutdown empty container");
  }
  item = std::move(queue_.front());
  queue_.pop();
  notify_after_pop();
}

template <typename T>
std::optional<T> ThreadSafeContainer<T>::waitRemove() {
  std::unique_lock<std::mutex> lock{mutex_};
  {
    WaiterGuard g{consumers_waiting_};
    not_empty_.wait(lock, [this] { return can_pop_locked(); });
  }
  if (queue_.empty()) {
    return std::nullopt;
  }
  std::optional<T> result{std::move(queue_.front())};
  queue_.pop();
  notify_after_pop();
  return result;
}

template <typename T>
template <class Rep, class Period>
QueueStatus ThreadSafeContainer<T>::tryRemoveFor(
    T& item, const std::chrono::duration<Rep, Period>& timeout) {
  std::unique_lock<std::mutex> lock{mutex_};
  bool ready = false;
  {
    WaiterGuard g{consumers_waiting_};
    ready = not_empty_.wait_for(lock, timeout,
                                [this] { return can_pop_locked(); });
  }
  if (!ready) {
    return QueueStatus::Timeout;
  }
  if (queue_.empty()) {
    return QueueStatus::Closed;
  }
  item = std::move(queue_.front());
  queue_.pop();
  notify_after_pop();
  return QueueStatus::Ok;
}

// ---------- Lifecycle / bulk --------------------------------------------------

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
  if (queue_.empty()) {
    return;
  }
  std::queue<T> empty;
  queue_.swap(empty);
  // Space became available; wake any blocked producer (no-op if shut down).
  not_full_.notify_all();
}

template <typename T>
std::vector<T> ThreadSafeContainer<T>::drain() {
  std::lock_guard<std::mutex> lock{mutex_};
  std::vector<T> out;
  out.reserve(queue_.size());
  while (!queue_.empty()) {
    out.push_back(std::move(queue_.front()));
    queue_.pop();
  }
  not_full_.notify_all();
  return out;
}

// ---------- Observers ---------------------------------------------------------

template <typename T>
typename ThreadSafeContainer<T>::size_type
ThreadSafeContainer<T>::size() const {
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

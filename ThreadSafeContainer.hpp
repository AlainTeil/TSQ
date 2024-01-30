#pragma once

#include <condition_variable>
#include <exception>
#include <mutex>
#include <queue>
#include <string>

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

#include "ThreadSafeContainerPrivate.hpp"

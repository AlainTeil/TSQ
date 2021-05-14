#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>
#include <exception>
#include <string>

class ShutdownException: public std::exception {
    public:
        ShutdownException(const std::string& message)
            : message{message} {}
        virtual const char* what() const noexcept override {
            return message.c_str();
        }
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
        ThreadSafeContainer(typename std::queue<T>::size_type capacity);

        virtual ~ThreadSafeContainer();

        ThreadSafeContainer(const ThreadSafeContainer& src) = delete;

        ThreadSafeContainer<T>& operator=(const ThreadSafeContainer& rhs)
            = delete;

        bool tryAdd(const T& item);

        void waitAdd(const T& item);

        bool tryRemove(T& item);

        void waitRemove(T& item);

        void shutdown();

        void clear();

        typename std::queue<T>::size_type size() const;

        bool empty() const;

        bool full() const;
};

#include "ThreadSafeContainerPrivate.h"

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <exception>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cassert>
#include "RandomGenerator.h"

const int NB_WRITER_THREADS = 19;
const int NB_READER_THREADS = 19;
const int SLEEP = 3;
// Defines the number of attempts per writer thread.
const int NB_WRITE_ATTEMPTS = 9;
// Defines lower and upper bounds for the random sleep
// duration in the writers and the readers threads.
const int WRITE_LOWER_BOUND = 100;
const int WRITE_UPPER_BOUND = 200;
const int READ_LOWER_BOUND = 200;
const int READ_UPPER_BOUND = 300;
// Defines a maximum capacity for the queue.
const unsigned int NB_ITEMS = 70;

std::mutex excMtx;
std::vector<std::exception_ptr> exc;

class ShutdownException: public std::exception {
    virtual const char* what() const noexcept {
        return "shutdown exception";
    }
} shutExc;

template <typename T>
class ThreadSafeContainer {
    private:
        mutable std::mutex mtx;
        std::condition_variable notFull;
        std::condition_variable notEmpty;
        typename std::queue<T>::size_type maxSize;
        std::queue<T> fifo;
        bool inUse;

        void throwOnShutdown();
    public:
        ThreadSafeContainer(typename std::queue<T>::size_type capacity);

        virtual ~ThreadSafeContainer();

        ThreadSafeContainer(const ThreadSafeContainer& src) = delete;

        ThreadSafeContainer<T>& operator=(const ThreadSafeContainer& rhs)
            = delete;

        bool add(const T& item);

        bool remove(T& item);

        void shutdown();

        void clear();

        typename std::queue<T>::size_type size() const;
};

template <typename T>
void ThreadSafeContainer<T>::throwOnShutdown() {
    std::unique_lock<std::mutex> lock{mtx};

    if (!inUse) {
        throw shutExc;
    }
}

template <typename T>
ThreadSafeContainer<T>::ThreadSafeContainer(
        typename std::queue<T>::size_type capacity)
    : maxSize{capacity}, inUse{true} {}

template <typename T>
ThreadSafeContainer<T>::~ThreadSafeContainer() {}

// The add method returns true if add succeeds
// and false if add fails.
template <typename T>
bool ThreadSafeContainer<T>::add(const T& item) {
    throwOnShutdown();

    bool status;
    std::unique_lock<std::mutex> lock{mtx};

    status = false;
    // Waits using a condition variable until the queue
    // is no longer full.
    notFull.wait(lock,
            [this]{return !((fifo.size() == maxSize) && inUse);});

    if ((fifo.size() == maxSize) && !inUse) {
        // Even if the queue is not in use, we need to
        // signal to potential writers blocked on
        // a full queue.
        notFull.notify_all();
    }
    if (inUse) {
        fifo.push(item);
        // We signal to potential readers in case
        // the queue was previously empty.
        if (fifo.size() == 1) {
            notEmpty.notify_all();
        }
        status = true;
    }
    return status;
}

// The remove method returns true if the value gathered
// from the queue is valid. Otherwise, it returns false.
template <typename T>
bool ThreadSafeContainer<T>::remove(T& item) {
    throwOnShutdown();

    bool status;
    std::unique_lock<std::mutex> lock{mtx};

    status = false;
    // Waits using a condition variable until the queue
    // is no longer empty.
    notEmpty.wait(lock, [this]{return !(fifo.empty() && inUse);});

    if (fifo.empty() && !inUse) {
        // Even if the queue is not in use, we need to
        // signal to potential readers blocked on
        // an empty queue.
        notEmpty.notify_all();
    }
    if (inUse) {
        item = fifo.front();
        fifo.pop();
        // We signal to potential writers in case
        // the queue was previously full.
        if (fifo.size() == (maxSize - 1)) {
            notFull.notify_all();
        }
        status = true;
    }
    return status;
}

// The shutdown method prevents producer threads to
// add data to the queue, and prevents consumer
// threads to remove data from the queue.
template <typename T>
void ThreadSafeContainer<T>::shutdown() {
    std::unique_lock<std::mutex> lock{mtx};

    inUse = false;
    notEmpty.notify_all();
    notFull.notify_all();
}

// The clear method removes any elements present
// within the queue. This method will do nothing
// when called while the queue is still in use.
template <typename T>
void ThreadSafeContainer<T>::clear() {
    std::unique_lock<std::mutex> lock{mtx};

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
    typename std::queue<T>::size_type size = fifo.size();

    return size;
}

void writerFunc(ThreadSafeContainer<int>& mtq, int ms) {
    bool status{false};

    try {
        for (int i = 1; i <= NB_WRITE_ATTEMPTS; ++i) {
            status = mtq.add(ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock{excMtx};
        exc.push_back(std::current_exception());
    }
}

void readerFunc(ThreadSafeContainer<int>& mtq, int ms) {
    bool status{false};
    int item;

    try {
        do {
            status = mtq.remove(item);
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        } while (status);
    }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock{excMtx};
        exc.push_back(std::current_exception());
    }
}

int main(int argc, char* argv[]) {
    ThreadSafeContainer<int> mtq{NB_ITEMS};
    std::vector<std::thread> writerThreads, readerThreads;

    randomize();
    exc.clear();

    for (int i = 0; i < NB_WRITER_THREADS; ++i) {
        writerThreads.push_back(std::thread(writerFunc, std::ref(mtq),
                    pick(WRITE_LOWER_BOUND, WRITE_UPPER_BOUND)));
    }

    for (int i = 0; i < NB_READER_THREADS; ++i) {
        readerThreads.push_back(std::thread(readerFunc, std::ref(mtq),
                    pick(READ_LOWER_BOUND, READ_UPPER_BOUND)));
    }

    std::this_thread::sleep_for(std::chrono::seconds(SLEEP));
    mtq.shutdown();

    for (auto &t : writerThreads) {
        t.join();
    }
    for (auto &t : readerThreads) {
        t.join();
    }

    for (auto &e : exc) {
        try {
            if (e != nullptr) {
                std::rethrow_exception(e);
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Thread exited with exception : " <<
                e.what() << std::endl;
        }
    }

    mtq.clear();
    assert(mtq.size() == 0);
    return 0;
}

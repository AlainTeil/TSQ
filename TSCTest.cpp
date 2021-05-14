#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>
#include "ThreadSafeContainer.h"
#include "RandomGenerator.h"

const int NB_WRITER_THREADS = 19;
const int NB_READER_THREADS = 19;
const int SLEEP = 3;
// Defines the number of attempts per writer thread.
const int NB_WRITE_ATTEMPTS = 9;
// Define lower and upper bounds for the random sleep
// duration in the writers and the readers threads.
const int WRITE_LOWER_BOUND = 100;
const int WRITE_UPPER_BOUND = 200;
const int READ_LOWER_BOUND = 200;
const int READ_UPPER_BOUND = 300;
// Defines a maximum capacity for the queue.
const unsigned int NB_ITEMS = 70;

std::mutex excMtx;
std::vector<std::exception_ptr> exc;

void writerFunc(ThreadSafeContainer<int>& mtq, int ms) {
    bool status{false};

    try {
        for (int i = 0; i < NB_WRITE_ATTEMPTS; ++i) {
            status = mtq.tryAdd(ms);
            if (!status) {
                mtq.waitAdd(ms);
            }
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
        while (!mtq.empty()) {
            status = mtq.tryRemove(item);
            if (!status) {
                mtq.waitRemove(item);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock{excMtx};

        exc.push_back(std::current_exception());
    }
}

int main() {
    ThreadSafeContainer<int> mtq{NB_ITEMS};
    std::vector<std::thread> writerThreads, readerThreads;

    randomize();
    exc.clear();

    try {
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
    }
    catch (const std::bad_alloc& e) {
        std::cerr << "bad_alloc caught in main: " <<
            e.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "exception caught in main: " <<
            e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "default exception caught in main" <<
            std::endl;
    }

    for (auto &e : exc) {
        try {
            if (e != nullptr) {
                std::rethrow_exception(e);
            }
        }
        catch (const std::exception& e) {
            std::cerr << "thread exited with exception : " <<
                e.what() << std::endl;
        }
    }

    mtq.clear();
    assert(mtq.size() == 0);

    return 0;
}

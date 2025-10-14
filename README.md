# ThreadSafeContainer - A C++17 Thread-Safe FIFO Queue

A modern C++17 implementation of a thread-safe, bounded FIFO (First-In-First-Out) queue container demonstrating advanced concurrency patterns and synchronization mechanisms. This project showcases proper use of mutexes, condition variables, and atomic operations for coordinating multiple producer and consumer threads.

## Overview

Based on the STL FIFO queue, this design provides thread-safety for multiple producer threads and multiple consumer threads through the utilization of a mutex and two condition variables. The first condition variable manages a full queue condition, while the second handles an empty queue condition.

This is a proof-of-concept illustrating concurrency features provided by modern C++, organized as a professional header-only library with a comprehensive testing application.

## Features

The `tsc::ThreadSafeContainer` class provides:

* **Template-based design** - Works with any copyable type
* **FIFO ordering** - Elements are extracted in first-in-first-out order
* **Bounded capacity** - Initialized with a maximum capacity to prevent unbounded growth
* **Non-blocking operations**:
  * `tryAdd()` - Attempts to insert an element, returns immediately with success/failure
  * `tryRemove()` - Attempts to extract an element, returns immediately with success/failure
* **Blocking operations**:
  * `waitAdd()` - Inserts an element, blocks if queue is full until space is available
  * `waitRemove()` - Extracts an element, blocks if queue is empty until an element is available
* **Graceful shutdown**:
  * `shutdown()` - Closes the queue and unblocks all waiting threads
  * Future operations throw `ShutdownException` after shutdown
  * `clear()` - Removes remaining elements after shutdown
* **Query methods**:
  * `size()` - Returns current number of elements
  * `empty()` - Checks if container is empty
  * `full()` - Checks if container is full
  * `isActive()` - Checks if container is active (not shut down)

## Project Structure

```
.
├── CMakeLists.txt             # Root build configuration
├── README.md                  # This file
├── .clang-format              # Code formatting rules
├── lib/                       # Thread-safe container library
│   ├── include/tsc/           # Public library headers
│   │   ├── thread_safe_container.hpp  # Main container implementation
│   │   └── random_generator.hpp       # Utility for random number generation
│   ├── cmake/                 # CMake configuration files
│   │   └── tscConfig.cmake.in
│   └── CMakeLists.txt         # Library build configuration
└── app/                       # Test application
    ├── src/
    │   └── main.cpp           # Integration test with 38 concurrent threads
    └── CMakeLists.txt         # Application build configuration
```

## Building the Project

### Prerequisites

* CMake 3.20 or higher
* C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
* POSIX threads library

### Quick Start

```bash
# Configure the build
cmake -S . -B build

# Build the project
cmake --build build

# Run the test application
./build/app/tsc_test

# Or run through CTest
cd build && ctest --output-on-failure
```

### Build Configurations

#### Debug Build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

#### Release Build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Testing with Sanitizers

This project includes support for ThreadSanitizer and AddressSanitizer to detect concurrency bugs and memory errors.

#### ThreadSanitizer (detect data races)
```bash
cmake -S . -B build -DENABLE_TSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/app/tsc_test
```

#### AddressSanitizer (detect memory errors)
```bash
cmake -S . -B build -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/app/tsc_test
```

### Code Formatting

```bash
cmake --build build --target format
```

## Test Application

The included test application ([`app/src/main.cpp`](app/src/main.cpp)) demonstrates a stress test with:

* **19 writer threads** producing items at 100-200ms intervals
* **19 reader threads** consuming items at 200-300ms intervals
* **Queue capacity of 70** items
* **3-second test duration**
* **Coordinated shutdown** ensuring all produced items are consumed

The test validates:
- Thread-safe concurrent access
- Proper blocking and notification behavior
- Graceful shutdown without deadlocks
- Complete consumption of all produced items

## Implementation Details

### Thread Safety Mechanisms

* **Mutex** (`std::mutex`) - Protects all access to the internal queue
* **Condition Variables** - Two condition variables coordinate blocking:
  * `not_full_` - Signals when space becomes available for producers
  * `not_empty_` - Signals when items become available for consumers
* **Atomic Operations** - Test application uses atomic flags for coordinated shutdown

### Performance Optimizations

* Uses `notify_one()` instead of `notify_all()` to avoid thundering herd
* Thread-local random number generators to avoid lock contention
* Efficient blocking with condition variables (no busy-waiting)
* Header-only library for potential inlining optimizations

### Exception Safety

* `ShutdownException` (derived from `std::runtime_error`) thrown when operations attempted after shutdown
* RAII compliance - destructor calls `shutdown()` and `clear()` automatically
* Exception-safe operation collection in test harness using `std::future`

## Installing the Library

### System-wide Installation
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

### User Installation
```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build
cmake --install build
```

### Using the Installed Library

After installation, use in your `CMakeLists.txt`:

```cmake
find_package(tsc REQUIRED)
target_link_libraries(your_target PRIVATE tsc::tsc)
```

## License

This is a proof-of-concept project for educational purposes demonstrating C++17 concurrency features.

## Contributing

This project follows Google C++ Style Guide (see [`.clang-format`](.clang-format)). Run `cmake --build build --target format` before submitting changes.

## Acknowledgments

This implementation demonstrates modern C++17 concurrency patterns including:
- `std::mutex` and `std::lock_guard`/`std::unique_lock`
- `std::condition_variable` for efficient thread coordination
- `std::atomic` with memory ordering semantics
- `std::async` and `std::future` for async task management
- RAII and exception-safe resource management

### Known Issues

#### ThreadSanitizer on Ubuntu 24.04

ThreadSanitizer has compatibility issues with Ubuntu 24.04 due to kernel memory mapping changes. If you encounter:

```
FATAL: ThreadSanitizer: unexpected memory mapping
```

**Workaround Options:**

1. **Use AddressSanitizer instead** (works on Ubuntu 24.04):
   ```bash
   cmake -S . -B build -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ./build/app/tsc_test
   ```

2. **Temporarily disable ASLR**:
   ```bash
   echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
   ./build/app/tsc_test
   echo 2 | sudo tee /proc/sys/kernel/randomize_va_space  # Re-enable after
   ```

3. **Upgrade to GCC 13+**:
   ```bash
   sudo apt-get install gcc-13 g++-13
   CC=gcc-13 CXX=g++-13 cmake -S . -B build -DENABLE_TSAN=ON
   ```

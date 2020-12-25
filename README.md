Based on the STL FIFO queue, this design provides thread-safety for multiple
producer threads and multiple consumer threads, through the utilization of a
mutex and two condition variables. The first condition variable takes care
of a full queue condition, the second condition variable takes care of an
empty queue condition. This is by no means a full-fledged production
container, but simply a proof-of-concept illustrating some of the
concurrency features provided by the C++11 language.

This ThreadSafeContainer class provides a thread-safe container conformant
to the following requirements:

  * This is a templated class.
  * Elements are extracted in a first in first out order.
  * Class instances are initialized with a maximum capacity.
  * A tryAdd method tries to insert an element to the queue and returns
    true in case the queue is not full or returns false otherwise.
  * A waitAdd method inserts an element to the queue and will block the caller
    in case the queue is full until space becomes available.
  * A tryRemove method tries to extract an element from the queue and returns
    true in case the queue is not empty or returns false otherwise.
  * A waitRemove method extracts an element from the queue and will block the
    caller in case the queue is empty until an element becomes available.
  * A shutdown method enables to close the queue and future calls to tryAdd,
    waitAdd, tryRemove, waitRemove will throw a ShutdownException.
  * A clear method enables to remove elements still present within the
    queue after a call to the shutdown method.

In order to test the ThreadSafeContainer class, it is possible to rely on
the ThreadSanitizer data race detector. To this end, an option not enabled
by default is defined within the CMakeLists.txt file. You can turn it on
by typing this at your shell prompt:

mkdir -p build
cd build
cmake -DENABLE_TSAN=ON ..
cmake --build .
cmake --build . --target test

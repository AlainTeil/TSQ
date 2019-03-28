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
  * An add method inserts an element to the queue and will block the caller
    in case the queue is full until space becomes available.
  * A remove method extracts an element from the queue and will block the
    caller in case the queue is empty until an element becomes available.
  * A shutdown method enables to close the queue and future calls to add
    or remove will throw a ShutdownException.
  * A clear method enables to remove elements still present within the
    queue after a call to the shutdown method.

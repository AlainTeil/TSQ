// Minimal usage example for tsc::ThreadSafeContainer.
//
// One producer thread pushes 10 messages and shuts down the queue. The
// consumer thread loops on waitRemove() until it returns std::nullopt,
// which signals "queue drained AND closed".

#include <iostream>
#include <string>
#include <thread>

#include <tsc/thread_safe_container.hpp>

int main() {
  tsc::ThreadSafeContainer<std::string> queue{8};

  std::thread producer([&] {
    for (int i = 0; i < 10; ++i) {
      queue.waitAdd("msg-" + std::to_string(i));
    }
    queue.shutdown();   // Consumers may still drain everything pushed above.
  });

  std::thread consumer([&] {
    while (auto msg = queue.waitRemove()) {
      std::cout << "received: " << *msg << '\n';
    }
    std::cout << "queue drained and closed\n";
  });

  producer.join();
  consumer.join();
  return 0;
}

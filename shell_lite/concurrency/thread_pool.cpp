#include "concurrency/thread_pool.hpp"
#include <iostream>

namespace shell_lite {
namespace concurrency {

ThreadPool::ThreadPool(size_t num_threads) : stop(false) {
  for (size_t i = 0; i < num_threads; ++i) {
    workers.emplace_back([this]() {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(this->queue_mutex);
          this->condition.wait(lock, [this]() {
            return this->stop || !this->tasks.empty();
          });
          if (this->stop && this->tasks.empty()) return;
          task = std::move(this->tasks.front());
          this->tasks.pop();
        }
        if (task) {
          try {
            task();
          } catch (const std::exception &e) {
            if (this->exception_handler_) {
              this->exception_handler_(e);
            } else {
              std::cerr << "Unhandled exception in worker thread: " << e.what() << std::endl;
            }
          }
        }
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    stop = true;
  }
  condition.notify_all();
  for (std::thread &worker : workers) {
    if (worker.joinable()) worker.join();
  }
}

void ThreadPool::enqueue(std::function<void()> task) {
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    if (stop) return;
    tasks.push(std::move(task));
  }
  condition.notify_one();
}

void ThreadPool::set_exception_handler(std::function<void(const std::exception&)> handler) {
    exception_handler_ = std::move(handler);
}

ThreadPool& get_global_thread_pool() {
  size_t hc = std::thread::hardware_concurrency();
  if (hc < 2) hc = 2;
  static ThreadPool s_pool(hc);
  return s_pool;
}

} // namespace concurrency
} // namespace shell_lite

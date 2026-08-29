#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

namespace shell_lite {
namespace concurrency {

class ThreadPool {
public:
    ThreadPool(size_t num_threads);
    ~ThreadPool();
    void enqueue(std::function<void()> task);
    void set_exception_handler(std::function<void(const std::exception&)> handler);
private:
    std::function<void(const std::exception&)> exception_handler_;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

ThreadPool& get_global_thread_pool();

} // namespace concurrency
} // namespace shell_lite

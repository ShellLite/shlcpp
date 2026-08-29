#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <string>
#include <chrono>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

namespace shell_lite {

class VM;

struct IOHandler {
    uint64_t fd;
    std::function<void()> on_read;
    std::function<void()> on_write;
};

struct Timer {
    std::chrono::steady_clock::time_point expires;
    int interval_ms;
    std::function<void()> callback;
};

class EventLoop {
public:
    static EventLoop& instance() {
        static EventLoop loop;
        return loop;
    }

    void add_handler(uint64_t fd, std::function<void()> on_read, std::function<void()> on_write = nullptr);
    void remove_handler(uint64_t fd);

    void add_timer(int delay_ms, std::function<void()> callback);
    void add_interval(int interval_ms, std::function<void()> callback);

    void init_ui();
    void set_ui_callback(std::function<void()> cb);
    void render_ui();
    bool has_ui() const;
    void set_server_active(bool active) { server_active_ = active; }

    void poll(int timeout_ms = 0);

    void run();
    void stop() { stop_ = true; }

    bool has_work() const;

private:
    EventLoop() : stop_(false), server_active_(false) {}
    std::vector<IOHandler> handlers_;
    mutable std::mutex handler_mutex_;
    std::vector<Timer> timers_;
    mutable std::mutex timer_mutex_;
    bool stop_;
    bool server_active_;
};

} // namespace shell_lite
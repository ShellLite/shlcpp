#include "event_loop.hpp"
#include "ui/ui_subsystem.hpp"
#include <algorithm>
#include <iostream>
#include <thread>

namespace shell_lite {

void EventLoop::init_ui() {
    ui::UISubsystem::instance().ensure_initialized();
}

void EventLoop::set_ui_callback(std::function<void()> cb) {
    ui::UISubsystem::instance().set_callback(std::move(cb));
}

void EventLoop::render_ui() {
    ui::UISubsystem::instance().render();
}

bool EventLoop::has_ui() const {
    return ui::UISubsystem::instance().is_active();
}

void EventLoop::add_handler(uint64_t fd, std::function<void()> on_read, std::function<void()> on_write) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handlers_.push_back({fd, std::move(on_read), std::move(on_write)});
}

void EventLoop::remove_handler(uint64_t fd) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(),
        [fd](const IOHandler& h) { return h.fd == fd; }), handlers_.end());
}

void EventLoop::add_timer(int delay_ms, std::function<void()> callback) {
    auto expires = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
    std::lock_guard<std::mutex> lock(timer_mutex_);
    timers_.push_back({expires, 0, std::move(callback)});
}

void EventLoop::add_interval(int interval_ms, std::function<void()> callback) {
    auto expires = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
    std::lock_guard<std::mutex> lock(timer_mutex_);
    timers_.push_back({expires, interval_ms, std::move(callback)});
}

void EventLoop::poll(int timeout_ms) {
    if (ui::UISubsystem::instance().is_active()) {
        if (!ui::UISubsystem::instance().poll_events()) {
            stop_ = true;
        }
        ui::UISubsystem::instance().render();
    }

    auto now = std::chrono::steady_clock::now();
    std::vector<Timer> expired;

    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        auto it = timers_.begin();
        while (it != timers_.end()) {
            if (now >= it->expires) {
                expired.push_back(*it);
                if (it->interval_ms > 0) {
                    it->expires = now + std::chrono::milliseconds(it->interval_ms);
                    ++it;
                } else {
                    it = timers_.erase(it);
                }
            } else {
                ++it;
            }
        }
    }

    for (auto& t : expired) t.callback();

    std::vector<IOHandler> handlers_copy;
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        handlers_copy = handlers_;
    }

    if (handlers_copy.empty()) {
        if (timeout_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        }
        return;
    }

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    uint64_t max_fd = 0;
    bool has_fds = false;
    for (const auto& h : handlers_copy) {
        if (h.on_read) { FD_SET(h.fd, &read_fds); has_fds = true; }
        if (h.on_write) { FD_SET(h.fd, &write_fds); has_fds = true; }
        if (h.fd > max_fd) max_fd = h.fd;
    }

    if (!has_fds) {
        if (timeout_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        }
        return;
    }

    timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int activity = select((int)max_fd + 1, &read_fds, &write_fds, nullptr, &timeout);

    if (activity > 0) {
        for (const auto& h : handlers_copy) {
            if (h.on_read && FD_ISSET(h.fd, &read_fds)) {
                h.on_read();
            }
            if (h.on_write && FD_ISSET(h.fd, &write_fds)) {
                h.on_write();
            }
        }
    }
}

bool EventLoop::has_work() const {
    std::scoped_lock lock(timer_mutex_, handler_mutex_);
    return !handlers_.empty() || !timers_.empty() || ui::UISubsystem::instance().is_active() || server_active_;
}

void EventLoop::run() {
    stop_ = false;
    while (!stop_ && has_work()) {
        poll(10);
    }
}

} // namespace shell_lite
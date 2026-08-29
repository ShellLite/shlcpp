#pragma once
#include <functional>
#include <memory>
#include <string>

namespace shell_lite {
namespace ui {

class UISubsystem {
public:
    static UISubsystem& instance();

    bool ensure_initialized();
    bool is_active() const;
    void set_callback(std::function<void()> cb);
    bool poll_events(); // returns false if user requested quit (SDL_QUIT)
    void render();
    void shutdown();

private:
    UISubsystem();
    ~UISubsystem();
    UISubsystem(const UISubsystem&) = delete;
    UISubsystem& operator=(const UISubsystem&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ui
} // namespace shell_lite

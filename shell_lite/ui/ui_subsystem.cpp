#include "ui/ui_subsystem.hpp"
#include <iostream>
#include <SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace shell_lite {
namespace ui {

struct UISubsystem::Impl {
    bool active = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::function<void()> callback;

    ~Impl() {
        shutdown();
    }

    bool init() {
        if (active) return true;

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            std::cerr << "Error: SDL_Init failed: " << SDL_GetError() << std::endl;
            return false;
        }

        SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        window = SDL_CreateWindow("shlcpp UI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
        if (!window) {
            std::cerr << "Error: SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return false;
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
        if (!renderer) {
            std::cerr << "Error: SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
            SDL_DestroyWindow(window);
            window = nullptr;
            SDL_Quit();
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);

        active = true;
        return true;
    }

    bool poll_events() {
        if (!active) return true;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                return false;
            }
        }
        return true;
    }

    void render() {
        if (!active) return;

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("shlcpp Layout", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

        if (callback) {
            callback();
        }

        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }

    void shutdown() {
        if (!active) return;

        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        if (renderer) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
        active = false;
    }
};

UISubsystem::UISubsystem() : impl_(std::make_unique<Impl>()) {}
UISubsystem::~UISubsystem() = default;

UISubsystem& UISubsystem::instance() {
    static UISubsystem s_instance;
    return s_instance;
}

bool UISubsystem::ensure_initialized() {
    return impl_->init();
}

bool UISubsystem::is_active() const {
    return impl_->active;
}

void UISubsystem::set_callback(std::function<void()> cb) {
    impl_->callback = std::move(cb);
}

bool UISubsystem::poll_events() {
    return impl_->poll_events();
}

void UISubsystem::render() {
    impl_->render();
}

void UISubsystem::shutdown() {
    impl_->shutdown();
}

} // namespace ui
} // namespace shell_lite

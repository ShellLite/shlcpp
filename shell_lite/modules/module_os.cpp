#include "../native_registry.hpp"
#include "clip.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <filesystem>
#include "pal/platform.hpp"

namespace shell_lite {

static bool automation_click_impl(double x, double y, std::string& err_out) {
  return pal::set_cursor_pos(x, y, err_out);
}

static bool automation_type_impl(const std::string& text, std::string& err_out) {
  return pal::send_text_input(text, err_out);
}

static bool automation_press_impl(const std::string& key, std::string& err_out) {
  return pal::send_key_input(key, err_out);
}

void register_stdlib_os(VM* vm) {
    NativeRegistry::bind(vm, "std_os_clear", []() -> void {
        std::cout << "\033[2J\033[H" << std::flush;
        pal::clear_console();
    });
    NativeRegistry::bind(vm, "std_os_getenv", [](std::string name) -> std::string {
        const char* val = std::getenv(name.c_str());
        return val ? std::string(val) : std::string("");
    });
    NativeRegistry::bind(vm, "std_os_getcwd", []() -> std::string {
        std::error_code ec;
        auto p = std::filesystem::current_path(ec);
        return ec ? std::string("") : p.string();
    });
    NativeRegistry::bind(vm, "std_os_name", []() -> std::string {
        return pal::get_os_name();
    });
    NativeRegistry::bind(vm, "std_os_platform", []() -> std::string {
        return pal::get_os_name();
    });
    NativeRegistry::bind(vm, "os_write_file", [](std::string filename, std::string content) -> bool {
        std::ofstream out(filename);
        out << content;
        out.close();
        return true;
    });
    NativeRegistry::bind(vm, "os_execute", [](std::string cmd) -> double {
        return pal::execute_process(cmd);
    });
    NativeRegistry::bind(vm, "clipboard_op", [](std::string op, std::optional<Value> content) -> std::string {
        if (op == "read" || op == "paste") {
            std::string text;
            if (clip::get_text(text)) {
                return text;
            }
            return "";
        } else if (op == "write" || op == "copy") {
            if (content.has_value()) {
                std::string text = content.value().to_string();
                clip::set_text(text);
            }
            return "";
        }
        return "";
    });
    NativeRegistry::bind(vm, "clipboard_copy", [](std::string text) -> void {
        clip::set_text(text);
    });
    NativeRegistry::bind(vm, "clipboard_paste", []() -> std::string {
        std::string text;
        if (clip::get_text(text)) return text;
        return "";
    });
    NativeRegistry::bind(vm, "automation_click", [vm](double x, double y) -> void {
        std::string err;
        if (!automation_click_impl(x, y, err)) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string(err));
        }
    });
    NativeRegistry::bind(vm, "automation_type", [vm](std::string text) -> void {
        std::string err;
        if (!automation_type_impl(text, err)) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string(err));
        }
    });
    NativeRegistry::bind(vm, "automation_press", [vm](std::string key) -> void {
        std::string err;
        if (!automation_press_impl(key, err)) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string(err));
        }
    });
    NativeRegistry::bind(vm, "automation_notify", [](std::string title, std::string msg) -> void {
        std::cout << "[Notification] " << title << ": " << msg << std::endl;
    });
    NativeRegistry::register_builtin(vm, "automation_call", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count < 1) return Value();
        std::string action = vm->peek(arg_count - 1).to_string();
        std::string err;

        if (action == "click") {
            if (arg_count != 3) return Value();
            double x = vm->peek(1).as_number();
            double y = vm->peek(0).as_number();
            if (!automation_click_impl(x, y, err)) {
                vm->has_error = true;
                vm->error_value = Value(vm->arena().allocate_string(err));
            }
        } else if (action == "press") {
            if (arg_count != 2) return Value();
            std::string key = vm->peek(0).to_string();
            if (!automation_press_impl(key, err)) {
                vm->has_error = true;
                vm->error_value = Value(vm->arena().allocate_string(err));
            }
        } else if (action == "type") {
            if (arg_count != 2) return Value();
            std::string text = vm->peek(0).to_string();
            if (!automation_type_impl(text, err)) {
                vm->has_error = true;
                vm->error_value = Value(vm->arena().allocate_string(err));
            }
        } else if (action == "notify") {
            if (arg_count != 3) return Value();
            std::string title = vm->peek(1).to_string();
            std::string msg = vm->peek(0).to_string();
            std::cout << "[Notification] " << title << ": " << msg << std::endl;
        }
        return Value();
    });
}

} // namespace shell_lite

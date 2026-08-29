#include "../native_registry.hpp"
#include "../event_loop.hpp"
#include "imgui.h"
#include <iostream>
#include <string>
#include <cstring>

namespace shell_lite {

static std::string escape_html_attr(const std::string& data) {
    std::string buffer;
    buffer.reserve(data.size());
    for (char c : data) {
        switch (c) {
            case '&':  buffer.append("&amp;");       break;
            case '\"': buffer.append("&quot;");      break;
            case '\'': buffer.append("&#39;");       break;
            case '<':  buffer.append("&lt;");        break;
            case '>':  buffer.append("&gt;");        break;
            default:   buffer.push_back(c);          break;
        }
    }
    return buffer;
}

void register_stdlib_ui(VM* vm) {
    NativeRegistry::register_builtin(vm, "std_ui_widget", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 3) return Value("");
        std::string tag = vm->peek(2).to_string();
        Value attrs_val = vm->peek(1);
        Value children_val = vm->peek(0);
        
        std::string out = "<" + escape_html_attr(tag);
        if (attrs_val.is_dict()) {
            ObjDict* dict = static_cast<ObjDict*>(attrs_val.get_obj());
            for (auto& pair : dict->elements) {
                out += " " + escape_html_attr(pair.first) + "=\"" + escape_html_attr(pair.second.to_string()) + "\"";
            }
        }
        
        if (tag == "meta" || tag == "link" || tag == "img" || tag == "br" || tag == "hr" || tag == "input") {
            out += ">";
            return Value(vm->arena().allocate_string(out));
        }
        out += ">";
        
        if (children_val.is_list()) {
            ObjList* list = static_cast<ObjList*>(children_val.get_obj());
            for (auto& child : list->elements) {
                out += child.to_string();
            }
        }
        
        out += "</" + escape_html_attr(tag) + ">";
        return Value(vm->arena().allocate_string(out));
    });

    NativeRegistry::register_builtin(vm, "std_ui_join", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 1) return Value("");
        Value list_val = vm->peek(0);
        if (!list_val.is_list()) return Value("");
        ObjList* list = static_cast<ObjList*>(list_val.get_obj());
        std::string out = "";
        for (auto& child : list->elements) {
            out += child.to_string() + "\n";
        }
        return Value(vm->arena().allocate_string(out));
    });

    NativeRegistry::register_builtin(vm, "std_ui_alert", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 1) return Value();
        std::cout << "[ALERT]: " << vm->peek(0).to_string() << std::endl;
        return Value();
    });

    NativeRegistry::register_builtin(vm, "std_ui_prompt", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 1) return Value();
        std::cout << "[PROMPT] " << vm->peek(0).to_string() << ": ";
        std::string res;
        std::getline(std::cin, res);
        return Value(vm->arena().allocate_string(res));
    });

    NativeRegistry::register_builtin(vm, "std_ui_confirm", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 1) return Value();
        std::cout << "[CONFIRM] " << vm->peek(0).to_string() << " (y/n): ";
        std::string res;
        std::getline(std::cin, res);
        return Value(res == "y" || res == "Y" || res == "yes");
    });

    NativeRegistry::register_builtin(vm, "ui_button", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 1) return Value(false);
        std::string label = vm->peek(0).to_string();
        if (!ImGui::GetCurrentContext()) return Value(false);
        bool clicked = ImGui::Button(label.c_str());
        return Value(clicked);
    });

    NativeRegistry::register_builtin(vm, "ui_text", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 1) return Value();
        std::string text = vm->peek(0).to_string();
        if (!ImGui::GetCurrentContext()) return Value();
        ImGui::TextUnformatted(text.c_str());
        return Value();
    });

    NativeRegistry::register_builtin(vm, "ui_input_text", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count < 1) return Value("");
        std::string label = vm->peek(arg_count - 1).to_string();
        std::string current = (arg_count >= 2) ? vm->peek(arg_count - 2).to_string() : "";
        if (!ImGui::GetCurrentContext()) return Value(vm->arena().allocate_string(current));
        static char buf[1024] = {0};
        strncpy(buf, current.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText(label.c_str(), buf, sizeof(buf))) {
            return Value(vm->arena().allocate_string(std::string(buf)));
        }
        return Value(vm->arena().allocate_string(current));
    });

    NativeRegistry::register_builtin(vm, "ui_window", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 2) return Value();
        std::string title = vm->peek(1).to_string();
        Value content_fn = vm->peek(0);
        if (!ImGui::GetCurrentContext()) {
            if (content_fn.is_callable()) {
                vm->call_value(content_fn, 0);
            }
            return Value();
        }
        if (ImGui::Begin(title.c_str())) {
            if (content_fn.is_callable()) {
                vm->call_value(content_fn, 0);
            }
            ImGui::End();
        }
        return Value();
    });

    NativeRegistry::register_builtin(vm, "ui_run_app", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 1) return Value();
        Value app_fn = vm->peek(0);
        EventLoop::instance().init_ui();
        EventLoop::instance().set_ui_callback([vm, app_fn]() {
            if (app_fn.is_callable()) {
                vm->call_value(app_fn, 0);
            }
        });
        EventLoop::instance().run();
        return Value();
    });
}

} // namespace shell_lite


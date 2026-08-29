#include "../native_registry.hpp"
#include "../event_loop.hpp"
#include <chrono>

namespace shell_lite {

void register_stdlib_nlp(VM* vm) {
    NativeRegistry::bind(vm, "std_nlp_add", [](Value arg0, Value arg1) -> Value {
        if (arg1.is_list()) {
            static_cast<ObjList*>(arg1.get_obj())->elements.push_back(arg0);
            return arg1;
        } else if (arg0.is_list()) {
            static_cast<ObjList*>(arg0.get_obj())->elements.push_back(arg1);
            return arg0;
        }
        throw std::runtime_error("nlp_add expects a list argument");
    });

    NativeRegistry::bind(vm, "std_nlp_remove", [](Value arg0, Value arg1) -> Value {
        Value target = arg1.is_list() ? arg1 : (arg0.is_list() ? arg0 : Value());
        Value item = (target == arg1) ? arg0 : arg1;
        if (target.is_list()) {
            auto& elements = static_cast<ObjList*>(target.get_obj())->elements;
            for (auto it = elements.begin(); it != elements.end(); ++it) {
                if (*it == item) {
                    elements.erase(it);
                    break;
                }
            }
            return target;
        }
        throw std::runtime_error("nlp_remove expects a list argument");
    });

    NativeRegistry::register_builtin(vm, "std_nlp_every", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 3) return Value();
        double interval = vm->peek(2).as_number();
        std::string unit = vm->peek(1).to_string();
        Value block = vm->peek(0);
        int ms = (unit == "seconds" || unit == "second") ? (int)(interval * 1000) : (int)(interval * 60000);
        EventLoop::instance().add_interval(ms, [vm, block]() {
            vm->enqueue_task([vm, block]() {
                if (vm->call_value(block, 0)) {
                    vm->run((int)vm->frames.size() - 1);
                }
            });
        });
        return Value();
    });

    NativeRegistry::register_builtin(vm, "std_nlp_after", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 3) return Value();
        double interval = vm->peek(2).as_number();
        std::string unit = vm->peek(1).to_string();
        Value block = vm->peek(0);
        int ms = (unit == "seconds" || unit == "second") ? (int)(interval * 1000) : (int)(interval * 60000);
        EventLoop::instance().add_timer(ms, [vm, block]() {
            vm->enqueue_task([vm, block]() {
                if (vm->call_value(block, 0)) {
                    vm->run((int)vm->frames.size() - 1);
                }
            });
        });
        return Value();
    });
}

} // namespace shell_lite

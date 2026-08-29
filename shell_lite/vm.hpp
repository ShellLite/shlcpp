#pragma once
#include "chunk.hpp"
#include "value.hpp"
#include "gc.hpp"
#include "objects.hpp"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <deque>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <queue>
#include <thread>

namespace shell_lite {

struct CallFrame {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
};

struct TryFrame {
    uint8_t* catch_ip;
    int stack_count;
    int frame_count;
};

struct GlobalsTable {
    mutable std::shared_mutex mutex;
    std::unordered_map<std::string, Value> values;
};

class VM {
public:
    static constexpr size_t DEFAULT_STACK_CAPACITY = 1024;

    VM();
    explicit VM(std::shared_ptr<GlobalsTable> shared_globals);
    ~VM();

    Value interpret(ObjFunction* function);
    void print_stack_trace();
    bool has_unhandled_error() const { return had_unhandled_error; }
    void clear_error() { has_error = false; had_unhandled_error = false; error_value = Value(); }
    void set_cli_args(const std::vector<std::string>& args);

    void push(Value value);
    Value pop();
    Value peek(int distance);
    void grow_stack();
    void mark_roots();

    Value* allocate_ffi_value(Value val);
    void release_ffi_value(Value* val_ptr);
    void clear_ffi_pool();


    void enqueue_task(std::function<void()> task);
    void run_loop();

    GCArena& arena() { return arena_; }

    std::shared_ptr<GlobalsTable> globals;
    std::unordered_map<std::string, ObjModule*> module_cache;
    std::vector<std::string> search_paths;
    ObjUpvalue* open_upvalues;
    std::vector<TryFrame> try_stack;
    mutable std::shared_mutex web_mutex;
    std::unordered_map<std::string, Value> web_routes;
    std::vector<Value> web_middlewares;
    std::unordered_map<std::string, std::string> static_routes;

    std::vector<CallFrame> frames;
    Value* stack;
    Value* stack_top;
    int stack_capacity;

    std::vector<std::unique_ptr<Value>> ffi_pool;

    bool has_error;
    bool had_unhandled_error = false;
    Value error_value;

    struct UIWidget {
        std::string kind;
        std::string label;
        Value handler_closure;
        std::string state_buffer;
    };
    bool ui_initialized = false;
    std::vector<UIWidget> active_widgets;

    Value run(int target_frame_depth = 0);
    bool call(ObjClosure* closure, int arg_count);
    bool call_value(Value callee, int arg_count);
    void setup_builtins();
    ObjUpvalue* capture_upvalue(Value* local);
    void close_upvalues(Value* last);

    ObjModule* load_module(const std::string& path);
    std::string resolve_path(const std::string& path);

    uint8_t read_byte();
    uint16_t read_short();
    Value read_constant();
    std::string read_string();

private:
    GCArena arena_;
    std::deque<std::function<void()>> task_queue_;
    std::mutex task_mutex_;
    std::condition_variable task_cv_;
};

}
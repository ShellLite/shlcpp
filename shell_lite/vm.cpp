#include "vm.hpp"
#include "compiler.hpp"
#include "concurrency/thread_pool.hpp"
#include "error/error_reporter.hpp"
#include "event_loop.hpp"
#include "gc.hpp"
#include "imgui.h"
#include "lexer.hpp"
#include "native_registry.hpp"
#include "pal/platform.hpp"
#include "parser.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <random>
#include <sstream>


namespace fs = std::filesystem;

namespace shell_lite {

// bind method to instance so 'self' works
class BoundMethod : public Callable {
public:
  ObjInstance *instance;
  ObjClosure *method;
  BoundMethod(ObjInstance *inst, ObjClosure *m) : instance(inst), method(m) {}
  Value call(VM *vm, int arg_count) override {
    vm->stack_top[-arg_count - 1] = Value(instance);
    if (vm->call(method, arg_count)) {
      return vm->run((int)vm->frames.size() - 1);
    }
    return Value();
  }
  void mark_children() override {
    if (instance && !instance->marked) {
      instance->marked = true;
      instance->mark_children();
    }
    if (method && !method->marked) {
      method->marked = true;
      method->mark_children();
    }
  }
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override {
    if (clones.count(this)) return clones[this];
    auto *inst_clone = instance ? static_cast<ObjInstance*>(instance->clone(target, clones)) : nullptr;
    auto *meth_clone = method ? static_cast<ObjClosure*>(method->clone(target, clones)) : nullptr;
    auto *bm = target.allocate<BoundMethod>(inst_clone, meth_clone);
    clones[this] = bm;
    return bm;
  }
};

static std::string get_executable_directory() {
  return pal::get_executable_path();
}

// collect module search paths from env, exe dir, and system stdlib
static void init_default_search_paths(std::vector<std::string> &paths) {
  paths.push_back(".");

  const char *shl_path_env = std::getenv("SHL_PATH");
  if (shl_path_env) {
    std::string env_str(shl_path_env);
    size_t start = 0;
    char delim = pal::get_path_delim();
    while (start < env_str.size()) {
      size_t end = env_str.find(delim, start);
      if (end == std::string::npos)
        end = env_str.size();
      std::string p = env_str.substr(start, end - start);
      if (!p.empty() && fs::exists(p)) {
        paths.push_back(p);
      }
      start = end + 1;
    }
  }

  std::string exe_dir = get_executable_directory();
  if (!exe_dir.empty()) {
    std::vector<fs::path> stdlib_candidates = {
        fs::path(exe_dir) / "stdlib",
        fs::path(exe_dir) / ".." / "stdlib",
        fs::path(exe_dir) / "shell_lite" / "stdlib",
        fs::path(exe_dir) / ".." / "shell_lite" / "stdlib",
        fs::path(exe_dir) / ".." / "share" / "shell_lite" / "stdlib",
        "/usr/local/share/shell_lite/stdlib",
        "/usr/share/shell_lite/stdlib"};
    const char *home_env = std::getenv("HOME");
    if (home_env) {
      stdlib_candidates.push_back(fs::path(home_env) / ".local" / "share" /
                                  "shell_lite" / "stdlib");
    }
    for (const auto &cand : stdlib_candidates) {
      if (fs::exists(cand)) {
        paths.push_back(fs::absolute(cand).string());
      }
    }
  }
}

// boot up vm with fresh globals default stack as well as stdlib builtins
VM::VM()
    : globals(std::make_shared<GlobalsTable>()), open_upvalues(nullptr),
      has_error(false), arena_(this) {
  static bool seeded = false;
  if (!seeded) {
    std::srand((unsigned int)std::time(nullptr));
    seeded = true;
  }
  stack_capacity = DEFAULT_STACK_CAPACITY;
  stack = new Value[stack_capacity];
  stack_top = stack;
  init_default_search_paths(search_paths);
  setup_builtins();
}

VM::VM(std::shared_ptr<GlobalsTable> shared_globals)
    : globals(shared_globals), open_upvalues(nullptr), has_error(false),
      arena_(this) {
  stack_capacity = DEFAULT_STACK_CAPACITY;
  stack = new Value[stack_capacity];
  stack_top = stack;
  init_default_search_paths(search_paths);
}

VM::~VM() { delete[] stack; }

// pick newer .shl over stale .shbc so we aint runnin outdated bytecode :)
std::string VM::resolve_path(const std::string &path) {
  fs::path target_path(path);
  fs::path shbc_target = target_path;
  fs::path shl_target = target_path;

  if (target_path.extension() == ".shl") {
    shbc_target.replace_extension(".shbc");
  } else if (target_path.extension() == ".shbc") {
    shl_target.replace_extension(".shl");
  } else {
    shbc_target += ".shbc";
    shl_target += ".shl";
  }

  for (const auto &dir_path : search_paths) {
    if (fs::exists(dir_path) && fs::is_directory(dir_path)) {
      fs::path shbc_path = fs::path(dir_path) / shbc_target;
      fs::path shl_path = fs::path(dir_path) / shl_target;

      bool shbc_exists = fs::exists(shbc_path);
      bool shl_exists = fs::exists(shl_path);

      if (shbc_exists && shl_exists) {
        if (fs::last_write_time(shl_path) > fs::last_write_time(shbc_path)) {
          std::cerr << "Warning: Loading newer source file '"
                    << shl_path.string() << "' instead of stale bytecode '"
                    << shbc_path.string() << "'\n";
          return fs::absolute(shl_path).string();
        }
        return fs::absolute(shbc_path).string();
      } else if (shbc_exists) {
        return fs::absolute(shbc_path).string();
      } else if (shl_exists) {
        return fs::absolute(shl_path).string();
      }
    }
  }
  return "";
}

// load module from cache or compile file into a fresh module object
ObjModule *VM::load_module(const std::string &path) {
  if (module_cache.count(path) && module_cache[path] != nullptr) {
    return module_cache[path];
  }

  auto make_native = [this](int args,
                            std::function<Value(VM *, int)> fn) -> Value {
    return Value(
        arena_.allocate<NativeWrapper<std::function<Value(VM *, int)>>>(
            args, std::move(fn)));
  };

  if (path == "random") {
    auto *mod = arena_.allocate<ObjModule>();
    GCRootGuard mod_guard(arena_, mod);
    mod->name = "random";
    mod->globals["random"] = make_native(0, [](VM *vm, int arg_count) -> Value {
      static thread_local std::mt19937 rng(std::random_device{}());
      std::uniform_real_distribution<double> dist(0.0, 1.0);
      return Value(dist(rng));
    });
    mod->globals["randint"] =
        make_native(2, [](VM *vm, int arg_count) -> Value {
          if (arg_count >= 2) {
            long long min_v = (long long)vm->peek(1).as_number();
            long long max_v = (long long)vm->peek(0).as_number();
            if (min_v > max_v)
              std::swap(min_v, max_v);
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<long long> dist(min_v, max_v);
            return Value((double)dist(rng));
          }
          return Value((double)0);
        });
    mod->globals["choice"] =
        make_native(1, [this](VM *vm, int arg_count) -> Value {
          if (arg_count >= 1) {
            Value val = vm->peek(0);
            if (val.is_list()) {
              auto *list = static_cast<ObjList *>(val.get_obj());
              if (!list->elements.empty()) {
                static thread_local std::mt19937 rng(std::random_device{}());
                std::uniform_int_distribution<size_t> dist(
                    0, list->elements.size() - 1);
                return list->elements[dist(rng)];
              }
            }
          }
          return Value();
        });
    module_cache["random"] = mod;
    return mod;
  }
  if (path == "time") {
    auto *mod = arena_.allocate<ObjModule>();
    GCRootGuard mod_guard(arena_, mod);
    mod->name = "time";
    mod->globals["time"] = make_native(0, [](VM *vm, int arg_count) -> Value {
      auto now = std::chrono::system_clock::now();
      auto duration = now.time_since_epoch();
      double seconds = std::chrono::duration<double>(duration).count();
      return Value(seconds);
    });
    mod->globals["time_ms"] =
        make_native(0, [](VM *vm, int arg_count) -> Value {
          auto now = std::chrono::system_clock::now();
          auto duration = now.time_since_epoch();
          double ms =
              (double)std::chrono::duration_cast<std::chrono::milliseconds>(
                  duration)
                  .count();
          return Value(ms);
        });
    mod->globals["sleep"] = make_native(1, [](VM *vm, int arg_count) -> Value {
      if (arg_count >= 1) {
        double s = vm->peek(0).as_number();
        std::this_thread::sleep_for(std::chrono::milliseconds((int)(s * 1000)));
      }
      return Value();
    });
    mod->globals["format"] =
        make_native(-1, [this](VM *vm, int arg_count) -> Value {
          double ts = 0;
          std::string fmt = "%Y-%m-%d %H:%M:%S";
          if (arg_count >= 1)
            ts = vm->peek(arg_count - 1).as_number();
          if (arg_count >= 2)
            fmt = vm->peek(arg_count - 2).to_string();
          std::time_t t = (std::time_t)ts;
          std::tm tm_buf;
          pal::localtime_safe(&t, &tm_buf);
          char buf[128];
          std::strftime(buf, sizeof(buf), fmt.c_str(), &tm_buf);
          return Value(arena_.allocate_string(std::string(buf)));
        });
    module_cache["time"] = mod;
    return mod;
  }
  if (path == "subprocess") {
    auto *mod = arena_.allocate<ObjModule>();
    GCRootGuard mod_guard(arena_, mod);
    mod->name = "subprocess";
    mod->globals["run"] = make_native(-1, [](VM *vm, int arg_count) -> Value {
      if (arg_count >= 1) {
        std::string cmd = vm->peek(arg_count - 1).to_string();
        int ret = std::system(cmd.c_str());
        return Value((double)ret);
      }
      return Value();
    });
    module_cache["subprocess"] = mod;
    return mod;
  }

  std::string abs_path = resolve_path(path);
  if (abs_path.empty()) {
    has_error = true;
    error_value = Value(arena_.allocate_string("Module not found: " + path));
    return nullptr;
  }

  if (module_cache.count(abs_path)) {
    if (module_cache[abs_path] == nullptr) {
      has_error = true;
      error_value = Value(arena_.allocate_string(
          "Circular dependency detected loading: " + abs_path));
      return nullptr;
    }
    return module_cache[abs_path];
  }

  module_cache[abs_path] = nullptr;
  ObjFunction *function = nullptr;

  if (abs_path.size() >= 5 && abs_path.substr(abs_path.size() - 5) == ".shbc") {
    std::ifstream file(abs_path, std::ios::binary);
    if (!file.is_open()) {
      has_error = true;
      error_value = Value(
          arena_.allocate_string("Could not open module file: " + abs_path));
      return nullptr;
    }
    try {
      function = ObjFunction::deserialize(file, arena_);
    } catch (const std::exception &e) {
      has_error = true;
      error_value = Value(arena_.allocate_string("Error loading SHBC module " +
                                                 abs_path + ": " + e.what()));
      return nullptr;
    }
  } else {
    std::ifstream file(abs_path);
    if (!file.is_open()) {
      has_error = true;
      error_value = Value(
          arena_.allocate_string("Could not open module file: " + abs_path));
      return nullptr;
    }
    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    try {
      Parser parser(source);
      auto nodes = parser.parse();
      Compiler compiler(this);
      function = compiler.compile(abs_path, nodes);
    } catch (const std::exception &e) {
      has_error = true;
      error_value = Value(arena_.allocate_string("Error parsing module " +
                                                 abs_path + ": " + e.what()));
      return nullptr;
    }
  }

  try {
    auto old_globals = globals;
    globals = std::make_shared<GlobalsTable>();
    setup_builtins();
    for (auto &pair : old_globals->values) {
      globals->values[pair.first] = pair.second;
    }

    auto *closure = arena_.allocate<ObjClosure>(function);
    GCRootGuard closure_guard(arena_, closure);
    closure->module_globals = globals;

    auto *module = arena_.allocate<ObjModule>();
    GCRootGuard module_guard(arena_, module);
    module->name = fs::path(abs_path).stem().string();

    module_cache[abs_path] = module;

    struct GlobalsRestorer {
      std::shared_ptr<GlobalsTable> &target;
      std::shared_ptr<GlobalsTable> original;
      ~GlobalsRestorer() { target = std::move(original); }
    } restorer{globals, old_globals};

    push(Value(closure));
    if (call(closure, 0)) {
      run((int)frames.size() - 1);
    }

    module->globals = globals->values;
    return module;
  } catch (const std::exception &e) {
    has_error = true;
    error_value = Value(arena_.allocate_string("Error loading module " +
                                               abs_path + ": " + e.what()));
    return nullptr;
  }
}

void VM::enqueue_task(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(task_mutex_);
    task_queue_.push_back(std::move(task));
  }
  task_cv_.notify_one();
}

// drain async tasks and poll event loop until work is cooked
void VM::run_loop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(task_mutex_);

      if (task_queue_.empty()) {
        task_cv_.wait_for(lock, std::chrono::milliseconds(10));
      }

      if (!task_queue_.empty()) {
        task = std::move(task_queue_.front());
        task_queue_.pop_front();
      }
    }

    if (task) {
      task();
    }

    EventLoop::instance().poll(0);

    if (!EventLoop::instance().has_work() && task_queue_.empty()) {
      break;
    }
  }
}

// stack ran outta space, double it and fix up all slot pointers
void VM::grow_stack() {
  int old_capacity = stack_capacity;
  stack_capacity *= 2;
  Value *old_stack = stack;
  Value *new_stack = new Value[stack_capacity];
  std::copy(old_stack, old_stack + old_capacity, new_stack);
  stack = new_stack;

  stack_top = stack + (stack_top - old_stack);
  for (auto &frame : frames) {
    frame.slots = stack + (frame.slots - old_stack);
  }
  for (ObjUpvalue *uv = open_upvalues; uv != nullptr; uv = uv->next) {
    if (uv->location >= old_stack && uv->location < old_stack + old_capacity) {
      uv->location = stack + (uv->location - old_stack);
    }
  }
  delete[] old_stack;
}

// push value to stack, grow if full
void VM::push(Value value) {
  if (stack_top - stack >= stack_capacity)
    grow_stack();
  *stack_top++ = value;
}

// pop value from stack top
Value VM::pop() { return *(--stack_top); }
Value VM::peek(int distance) { return stack_top[-1 - distance]; }

Value *VM::allocate_ffi_value(Value val) {
  ffi_pool.push_back(std::make_unique<Value>(val));
  return ffi_pool.back().get();
}

void VM::release_ffi_value(Value *val_ptr) {
  if (!val_ptr) return;
  for (auto it = ffi_pool.begin(); it != ffi_pool.end(); ++it) {
    if (it->get() == val_ptr) {
      ffi_pool.erase(it);
      break;
    }
  }
}

void VM::clear_ffi_pool() { ffi_pool.clear(); }

// mark everything alive rn so gc doesn't nuke it -_-
void VM::mark_roots() {
  for (Value *slot = stack; slot < stack_top; slot++)
    slot->mark();
  {
    std::shared_lock<std::shared_mutex> lock(globals->mutex);
    for (auto &pair : globals->values)
      pair.second.mark();
  }
  for (auto &frame : frames)
    if (frame.closure)
      Value(frame.closure).mark();
  for (ObjUpvalue *uv = open_upvalues; uv != nullptr; uv = uv->next)
    Value(uv).mark();
  for (auto &pair : web_routes)
    pair.second.mark();
  for (auto &mw : web_middlewares)
    mw.mark();
  for (auto &pair : module_cache)
    if (pair.second)
      Value(pair.second).mark();
  for (auto &widget : active_widgets)
    widget.handler_closure.mark();
  error_value.mark();
  for (auto &val_ptr : ffi_pool)
    if (val_ptr)
      val_ptr->mark();
}

// fetch next bytecode instruction and bump ip
uint8_t VM::read_byte() { return *frames.back().ip++; }
uint16_t VM::read_short() {
  frames.back().ip += 2;
  return (uint16_t)((frames.back().ip[-2] << 8) | frames.back().ip[-1]);
}
Value VM::read_constant() {
  return frames.back().closure->function->chunk->constants[read_short()];
}
const std::string& VM::read_string_ref() {
  uint16_t idx = read_short();
  const Value& val = frames.back().closure->function->chunk->constants[idx];
  return val.as_string();
}
std::string VM::read_string() { return read_string_ref(); }

// spin up a new callframe check arity first so args dont mismatch
bool VM::call(ObjClosure *closure, int arg_count) {
  if (closure->function->arity >= 0 && arg_count != closure->function->arity) {
    has_error = true;
    error_value = Value(arena_.allocate_string(
        "Expected " + std::to_string(closure->function->arity) +
        " arguments but got " + std::to_string(arg_count) + "."));
    return false;
  }
  frames.push_back({closure, closure->function->chunk->code.data(),
                    stack_top - arg_count - 1});
  return true;
}

// dynamic dispatch for closures functions classes and native callables
bool VM::call_value(Value callee, int arg_count) {
  if (callee.is_closure())
    return call(static_cast<ObjClosure *>(callee.get_obj()), arg_count);
  if (callee.is_function()) {
    auto *closure = arena_.allocate<ObjClosure>(
        static_cast<ObjFunction *>(callee.get_obj()));
    stack_top[-arg_count - 1] = Value(closure);
    return call(closure, arg_count);
  }
  if (callee.is_class()) {
    ObjClass *klass = static_cast<ObjClass *>(callee.get_obj());
    auto instance = arena_.allocate<ObjInstance>();
    instance->klass = klass;
    stack_top[-arg_count - 1] = Value(instance);
    auto it = klass->methods.find("init");
    if (it != klass->methods.end()) {
      return call(it->second, arg_count);
    } else {
      for (int i = 0; i < arg_count && i < (int)klass->default_fields.size();
           ++i) {
        instance->fields[klass->default_fields[i]] = peek(arg_count - 1 - i);
      }
      for (size_t i = arg_count; i < klass->default_fields.size(); ++i) {
        instance->fields[klass->default_fields[i]] = Value((double)0);
      }
      stack_top -= arg_count + 1;
      push(Value(instance));
      return true;
    }
  }
  if (callee.is_callable()) {
    auto *bm =
        dynamic_cast<BoundMethod *>(static_cast<Callable *>(callee.get_obj()));
    if (bm) {
      stack_top[-arg_count - 1] = Value(bm->instance);
      return call(bm->method, arg_count);
    }
    Value res =
        static_cast<Callable *>(callee.get_obj())->call(this, arg_count);
    stack_top -= arg_count + 1;
    push(res);
    return true;
  }
  has_error = true;
  std::string val_desc =
      callee.is_null()
          ? "null"
          : (callee.is_bool()
                 ? "bool"
                 : (callee.is_number()
                        ? "number (" + std::to_string(callee.as_number()) + ")"
                        : (callee.is_string()
                               ? "string (" + callee.as_string() + ")"
                               : "other")));
  error_value = Value(arena_.allocate_string(
      "Can only call functions and classes. Got: " + val_desc));
  return false;
}

// grab existing open upval or hoist a new one onto the list
ObjUpvalue *VM::capture_upvalue(Value *local) {
  ObjUpvalue *prev = nullptr;
  ObjUpvalue *curr = open_upvalues;
  while (curr != nullptr && curr->location > local) {
    prev = curr;
    curr = curr->next;
  }
  if (curr != nullptr && curr->location == local)
    return curr;
  auto *uv = arena_.allocate<ObjUpvalue>(local);
  uv->next = curr;
  if (prev == nullptr)
    open_upvalues = uv;
  else
    prev->next = uv;
  return uv;
}

// local going out of scope copy to heap so closure keeps it
void VM::close_upvalues(Value *last) {
  while (open_upvalues != nullptr && open_upvalues->location >= last) {
    ObjUpvalue *uv = open_upvalues;
    uv->closed = *uv->location;
    uv->location = &uv->closed;
    open_upvalues = uv->next;
  }
}

struct StackFrameInfo {
  std::string source_file;
  int line = 0;
  int col = 0;
  std::string formatted_line;
};

static std::vector<StackFrameInfo> get_stack_frame_info(const std::vector<CallFrame> &frames) {
  std::vector<StackFrameInfo> info;
  info.reserve(frames.size());
  for (const auto &frame : frames) {
    ObjFunction *function = frame.closure->function;
    int offset = (int)(frame.ip - function->chunk->code.data());
    auto loc = function->chunk->get_location(offset > 0 ? offset - 1 : 0);
    std::string fn_name = (function->name.empty() || function->name == "script")
                              ? "<module>"
                              : function->name + "()";
    std::string formatted = "File \"" + function->source_file + "\", line " +
                            std::to_string(loc.first) + ":" + std::to_string(loc.second) +
                            ", in " + fn_name;
    info.push_back({function->source_file, loc.first, loc.second, std::move(formatted)});
  }
  return info;
}

void VM::print_stack_trace() {
  std::cerr << "Stack Trace (most recent call last):" << std::endl;
  for (const auto &f : get_stack_frame_info(frames)) {
    std::cerr << "  " << f.formatted_line << std::endl;
  }
}

// wrap top level script in a closure and fire up the vm :)
Value VM::interpret(ObjFunction *function) {
  auto *closure = arena_.allocate<ObjClosure>(function);
  push(Value(closure));
  if (!call(closure, 0))
    return Value();
  Value res = run();
  return res;
}

// main dispatch loop 
Value VM::run(int target_frame_depth) {
  while (true) {
    // uhhh hit an error ig we unwind to a catch block or bail with traceback
    if (has_error) {
      if (try_stack.empty()) {
        auto frame_info = get_stack_frame_info(frames);
        std::vector<std::string> btrace;
        btrace.reserve(frame_info.size());
        for (const auto &fi : frame_info) {
          btrace.push_back(fi.formatted_line);
        }
        std::string src_file = frame_info.empty() ? "" : frame_info.back().source_file;
        int err_line = frame_info.empty() ? 0 : frame_info.back().line;
        int err_col = frame_info.empty() ? 0 : frame_info.back().col;

        ErrorReporter::report(error_value, SourceLocation(src_file, err_line, err_col), btrace);
        has_error = false;
        had_unhandled_error = true;
        return Value();
      }
      auto tf = try_stack.back();
      try_stack.pop_back();
      while (frames.size() > (size_t)tf.frame_count) {
        close_upvalues(frames.back().slots);
        frames.pop_back();
      }
      close_upvalues(stack + tf.stack_count);
      stack_top = stack + tf.stack_count;
      push(error_value);
      frames.back().ip = tf.catch_ip;
      has_error = false;
      error_value = Value();
    }

    uint8_t instruction;
    switch (instruction = read_byte()) {
    case OP_CONSTANT:
      push(read_constant());
      break;
    case OP_NULL:
      push(Value());
      break;
    case OP_TRUE:
      push(Value(true));
      break;
    case OP_FALSE:
      push(Value(false));
      break;
    case OP_POP:
      pop();
      break;
    case OP_GET_LOCAL:
      push(frames.back().slots[read_short()]);
      break;
    case OP_SET_LOCAL:
      frames.back().slots[read_short()] = peek(0);
      break;
    // grab global from module first, fallback to root globals
    case OP_GET_GLOBAL: {
      const std::string &name = read_string_ref();
      std::shared_ptr<GlobalsTable> target_globals = globals;
      if (!frames.empty() && frames.back().closure &&
          frames.back().closure->module_globals) {
        target_globals = frames.back().closure->module_globals;
      }
      {
        std::shared_lock<std::shared_mutex> lock(target_globals->mutex);
        auto it = target_globals->values.find(name);
        if (it != target_globals->values.end()) {
          push(it->second);
          break;
        }
      }
      if (target_globals != globals) {
        std::shared_lock<std::shared_mutex> lock(globals->mutex);
        auto it = globals->values.find(name);
        if (it != globals->values.end()) {
          push(it->second);
          break;
        }
      }
      has_error = true;
      error_value =
          Value(arena_.allocate_string("Undefined variable: " + name));
      break;
    }
    // define global in active module or root scope
    case OP_DEFINE_GLOBAL: {
      const std::string &name = read_string_ref();
      std::shared_ptr<GlobalsTable> target_globals = globals;
      if (!frames.empty() && frames.back().closure &&
          frames.back().closure->module_globals) {
        target_globals = frames.back().closure->module_globals;
      }
      std::unique_lock<std::shared_mutex> lock(target_globals->mutex);
      target_globals->values[name] = pop();
      break;
    }
    case OP_SET_GLOBAL: {
      const std::string &name = read_string_ref();
      std::shared_ptr<GlobalsTable> target_globals = globals;
      if (!frames.empty() && frames.back().closure &&
          frames.back().closure->module_globals) {
        target_globals = frames.back().closure->module_globals;
      }
      bool found = false;
      {
        std::unique_lock<std::shared_mutex> lock(target_globals->mutex);
        auto it = target_globals->values.find(name);
        if (it != target_globals->values.end()) {
          it->second = peek(0);
          found = true;
        }
      }
      if (!found && target_globals != globals) {
        std::unique_lock<std::shared_mutex> lock(globals->mutex);
        auto it = globals->values.find(name);
        if (it != globals->values.end()) {
          it->second = peek(0);
          found = true;
        }
      }
      if (!found) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Undefined variable: " + name));
        break;
      }
      break;
    }
    case OP_GET_UPVALUE: {
      uint16_t idx = read_short();
      push(*frames.back().closure->upvalues[idx]->location);
      break;
    }
    case OP_SET_UPVALUE: {
      uint16_t idx = read_short();
      *frames.back().closure->upvalues[idx]->location = peek(0);
      break;
    }
    // add numbers or concat strings or merge lists
    case OP_ADD: {
      Value b = pop();
      Value a = pop();
      Value res = a + b;
      if (res.is_null() && !a.is_null() && !b.is_null()) {
        has_error = true;
        error_value = Value(arena_.allocate_string(
            "Operands must be numbers, strings, or lists for +"));
        break;
      }
      push(res);
      break;
    }
    case OP_SUBTRACT: {
      Value b = pop();
      Value a = pop();
      if (a.is_number() && b.is_number()) {
        push(Value(a.as_number() - b.as_number()));
        break;
      }
      has_error = true;
      error_value =
          Value(arena_.allocate_string("Operands must be numbers for -"));
      break;
    }
    case OP_MULTIPLY: {
      Value b = pop();
      Value a = pop();
      if ((a.is_number() && b.is_number()) ||
          (a.is_string() && b.is_number())) {
        push(a * b);
      } else {
        has_error = true;
        error_value = Value(arena_.allocate_string(
            "Operands must be numbers, or string and number for *"));
      }
      break;
    }
    case OP_DIVIDE: {
      Value b = pop();
      Value a = pop();
      if (!a.is_number() || !b.is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operands must be numbers for /"));
        break;
      }
      if (b.as_number() == 0) {
        has_error = true;
        error_value = Value(arena_.allocate_string("Division by zero"));
        break;
      }
      push(a / b);
      break;
    }
    case OP_MOD: {
      Value b = pop();
      Value a = pop();
      if (!a.is_number() || !b.is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operands must be numbers for %"));
        break;
      }
      if (b.as_number() == 0) {
        has_error = true;
        error_value = Value(arena_.allocate_string("Modulo by zero"));
        break;
      }
      double mod_val = std::fmod(a.as_number(), b.as_number());
      if ((mod_val < 0 && b.as_number() > 0) ||
          (mod_val > 0 && b.as_number() < 0)) {
        mod_val += b.as_number();
      }
      push(Value(mod_val));
      break;
    }
    case OP_POW: {
      Value b = pop();
      Value a = pop();
      if (!a.is_number() || !b.is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operands must be numbers for **"));
        break;
      }
      push(Value(std::pow(a.as_number(), b.as_number())));
      break;
    }
    case OP_BIT_AND: {
      Value b = pop();
      Value a = pop();
      if (!a.is_number() || !b.is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operands must be numbers for &"));
        break;
      }
      push(Value((double)((int64_t)a.as_number() & (int64_t)b.as_number())));
      break;
    }
    case OP_BIT_OR: {
      Value b = pop();
      Value a = pop();
      if (!a.is_number() || !b.is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operands must be numbers for |"));
        break;
      }
      push(Value((double)((int64_t)a.as_number() | (int64_t)b.as_number())));
      break;
    }
    case OP_BIT_XOR: {
      Value b = pop();
      Value a = pop();
      if (!a.is_number() || !b.is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operands must be numbers for ^"));
        break;
      }
      push(Value((double)((int64_t)a.as_number() ^ (int64_t)b.as_number())));
      break;
    }
    case OP_LSHIFT: {
      Value b = pop();
      Value a = pop();
      if (!a.is_number() || !b.is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operands must be numbers for <<"));
        break;
      }
      uint64_t val = (uint64_t)(int64_t)a.as_number();
      int shift = (int)b.as_number() & 63;
      push(Value((double)(int64_t)(val << shift)));
      break;
    }
    case OP_RSHIFT: {
      Value b = pop();
      Value a = pop();
      if (!a.is_number() || !b.is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operands must be numbers for >>"));
        break;
      }
      int64_t val = (int64_t)a.as_number();
      int shift = (int)b.as_number() & 63;
      push(Value((double)(val >> shift)));
      break;
    }
    case OP_BIT_NOT: {
      if (!peek(0).is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operand must be a number for ~"));
        break;
      }
      push(Value((double)(~(int64_t)pop().as_number())));
      break;
    }
    // equality check on top two stack vals
    case OP_EQUAL: {
      Value b = pop();
      Value a = pop();
      push(Value(a == b));
      break;
    }
    case OP_NOT_EQUAL: {
      Value b = pop();
      Value a = pop();
      push(Value(a != b));
      break;
    }
    case OP_GREATER: {
      Value b = pop();
      Value a = pop();
      push(Value(a > b));
      break;
    }
    case OP_LESS: {
      Value b = pop();
      Value a = pop();
      push(Value(a < b));
      break;
    }
    case OP_GREATER_EQUAL: {
      Value b = pop();
      Value a = pop();
      push(Value(a >= b));
      break;
    }
    case OP_LESS_EQUAL: {
      Value b = pop();
      Value a = pop();
      push(Value(a <= b));
      break;
    }
    case OP_NOT:
      push(Value(!pop().as_bool()));
      break;
    case OP_NEGATE: {
      if (!peek(0).is_number()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Operand must be a number for -"));
        break;
      }
      push(Value(-pop().as_number()));
      break;
    }
    case OP_GET_ITER: {
      Value iterable = pop();
      auto *iter_obj = arena_.allocate<ObjIterator>(iterable);
      push(Value(iter_obj));
      break;
    }
    case OP_FOR_ITER: {
      uint16_t offset = read_short();
      Value iter_val = peek(0);
      if (!iter_val.is_iterator()) {
        has_error = true;
        error_value = Value(arena_.allocate_string("Invalid iterator object"));
        break;
      }
      auto *iter_obj = static_cast<ObjIterator *>(iter_val.get_obj());
      bool has_next = false;
      Value next_val;

      if (iter_obj->iterable.is_list()) {
        auto *list = static_cast<ObjList *>(iter_obj->iterable.get_obj());
        if (iter_obj->index < list->elements.size()) {
          next_val = list->elements[iter_obj->index++];
          has_next = true;
        }
      } else if (iter_obj->iterable.is_string()) {
        const std::string &s = iter_obj->iterable.as_string();
        if (iter_obj->index < s.size()) {
          std::string c(1, s[iter_obj->index++]);
          next_val = Value(arena_.allocate_string(c));
          has_next = true;
        }
      } else if (iter_obj->iterable.is_dict()) {
        if (iter_obj->index < iter_obj->dict_keys.size()) {
          next_val = Value(
              arena_.allocate_string(iter_obj->dict_keys[iter_obj->index++]));
          has_next = true;
        }
      }

      if (has_next) {
        push(next_val);
      } else {
        frames.back().ip += offset;
      }
      break;
    }
    case OP_PRINT:
      std::cout << pop().to_string() << std::endl;
      break;
    // construct dynamic list from top stack elements
    case OP_LIST: {
      uint8_t count = read_byte();
      auto *list = arena_.allocate<ObjList>();
      list->elements.resize(count);
      for (int i = count - 1; i >= 0; --i) {
        list->elements[i] = pop();
      }
      push(Value(list));
      break;
    }
    // construct dict from key val pairs on stack
    case OP_DICT: {
      uint8_t count = read_byte();
      auto *dict = arena_.allocate<ObjDict>();
      for (int i = 0; i < count; ++i) {
        Value val = pop();
        Value key = pop();
        dict->elements[key.to_string()] = val;
      }
      push(Value(dict));
      break;
    }
    // index into list or dict or string
    case OP_GET_INDEX: {
      Value index = pop();
      Value obj = pop();
      if (obj.is_list()) {
        if (!index.is_number()) {
          has_error = true;
          error_value =
              Value(arena_.allocate_string("List index must be a number"));
          break;
        }
        auto *list = static_cast<ObjList *>(obj.get_obj());
        int idx = (int)index.as_number();
        if (idx < 0)
          idx += (int)list->elements.size();
        if (idx < 0 || idx >= (int)list->elements.size()) {
          has_error = true;
          error_value =
              Value(arena_.allocate_string("List index out of range"));
          break;
        }
        push(list->elements[idx]);
      } else if (obj.is_dict()) {
        auto *dict = static_cast<ObjDict *>(obj.get_obj());
        std::string key = index.to_string();
        auto it = dict->elements.find(key);
        if (it == dict->elements.end()) {
          push(Value());
        } else {
          push(it->second);
        }
      } else if (obj.is_string()) {
        std::string s = obj.as_string();
        if (!index.is_number()) {
          has_error = true;
          error_value =
              Value(arena_.allocate_string("String index must be a number"));
          break;
        }
        int idx = (int)index.as_number();
        if (idx < 0)
          idx += (int)s.size();
        if (idx < 0 || idx >= (int)s.size()) {
          has_error = true;
          error_value =
              Value(arena_.allocate_string("String index out of range"));
          break;
        }
        push(Value(arena_.allocate_string(std::string(1, s[idx]))));
      } else {
        has_error = true;
        error_value = Value(arena_.allocate_string("Object is not indexable"));
        break;
      }
      break;
    }
    // python-style slice on list or string with optional step
    case OP_SLICE: {
      Value step_val = pop();
      Value stop_val = pop();
      Value start_val = pop();
      Value obj = pop();

      bool has_start = start_val.is_number();
      bool has_stop = stop_val.is_number();
      int step = step_val.is_number() ? (int)step_val.as_number() : 1;
      if (step == 0)
        step = 1;

      auto clamp_val = [](int val, int lo, int hi) -> int {
        return val < lo ? lo : (val > hi ? hi : val);
      };
      auto max_val = [](int a, int b) -> int { return a > b ? a : b; };

      if (obj.is_list()) {
        auto *list = static_cast<ObjList *>(obj.get_obj());
        int sz = (int)list->elements.size();
        int start_idx =
            has_start ? (int)start_val.as_number() : (step > 0 ? 0 : sz - 1);
        int stop_idx =
            has_stop ? (int)stop_val.as_number() : (step > 0 ? sz : -1);

        if (has_start && start_idx < 0)
          start_idx += sz;
        if (has_stop && stop_idx < 0)
          stop_idx += sz;

        if (step > 0) {
          start_idx = clamp_val(start_idx, 0, sz);
          stop_idx = clamp_val(stop_idx, 0, sz);
        } else {
          start_idx = clamp_val(start_idx, 0, max_val(0, sz - 1));
          stop_idx = clamp_val(stop_idx, -1, max_val(-1, sz - 1));
        }

        auto *res = arena_.allocate<ObjList>();
        if (step > 0) {
          for (int i = start_idx; i < stop_idx; i += step)
            res->elements.push_back(list->elements[i]);
        } else if (step < 0) {
          for (int i = start_idx; i > stop_idx; i += step)
            res->elements.push_back(list->elements[i]);
        }
        push(Value(res));
      } else if (obj.is_string()) {
        std::string s = obj.as_string();
        int sz = (int)s.size();
        int start_idx =
            has_start ? (int)start_val.as_number() : (step > 0 ? 0 : sz - 1);
        int stop_idx =
            has_stop ? (int)stop_val.as_number() : (step > 0 ? sz : -1);

        if (has_start && start_idx < 0)
          start_idx += sz;
        if (has_stop && stop_idx < 0)
          stop_idx += sz;

        if (step > 0) {
          start_idx = clamp_val(start_idx, 0, sz);
          stop_idx = clamp_val(stop_idx, 0, sz);
        } else {
          start_idx = clamp_val(start_idx, 0, max_val(0, sz - 1));
          stop_idx = clamp_val(stop_idx, -1, max_val(-1, sz - 1));
        }

        std::string res;
        if (step > 0) {
          for (int i = start_idx; i < stop_idx; i += step)
            res += s[i];
        } else if (step < 0) {
          for (int i = start_idx; i > stop_idx; i += step)
            res += s[i];
        }
        push(Value(arena_.allocate_string(res)));
      } else {
        has_error = true;
        error_value = Value(arena_.allocate_string("Object is not sliceable"));
        break;
      }
      break;
    }
    case OP_SET_INDEX: {
      Value val = pop();
      Value index = pop();
      Value obj = pop();
      if (obj.is_list()) {
        if (!index.is_number()) {
          has_error = true;
          error_value =
              Value(arena_.allocate_string("List index must be a number"));
          break;
        }
        auto *list = static_cast<ObjList *>(obj.get_obj());
        int idx = (int)index.as_number();
        if (idx < 0)
          idx += (int)list->elements.size();
        if (idx < 0 || idx >= (int)list->elements.size()) {
          has_error = true;
          error_value =
              Value(arena_.allocate_string("List index out of range"));
          break;
        }
        list->elements[idx] = val;
      } else if (obj.is_dict()) {
        auto *dict = static_cast<ObjDict *>(obj.get_obj());
        std::string key = index.to_string();
        dict->elements[key] = val;
      } else {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Object is not index assignable"));
        break;
      }
      push(val);
      break;
    }
    case OP_LIST_APPEND: {
      uint8_t distance = read_byte();
      Value val = pop();
      Value list_val = peek(distance);
      if (list_val.is_list()) {
        auto *list = static_cast<ObjList *>(list_val.get_obj());
        list->elements.push_back(val);
      }
      break;
    }
    case OP_CLASS: {
      std::string name = read_string();
      auto *klass = arena_.allocate<ObjClass>(name);
      push(Value(klass));
      break;
    }
    case OP_METHOD: {
      std::string name = read_string();
      Value method = pop();
      ObjClass *klass = static_cast<ObjClass *>(peek(0).get_obj());
      klass->methods[name] = static_cast<ObjClosure *>(method.get_obj());
      break;
    }
    case OP_PROPERTY: {
      const std::string &name = read_string_ref();
      ObjClass *klass = static_cast<ObjClass *>(peek(0).get_obj());
      klass->default_fields.push_back(name);
      break;
    }
    case OP_GET_SELF_PROPERTY: {
      const std::string &name = read_string_ref();
      Value self_val = frames.back().slots[0];
      if (self_val.is_instance()) {
        ObjInstance *instance = static_cast<ObjInstance *>(self_val.get_obj());
        auto it = instance->fields.find(name);
        if (it != instance->fields.end()) {
          push(it->second);
        } else {
          auto method_it = instance->klass->methods.find(name);
          if (method_it != instance->klass->methods.end()) {
            auto *bm =
                arena_.allocate<BoundMethod>(instance, method_it->second);
            push(Value(bm));
          } else {
            push(Value());
          }
        }
      } else {
        push(Value());
      }
      break;
    }
    case OP_SET_SELF_PROPERTY: {
      const std::string &name = read_string_ref();
      Value val = peek(0);
      Value self_val = frames.back().slots[0];
      if (self_val.is_instance()) {
        ObjInstance *instance = static_cast<ObjInstance *>(self_val.get_obj());
        instance->fields[name] = val;
      }
      break;
    }
    // read field from instance or module and bind method if callable
    case OP_GET_PROPERTY: {
      const std::string &name = read_string_ref();
      Value receiver = pop();
      if (receiver.is_instance()) {
        ObjInstance *instance = static_cast<ObjInstance *>(receiver.get_obj());
        auto it = instance->fields.find(name);
        if (it != instance->fields.end()) {
          push(it->second);
        } else {
          auto method_it = instance->klass->methods.find(name);
          if (method_it != instance->klass->methods.end()) {
            auto *bound =
                arena_.allocate<BoundMethod>(instance, method_it->second);
            push(Value(bound));
          } else {
            has_error = true;
            error_value = Value(
                arena_.allocate_string("Undefined property '" + name + "'"));
            break;
          }
        }
      } else if (receiver.is_module()) {
        ObjModule *module = static_cast<ObjModule *>(receiver.get_obj());
        auto it = module->globals.find(name);
        if (it != module->globals.end()) {
          push(it->second);
        } else {
          has_error = true;
          error_value = Value(
              arena_.allocate_string("Undefined module member '" + name + "'"));
          break;
        }
      } else {
        has_error = true;
        error_value = Value(arena_.allocate_string(
            "Only instances and modules have properties."));
        break;
      }
      break;
    }
    // set instance field
    case OP_SET_PROPERTY: {
      const std::string &name = read_string_ref();
      Value val = pop();
      Value receiver = pop();
      if (receiver.is_instance()) {
        ObjInstance *instance = static_cast<ObjInstance *>(receiver.get_obj());
        instance->fields[name] = val;
        push(val);
      } else {
        has_error = true;
        error_value = Value(
            arena_.allocate_string("Only instances have mutable properties."));
        break;
      }
      break;
    }
    case OP_INVOKE: {
      const std::string &method_name = read_string_ref();
      uint8_t arg_count = read_byte();
      Value receiver = peek(arg_count);
      if (receiver.is_instance()) {
        ObjInstance *instance = static_cast<ObjInstance *>(receiver.get_obj());
        auto it = instance->klass->methods.find(method_name);
        if (it != instance->klass->methods.end()) {
          if (!call(it->second, arg_count))
            break;
        } else {
          auto field_it = instance->fields.find(method_name);
          if (field_it != instance->fields.end() &&
              field_it->second.is_closure()) {
            stack_top[-arg_count - 1] = field_it->second;
            if (!call_value(field_it->second, arg_count))
              break;
          } else {
            has_error = true;
            error_value = Value(arena_.allocate_string("Undefined method '" +
                                                       method_name + "'"));
            break;
          }
        }
      } else if (receiver.is_module()) {
        ObjModule *module = static_cast<ObjModule *>(receiver.get_obj());
        auto it = module->globals.find(method_name);
        if (it != module->globals.end()) {
          stack_top[-arg_count - 1] = it->second;
          if (!call_value(it->second, arg_count))
            break;
        } else {
          has_error = true;
          error_value = Value(arena_.allocate_string(
              "Undefined module member '" + method_name + "'"));
          break;
        }
      } else if (receiver.is_list()) {
        ObjList *list = static_cast<ObjList *>(receiver.get_obj());
        if (method_name == "append" || method_name == "push") {
          if (arg_count >= 1) {
            list->elements.push_back(peek(0));
          }
          stack_top -= arg_count + 1;
          push(receiver);
        } else if (method_name == "pop") {
          Value ret;
          if (arg_count == 0) {
            if (!list->elements.empty()) {
              ret = list->elements.back();
              list->elements.pop_back();
            }
          } else if (arg_count >= 1) {
            int idx = (int)peek(0).as_number();
            if (idx >= 0 && idx < (int)list->elements.size()) {
              ret = list->elements[idx];
              list->elements.erase(list->elements.begin() + idx);
            }
          }
          stack_top -= arg_count + 1;
          push(ret);
        } else if (method_name == "len" || method_name == "length" ||
                   method_name == "size") {
          stack_top -= arg_count + 1;
          push(Value((double)list->elements.size()));
        } else if (method_name == "clear") {
          list->elements.clear();
          stack_top -= arg_count + 1;
          push(receiver);
        } else {
          has_error = true;
          error_value = Value(arena_.allocate_string("Undefined list method '" +
                                                     method_name + "'"));
          break;
        }
      } else if (receiver.is_dict()) {
        ObjDict *dict = static_cast<ObjDict *>(receiver.get_obj());
        if (method_name == "keys") {
          ObjList *keys_list = arena_.allocate<ObjList>();
          GCRootGuard guard(arena_, keys_list);
          for (auto &p : dict->elements) {
            keys_list->elements.push_back(
                Value(arena_.allocate_string(p.first)));
          }
          stack_top -= arg_count + 1;
          push(Value(keys_list));
        } else if (method_name == "values") {
          ObjList *vals_list = arena_.allocate<ObjList>();
          GCRootGuard guard(arena_, vals_list);
          for (auto &p : dict->elements) {
            vals_list->elements.push_back(p.second);
          }
          stack_top -= arg_count + 1;
          push(Value(vals_list));
        } else if (method_name == "has" || method_name == "contains") {
          bool has_key = false;
          if (arg_count >= 1) {
            has_key = (dict->elements.find(peek(0).to_string()) !=
                       dict->elements.end());
          }
          stack_top -= arg_count + 1;
          push(Value(has_key));
        } else {
          has_error = true;
          error_value = Value(arena_.allocate_string("Undefined dict method '" +
                                                     method_name + "'"));
          break;
        }
      } else if (receiver.is_string()) {
        std::string str = receiver.as_string();
        if (method_name == "len" || method_name == "length" ||
            method_name == "size") {
          stack_top -= arg_count + 1;
          push(Value((double)str.size()));
        } else if (method_name == "upper") {
          std::string up = str;
          std::transform(up.begin(), up.end(), up.begin(), ::toupper);
          stack_top -= arg_count + 1;
          push(Value(arena_.allocate_string(up)));
        } else if (method_name == "lower") {
          std::string low = str;
          std::transform(low.begin(), low.end(), low.begin(), ::tolower);
          stack_top -= arg_count + 1;
          push(Value(arena_.allocate_string(low)));
        } else if (method_name == "strip" || method_name == "trim") {
          size_t first = str.find_first_not_of(" \t\n\r");
          std::string res = (first == std::string::npos) ? "" : str.substr(first, str.find_last_not_of(" \t\n\r") - first + 1);
          stack_top -= arg_count + 1;
          push(Value(arena_.allocate_string(res)));
        } else if (method_name == "capitalize") {
          std::string cap = str;
          if (!cap.empty()) cap[0] = (char)::toupper(cap[0]);
          stack_top -= arg_count + 1;
          push(Value(arena_.allocate_string(cap)));
        } else if (method_name == "starts_with") {
          bool sw = (arg_count >= 1 && str.rfind(peek(0).to_string(), 0) == 0);
          stack_top -= arg_count + 1;
          push(Value(sw));
        } else if (method_name == "ends_with") {
          bool ew = false;
          if (arg_count >= 1) {
            std::string suf = peek(0).to_string();
            if (suf.size() <= str.size()) {
              ew = (str.compare(str.size() - suf.size(), suf.size(), suf) == 0);
            }
          }
          stack_top -= arg_count + 1;
          push(Value(ew));
        } else if (method_name == "replace") {
          std::string from = (arg_count >= 1) ? peek(arg_count - 1).to_string() : "";
          std::string to = (arg_count >= 2) ? peek(arg_count - 2).to_string() : "";
          std::string res = str;
          if (!from.empty()) {
            size_t start_pos = 0;
            while ((start_pos = res.find(from, start_pos)) != std::string::npos) {
              res.replace(start_pos, from.length(), to);
              start_pos += to.length();
            }
          }
          stack_top -= arg_count + 1;
          push(Value(arena_.allocate_string(res)));
        } else if (method_name == "split") {
          std::string delim = (arg_count >= 1) ? peek(0).to_string() : " ";
          auto *list = arena_.allocate<ObjList>();
          if (delim.empty()) {
            for (char c : str) {
              list->elements.push_back(Value(arena_.allocate_string(std::string(1, c))));
            }
          } else {
            size_t start = 0;
            size_t end = str.find(delim);
            while (end != std::string::npos) {
              std::string token = str.substr(start, end - start);
              if (!token.empty() || delim != " ") {
                list->elements.push_back(Value(arena_.allocate_string(token)));
              }
              start = end + delim.length();
              end = str.find(delim, start);
            }
            std::string rem = str.substr(start);
            if (!rem.empty() || delim != " ") {
              list->elements.push_back(Value(arena_.allocate_string(rem)));
            }
          }
          stack_top -= arg_count + 1;
          push(Value(list));
        } else if (method_name == "contains") {
          bool c = (arg_count >= 1 && str.find(peek(0).to_string()) != std::string::npos);
          stack_top -= arg_count + 1;
          push(Value(c));
        } else {
          has_error = true;
          error_value = Value(arena_.allocate_string(
              "Undefined string method '" + method_name + "'"));
          break;
        }
      } else {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Only instances, modules, lists, "
                                         "dicts, and strings have methods."));
        break;
      }
      break;
    }
    // jump forward unconditionally
    case OP_JUMP:
      frames.back().ip += read_short();
      break;
    // jump ahead ONLY if condition on stack is false
    case OP_JUMP_IF_FALSE: {
      uint16_t offset = read_short();
      if (!peek(0).as_bool())
        frames.back().ip += offset;
      break;
    }
    // loop jump and rewind ip backwards
    case OP_LOOP:
      frames.back().ip -= read_short();
      break;
    // call closure or class with arg count
    case OP_CALL: {
      uint8_t arg_count = read_byte();
      call_value(peek(arg_count), arg_count);
      break;
    }
    case OP_NATIVE_CALL: {
      uint8_t arg_count = read_byte();
      Value callee = pop();
      if (callee.is_callable()) {
        Value res =
            static_cast<Callable *>(callee.get_obj())->call(this, arg_count);
        stack_top -= arg_count;
        push(res);
      } else {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Native call target is not callable"));
      }
      break;
    }
    // wrap func into closure and pull in its upvalues
    case OP_CLOSURE: {
      ObjFunction *func = static_cast<ObjFunction *>(read_constant().get_obj());
      auto *closure = arena_.allocate<ObjClosure>(func);
      if (!frames.empty() && frames.back().closure &&
          frames.back().closure->module_globals) {
        closure->module_globals = frames.back().closure->module_globals;
      } else {
        closure->module_globals = globals;
      }
      push(Value(closure));
      for (int i = 0; i < func->upvalue_count; i++) {
        uint8_t is_local = read_byte();
        uint16_t index = read_short();
        if (is_local)
          closure->upvalues[i] = capture_upvalue(frames.back().slots + index);
        else
          closure->upvalues[i] = frames.back().closure->upvalues[index];
      }
      break;
    }
    case OP_CLOSE_UPVALUE:
      close_upvalues(stack_top - 1);
      pop();
      break;
    // pop frame, close locals and hand return val back to caller
    case OP_RETURN: {
      Value res = pop();
      Value *slots = frames.back().slots;
      close_upvalues(slots);
      frames.pop_back();
      if ((int)frames.size() == target_frame_depth)
        return res;
      if (frames.empty())
        return res;
      stack_top = slots;
      push(res);
      break;
    }
    // push catch target and stack depth for error recovery
    case OP_TRY: {
      uint16_t offset = read_short();
      try_stack.push_back({frames.back().ip + offset, (int)(stack_top - stack),
                           (int)frames.size()});
      break;
    }
    case OP_END_TRY:
      try_stack.pop_back();
      break;
    // raise error and kick off stack unwind
    case OP_THROW: {
      error_value = pop();
      has_error = true;
      break;
    }
    // clone isolated vm and YEET task onto global thread pool
    case OP_SPAWN: {
      uint8_t arg_count = read_byte();
      std::vector<Value> args(arg_count);
      for (int i = arg_count - 1; i >= 0; --i)
        args[i] = pop();
      Value callee = pop();

      try {
        auto promise = std::make_shared<std::promise<std::string>>();
        std::shared_future<std::string> future = promise->get_future();
        auto *task = arena_.allocate<ObjTask>(future);
        GCRootGuard task_guard(arena_, task);

        auto worker_globals = std::make_shared<GlobalsTable>();
        auto worker = std::make_shared<VM>(worker_globals);
        worker->search_paths = this->search_paths;

        std::unordered_map<GCObject *, GCObject *> clones;
        TableCloneScope table_scope(worker->arena(), clones);
        table_scope.map_table(this->globals.get(), worker_globals);

        {
          std::shared_lock<std::shared_mutex> lock(this->globals->mutex);
          for (const auto &pair : this->globals->values) {
            worker_globals->values[pair.first] =
                pair.second.clone_val(worker->arena(), clones);
          }
        }

        for (const auto &pair : module_cache) {
          if (pair.second) {
            worker->module_cache[pair.first] = static_cast<ObjModule *>(
                pair.second->clone(worker->arena(), clones));
          }
        }

        Value isolated_callee = callee.clone_val(worker->arena(), clones);
        std::vector<Value> isolated_args;
        isolated_args.reserve(args.size());
        for (const auto &arg : args) {
          isolated_args.push_back(arg.clone_val(worker->arena(), clones));
        }

        concurrency::get_global_thread_pool().enqueue(
            [worker, isolated_callee, isolated_args, promise, arg_count]() {
              worker->push(isolated_callee);
              for (const auto &arg : isolated_args)
                worker->push(arg);

              Value res;
              if (worker->call_value(isolated_callee, arg_count)) {
                res = worker->run();
              }
              std::ostringstream ss(std::ios::binary);
              serialize_value(ss, res);
              promise->set_value(ss.str());
            });

        push(Value(task));
      } catch (const std::exception &e) {
        has_error = true;
        error_value = Value(arena_.allocate_string(e.what()));
      }
      break;
    }
    // create thread safe channel for worker comms
    case OP_CHANNEL: {
      push(Value(arena_.allocate<ObjChannel>()));
      break;
    }
    // send val through channel
    case OP_SEND: {
      Value v_val = pop();
      Value v_chan = pop();
      if (!v_chan.is_channel()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Expected channel for send."));
        break;
      }
      ObjChannel *chan = static_cast<ObjChannel *>(v_chan.get_obj());
      chan->send(v_val);
      push(v_val);
      break;
    }
    // receive val from channel or block
    case OP_RECEIVE: {
      Value v_chan = pop();
      if (!v_chan.is_channel()) {
        has_error = true;
        error_value =
            Value(arena_.allocate_string("Expected channel for receive."));
        break;
      }
      ObjChannel *chan = static_cast<ObjChannel *>(v_chan.get_obj());
      Value res = chan->receive(arena_);
      push(res);
      break;
    }
    case OP_DUP: {
      push(peek(0));
      break;
    }
    case OP_HALT:
      return (stack_top > stack) ? pop() : Value();
    // load module and bind to global namespace under name or alias
    case OP_IMPORT: {
      Value alias_val = pop();
      Value path_val = pop();
      std::string path = path_val.as_string();

      ObjModule *module = load_module(path);
      if (has_error)
        break;

      std::string name;
      if (!alias_val.is_null()) {
        name = alias_val.as_string();
      } else {
        name = fs::path(path).stem().string();
      }
      {
        std::unique_lock<std::shared_mutex> lock(globals->mutex);
        globals->values[name] = Value(module);
      }
      push(Value(module));
      break;
    }

    default: {
      has_error = true;
      std::string msg = "Unknown opcode: " + std::to_string(instruction);
      error_value = Value(arena_.allocate_string(msg));
      break;
    }
    }
  }
}

// wire up core stdlib modules and native functions into globals
void VM::setup_builtins() {
  globals->values["null"] = Value();
  ObjList *empty_args = arena_.allocate<ObjList>();
  ObjList *empty_argv = arena_.allocate<ObjList>();
  globals->values["args"] = Value(empty_args);
  globals->values["argv"] = Value(empty_argv);

  register_stdlib_math(this);
  register_stdlib_web(this);
  register_stdlib_db(this);
  register_stdlib_io(this);
  register_stdlib_nlp(this);
  register_stdlib_ui(this);
  register_stdlib_core(this);
  register_stdlib_csv(this);
  register_stdlib_archive(this);
  register_stdlib_net(this);
  register_stdlib_os(this);
}

void VM::set_cli_args(const std::vector<std::string> &args_vec) {
  ObjList *list_args = arena_.allocate<ObjList>();
  ObjList *list_argv = arena_.allocate<ObjList>();
  for (const auto &a : args_vec) {
    Value str_val(arena_.allocate_string(a));
    list_args->elements.push_back(str_val);
    list_argv->elements.push_back(str_val);
  }
  std::unique_lock<std::shared_mutex> lock(globals->mutex);
  globals->values["args"] = Value(list_args);
  globals->values["argv"] = Value(list_argv);
}

} // namespace shell_lite

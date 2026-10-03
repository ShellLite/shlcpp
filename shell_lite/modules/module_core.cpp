#include "../native_registry.hpp"
#include "../event_loop.hpp"
#include "../plugin_api.h"
#include <charconv>
#include <iostream>
#include <mutex>
#include <string_view>
#include <sstream>
#include <cmath>
#include <functional>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <vector>

#include "pal/platform.hpp"

namespace shell_lite {

static Value builtin_error(VM *vm, const std::string &msg) {
  vm->has_error = true;
  vm->error_value = Value(vm->arena().allocate_string(msg));
  return Value();
}

void register_stdlib_core(VM *vm) {
  // load native shared library and wire up plugin c-api
  NativeRegistry::register_builtin(
      vm, "import_plugin", 1, [](VM *vm, int arg_count) -> Value {
        Value path_val = vm->peek(0);
        if (!path_val.is_string()) {
          vm->has_error = true;
          vm->error_value = Value(vm->arena().allocate_string(
              "import_plugin requires a string path"));
          return Value();
        }
        std::string path = path_val.as_string();

        std::string err_out;
        void* handle = pal::load_shared_library(path, err_out);
        if (!handle) {
          vm->has_error = true;
          vm->error_value = Value(vm->arena().allocate_string(err_out));
          return Value();
        }
        auto init_func = (void (*)(shlcppAPI *, shl_vm_t))pal::get_function_pointer(handle, "shlcpp_plugin_init");
        if (!init_func) {
          init_func = (void (*)(shlcppAPI *, shl_vm_t))pal::get_function_pointer(handle, "shell_lite_plugin_init");
        }

        if (!init_func) {
          vm->has_error = true;
          vm->error_value = Value(vm->arena().allocate_string(
              "Plugin missing shlcpp_plugin_init entry point"));
          return Value();
        }

        static shlcppAPI api = {
            [](shl_vm_t v_vm, const char *name, int args, shl_native_fn func) {
              VM *real_vm = (VM *)v_vm;
              NativeRegistry::register_builtin(
                  real_vm, name, args,
                  [func](VM *inner_vm, int c_args) -> Value {
                    shl_value_t *args_array =
                        (shl_value_t *)(inner_vm->stack_top - c_args);
                    shl_value_t result =
                        func((shl_vm_t)inner_vm, c_args, args_array);
                    if (result == nullptr)
                      return Value();
                    return *(Value *)result;
                  });
            },
            [](shl_vm_t v_vm, const char *str) -> shl_value_t {
              VM *real_vm = (VM *)v_vm;
              Value *v = real_vm->allocate_ffi_value(
                  Value(real_vm->arena().allocate_string(str)));
              return (shl_value_t)v;
            },
            [](shl_vm_t v_vm, double val) -> shl_value_t {
              VM *real_vm = (VM *)v_vm;
              return (shl_value_t)real_vm->allocate_ffi_value(Value(val));
            },
            [](shl_vm_t v_vm, int val) -> shl_value_t {
              VM *real_vm = (VM *)v_vm;
              return (shl_value_t)real_vm->allocate_ffi_value(Value((bool)val));
            },
            [](shl_vm_t v_vm) -> shl_value_t {
              VM *real_vm = (VM *)v_vm;
              return (shl_value_t)real_vm->allocate_ffi_value(Value());
            },
            [](shl_value_t val) -> double {
              if (!val) return 0.0;
              Value* v = (Value*)val;
              return v->is_number() ? v->as_number() : 0.0;
            },
            [](shl_value_t val) -> const char* {
              if (!val) return "";
              Value* v = (Value*)val;
              return v->is_string() ? v->as_string().c_str() : "";
            },
            [](shl_value_t val) -> int {
              if (!val) return 0;
              Value* v = (Value*)val;
              return v->as_bool() ? 1 : 0;
            },
            [](shl_value_t val) -> int {
              return (val && ((Value*)val)->is_number()) ? 1 : 0;
            },
            [](shl_value_t val) -> int {
              return (val && ((Value*)val)->is_string()) ? 1 : 0;
            },
            [](shl_value_t val) -> int {
              return (val && ((Value*)val)->is_bool()) ? 1 : 0;
            },
            [](shl_value_t val) -> int {
              return (!val || ((Value*)val)->is_null()) ? 1 : 0;
            },
            [](shl_vm_t v_vm, shl_value_t val) {
              if (v_vm && val) {
                ((VM*)v_vm)->release_ffi_value((Value*)val);
              }
            }};

        init_func(&api, (shl_vm_t)vm);
        return Value(true);
      });

  NativeRegistry::register_builtin(
      vm, "str", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1)
          return Value(vm->arena().allocate_string(""));
        return Value(vm->arena().allocate_string(vm->peek(0).to_string()));
      });
  NativeRegistry::register_builtin(
      vm, "int", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1)
          return Value((double)0);
        Value val = vm->peek(0);
        if (val.is_number()) {
          return Value((double)(int)val.as_number());
        } else if (val.is_string()) {
          try {
            std::string s(val.as_string());
            return Value((double)std::stoll(s));
          } catch (...) {
            return Value((double)0);
          }
        } else if (val.is_bool()) {
          return Value(val.as_bool() ? (double)1 : (double)0);
        }
        return Value((double)0);
      });
  NativeRegistry::register_builtin(
      vm, "float", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1)
          return Value((double)0);
        Value val = vm->peek(0);
        if (val.is_number()) {
          return val;
        } else if (val.is_string()) {
          try {
            std::string s(val.as_string());
            return Value(std::stod(s));
          } catch (...) {
            return Value((double)0);
          }
        } else if (val.is_bool()) {
          return Value(val.as_bool() ? (double)1 : (double)0);
        }
        return Value((double)0);
      });
  NativeRegistry::register_builtin(vm, "bool", -1,
                                   [](VM *vm, int arg_count) -> Value {
                                     if (arg_count < 1)
                                       return Value(false);
                                     return Value(vm->peek(0).as_bool());
                                   });
  NativeRegistry::register_builtin(
      vm, "chr", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1)
          return Value(vm->arena().allocate_string(""));
        Value val = vm->peek(0);
        if (!val.is_number())
          return Value(vm->arena().allocate_string(""));
        char c = (char)val.as_number();
        return Value(vm->arena().allocate_string(std::string(1, c)));
      });
  NativeRegistry::register_builtin(vm, "ord", -1,
                                   [](VM *vm, int arg_count) -> Value {
                                     if (arg_count < 1)
                                       return Value((double)0);
                                     Value val = vm->peek(0);
                                     if (!val.is_string())
                                       return Value((double)0);
                                     std::string s = val.as_string();
                                     if (s.empty())
                                       return Value((double)0);
                                     return Value((double)(unsigned char)s[0]);
                                   });
  NativeRegistry::register_builtin(
      vm, "list", -1, [](VM *vm, int arg_count) -> Value {
        auto *list = vm->arena().allocate<ObjList>();
        if (arg_count > 0) {
          Value val = vm->peek(0);
          if (val.is_list()) {
            auto *other = static_cast<ObjList *>(val.get_obj());
            list->elements = other->elements;
          } else if (val.is_string()) {
            std::string s = val.as_string();
            for (char c : s) {
              list->elements.push_back(
                  Value(vm->arena().allocate_string(std::string(1, c))));
            }
          }
        }
        return Value(list);
      });
  NativeRegistry::register_builtin(
      vm, "len", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1)
          return Value((double)0);
        Value val = vm->peek(0);
        if (val.is_list()) {
          return Value(
              (double)static_cast<ObjList *>(val.get_obj())->elements.size());
        } else if (val.is_string()) {
          return Value((double)val.as_string().size());
        } else if (val.is_dict()) {
          return Value(
              (double)static_cast<ObjDict *>(val.get_obj())->elements.size());
        }
        return Value((double)0);
      });
  NativeRegistry::register_builtin(
      vm, "range", -1, [](VM *vm, int arg_count) -> Value {
        double start = 0;
        double stop = 0;
        double step = 1;
        if (arg_count == 1) {
          stop = vm->peek(0).as_number();
        } else if (arg_count == 2) {
          start = vm->peek(1).as_number();
          stop = vm->peek(0).as_number();
        } else if (arg_count >= 3) {
          start = vm->peek(arg_count - 1).as_number();
          stop = vm->peek(arg_count - 2).as_number();
          step = vm->peek(arg_count - 3).as_number();
        }
        auto *list = vm->arena().allocate<ObjList>();
        if (step > 0) {
          for (double v = start; v < stop; v += step) {
            list->elements.push_back(Value(v));
          }
        } else if (step < 0) {
          for (double v = start; v > stop; v += step) {
            list->elements.push_back(Value(v));
          }
        }
        return Value(list);
      });
  NativeRegistry::register_builtin(
      vm, "print", -1, [](VM *vm, int arg_count) -> Value {
        for (int i = arg_count - 1; i >= 0; --i) {
          std::cout << vm->peek(i).to_string() << (i == 0 ? "" : " ");
        }
        std::cout << std::endl;
        return Value();
      });
  NativeRegistry::register_builtin(
      vm, "say", -1, [](VM *vm, int arg_count) -> Value {
        for (int i = arg_count - 1; i >= 0; --i) {
          std::cout << vm->peek(i).to_string() << (i == 0 ? "" : " ");
        }
        std::cout << std::endl;
        return Value();
      });
  NativeRegistry::register_builtin(
      vm, "task_await", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 1)
          return Value();
        Value v_task = vm->peek(0);
        if (!v_task.is_task())
          return Value();
        ObjTask *task = static_cast<ObjTask *>(v_task.get_obj());

        if (!task->completed) {
          while (task->future.wait_for(std::chrono::milliseconds(2)) != std::future_status::ready) {
            EventLoop::instance().poll(0);
          }
          std::string payload = task->future.get();
          std::istringstream ss(payload, std::ios::binary);
          uint8_t status = 0;
          ss.read(reinterpret_cast<char *>(&status), 1);
          if (status == 1) {
            task->failed = true;
            task->result = deserialize_value(ss, vm->arena());
            task->completed = true;
          } else {
            task->failed = false;
            task->result = deserialize_value(ss, vm->arena());
            task->completed = true;
          }
        }
        if (task->failed) {
          vm->has_error = true;
          vm->error_value = task->result;
          return Value();
        }
        return task->result;
      });

  NativeRegistry::register_builtin(
      vm, "push", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 2)
          return Value();
        Value val = vm->peek(0);
        Value list_val = vm->peek(1);
        if (list_val.is_list()) {
          static_cast<ObjList *>(list_val.get_obj())->elements.push_back(val);
        }
        return list_val;
      });

  NativeRegistry::register_builtin(
      vm, "add", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 2)
          return Value();
        Value arg0 = vm->peek(1);
        Value arg1 = vm->peek(0);
        if (arg1.is_list()) {
          static_cast<ObjList *>(arg1.get_obj())->elements.push_back(arg0);
          return arg1;
        } else if (arg0.is_list()) {
          static_cast<ObjList *>(arg0.get_obj())->elements.push_back(arg1);
          return arg0;
        }
        return Value();
      });

  NativeRegistry::register_builtin(
      vm, "append", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 2)
          return Value();
        Value arg0 = vm->peek(1);
        Value arg1 = vm->peek(0);
        if (arg0.is_list()) {
          static_cast<ObjList *>(arg0.get_obj())->elements.push_back(arg1);
          return arg0;
        } else if (arg1.is_list()) {
          static_cast<ObjList *>(arg1.get_obj())->elements.push_back(arg0);
          return arg1;
        }
        return Value();
      });

  NativeRegistry::register_builtin(
      vm, "pop", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1)
          return Value();
        Value list_val = vm->peek(arg_count - 1);
        if (list_val.is_list()) {
          auto *list = static_cast<ObjList *>(list_val.get_obj());
          if (list->elements.empty())
            return Value();
          if (arg_count >= 2) {
            int idx = (int)vm->peek(arg_count - 2).as_number();
            if (idx < 0)
              idx += (int)list->elements.size();
            if (idx >= 0 && idx < (int)list->elements.size()) {
              Value val = list->elements[idx];
              list->elements.erase(list->elements.begin() + idx);
              return val;
            }
            return Value();
          } else {
            Value last = list->elements.back();
            list->elements.pop_back();
            return last;
          }
        }
        return Value();
      });

  NativeRegistry::register_builtin(
      vm, "contains", 2, [](VM *vm, int arg_count) -> Value {
        Value container = vm->peek(1);
        Value item = vm->peek(0);
        if (container.is_dict()) {
          auto *dict = static_cast<ObjDict *>(container.get_obj());
          return Value(dict->elements.count(item.to_string()) > 0);
        } else if (container.is_list()) {
          auto *list = static_cast<ObjList *>(container.get_obj());
          for (auto &el : list->elements) {
            if (el == item)
              return Value(true);
          }
          return Value(false);
        } else if (container.is_string()) {
          return Value(container.as_string().find(item.to_string()) !=
                       std::string::npos);
        }
        return Value(false);
      });

  NativeRegistry::register_builtin(
      vm, "split", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1)
          return Value(vm->arena().allocate<ObjList>());
        std::string str = vm->peek(arg_count - 1).to_string();
        std::string delim =
            (arg_count >= 2) ? vm->peek(arg_count - 2).to_string() : " ";
        auto *list = vm->arena().allocate<ObjList>();
        if (delim.empty()) {
          for (char c : str) {
            list->elements.push_back(
                Value(vm->arena().allocate_string(std::string(1, c))));
          }
          return Value(list);
        }
        size_t start = 0;
        size_t end = str.find(delim);
        while (end != std::string::npos) {
          std::string token = str.substr(start, end - start);
          if (!token.empty() || delim != " ") {
            list->elements.push_back(Value(vm->arena().allocate_string(token)));
          }
          start = end + delim.length();
          end = str.find(delim, start);
        }
        std::string rem = str.substr(start);
        if (!rem.empty() || delim != " ") {
          list->elements.push_back(Value(vm->arena().allocate_string(rem)));
        }
        return Value(list);
      });



  // open thread-safe channel for worker task comms
  auto chan_open_fn = [](VM *vm, int arg_count) -> Value {
    return Value(vm->arena().allocate<ObjChannel>());
  };
  NativeRegistry::register_builtin(vm, "channel", -1, chan_open_fn);
  NativeRegistry::register_builtin(vm, "chan_open", -1, chan_open_fn);

  auto chan_send_fn = [](VM *vm, int arg_count) -> Value {
    if (arg_count != 2)
      return Value();
    Value v_chan = vm->peek(1);
    Value v_val = vm->peek(0);
    if (!v_chan.is_channel()) {
      vm->has_error = true;
      vm->error_value =
          Value(vm->arena().allocate_string("Expected channel for send"));
      return Value();
    }
    ObjChannel *chan = static_cast<ObjChannel *>(v_chan.get_obj());
    chan->send_shared(v_val);
    return Value(true);
  };
  NativeRegistry::register_builtin(vm, "channel_send", -1, chan_send_fn);
  NativeRegistry::register_builtin(vm, "chan_send", -1, chan_send_fn);

  auto chan_transfer_fn = [](VM *vm, int arg_count) -> Value {
    if (arg_count != 2)
      return Value();
    Value v_chan = vm->peek(1);
    Value v_val = vm->peek(0);
    if (!v_chan.is_channel()) {
      vm->has_error = true;
      vm->error_value =
          Value(vm->arena().allocate_string("Expected channel for transfer"));
      return Value();
    }
    ObjChannel *chan = static_cast<ObjChannel *>(v_chan.get_obj());
    chan->transfer(v_val);
    return Value(true);
  };
  NativeRegistry::register_builtin(vm, "channel_transfer", -1, chan_transfer_fn);
  NativeRegistry::register_builtin(vm, "chan_transfer", -1, chan_transfer_fn);

  auto chan_recv_fn = [](VM *vm, int arg_count) -> Value {
    if (arg_count != 1)
      return Value();
    Value v_chan = vm->peek(0);
    if (!v_chan.is_channel()) {
      vm->has_error = true;
      vm->error_value =
          Value(vm->arena().allocate_string("Expected channel for receive"));
      return Value();
    }
    ObjChannel *chan = static_cast<ObjChannel *>(v_chan.get_obj());
    return chan->receive(vm->arena());
  };
  NativeRegistry::register_builtin(vm, "channel_receive", -1, chan_recv_fn);
  NativeRegistry::register_builtin(vm, "chan_recv", -1, chan_recv_fn);

  auto chan_close_fn = [](VM *vm, int arg_count) -> Value {
    if (arg_count != 1)
      return Value();
    Value v_chan = vm->peek(0);
    if (!v_chan.is_channel()) {
      vm->has_error = true;
      vm->error_value =
          Value(vm->arena().allocate_string("Expected channel for close"));
      return Value();
    }
    ObjChannel *chan = static_cast<ObjChannel *>(v_chan.get_obj());
    chan->close();
    return Value(true);
  };
  NativeRegistry::register_builtin(vm, "channel_close", -1, chan_close_fn);
  NativeRegistry::register_builtin(vm, "chan_close", -1, chan_close_fn);

  NativeRegistry::register_builtin(
      vm, "make_slice", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 3)
          return Value();
        auto *dict = vm->arena().allocate<ObjDict>();
        dict->elements["type"] = Value(vm->arena().allocate_string("slice"));
        dict->elements["start"] = vm->peek(2);
        dict->elements["stop"] = vm->peek(1);
        dict->elements["step"] = vm->peek(0);
        return Value(dict);
      });

  // wait for worker task futures and deserialize results
  NativeRegistry::register_builtin(
      vm, "gather", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 1) {
          vm->has_error = true;
          vm->error_value =
              Value(vm->arena().allocate_string("gather expects 1 argument"));
          return Value();
        }
        Value list_val = vm->peek(0);
        if (!list_val.is_list()) {
          vm->has_error = true;
          vm->error_value = Value(
              vm->arena().allocate_string("gather expects a list of tasks"));
          return Value();
        }
        auto *list = static_cast<ObjList *>(list_val.get_obj());
        auto *results = vm->arena().allocate<ObjList>();
        for (auto &item : list->elements) {
          if (item.is_task()) {
            auto *task = static_cast<ObjTask *>(item.get_obj());
            if (!task->completed) {
              while (task->future.wait_for(std::chrono::milliseconds(2)) != std::future_status::ready) {
                EventLoop::instance().poll(0);
              }
              std::string payload = task->future.get();
              std::istringstream ss(payload, std::ios::binary);
              uint8_t status = 0;
              ss.read(reinterpret_cast<char *>(&status), 1);
              if (status == 1) {
                task->failed = true;
                task->result = deserialize_value(ss, vm->arena());
                task->completed = true;
              } else {
                task->failed = false;
                task->result = deserialize_value(ss, vm->arena());
                task->completed = true;
              }
            }
            if (task->failed) {
              vm->has_error = true;
              vm->error_value = task->result;
              return Value();
            }
            results->elements.push_back(task->result);
          } else {
            results->elements.push_back(item);
          }
        }
        return Value(results);
      });

  NativeRegistry::register_builtin(
      vm, "std_convert", 2, [](VM *vm, int arg_count) -> Value {
        Value val = vm->peek(1);
        std::string target_type = vm->peek(0).to_string();
        if (target_type == "string" || target_type == "str") {
          return Value(vm->arena().allocate_string(val.to_string()));
        } else if (target_type == "int" || target_type == "integer") {
          if (val.is_number())
            return Value((double)(int)val.as_number());
          if (val.is_bool())
            return Value(val.as_bool() ? (double)1 : (double)0);
          if (val.is_string()) {
            try {
              std::string s(val.as_string());
              return Value((double)std::stoll(s));
            } catch (...) {
              return Value((double)0);
            }
          }
          return Value((double)0);
        } else if (target_type == "float" || target_type == "number" ||
                   target_type == "decimal") {
          if (val.is_number())
            return val;
          if (val.is_bool())
            return Value(val.as_bool() ? (double)1 : (double)0);
          if (val.is_string()) {
            try {
              std::string s(val.as_string());
              return Value(std::stod(s));
            } catch (...) {
              return Value((double)0);
            }
          }
          return Value((double)0);
        } else if (target_type == "bool" || target_type == "boolean") {
          return Value(val.as_bool());
        } else if (target_type == "list") {
          auto *list = vm->arena().allocate<ObjList>();
          if (val.is_list()) {
            list->elements = static_cast<ObjList *>(val.get_obj())->elements;
          } else if (val.is_string()) {
            for (char c : val.as_string()) {
              list->elements.push_back(
                  Value(vm->arena().allocate_string(std::string(1, c))));
            }
          } else {
            list->elements.push_back(val);
          }
          return Value(list);
        }
        return val;
      });
  vm->globals->values["convert"] = vm->globals->values["std_convert"];

  NativeRegistry::register_builtin(
      vm, "str", 1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 1) return Value("");
        return Value(vm->arena().allocate_string(vm->peek(0).to_string()));
      });
  NativeRegistry::register_builtin(
      vm, "string", 1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 1) return Value("");
        return Value(vm->arena().allocate_string(vm->peek(0).to_string()));
      });
  NativeRegistry::register_builtin(
      vm, "int", 1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 1) return Value(0.0);
        Value v = vm->peek(0);
        if (v.is_number()) return Value((double)(int64_t)v.as_number());
        if (v.is_string()) {
          try { return Value((double)std::stoll(v.as_string())); } catch (...) {}
        }
        return Value(0.0);
      });
  NativeRegistry::register_builtin(
      vm, "float", 1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 1) return Value(0.0);
        Value v = vm->peek(0);
        if (v.is_number()) return v;
        if (v.is_string()) {
          try { return Value(std::stod(v.as_string())); } catch (...) {}
        }
        return Value(0.0);
      });

  NativeRegistry::register_builtin(
      vm, "typeof", 1, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 1) return Value("");
        Value val = vm->peek(0);
        if (val.is_null()) return Value(vm->arena().allocate_string("null"));
        if (val.is_bool()) return Value(vm->arena().allocate_string("bool"));
        if (val.is_number()) return Value(vm->arena().allocate_string("number"));
        if (val.is_string()) return Value(vm->arena().allocate_string("string"));
        if (val.is_list()) return Value(vm->arena().allocate_string("list"));
        if (val.is_dict()) return Value(vm->arena().allocate_string("dict"));
        if (val.is_function() || val.is_closure() || val.is_callable()) return Value(vm->arena().allocate_string("function"));
        if (val.is_class()) return Value(vm->arena().allocate_string("class"));
        if (val.is_instance()) return Value(vm->arena().allocate_string("instance"));
        if (val.is_task()) return Value(vm->arena().allocate_string("task"));
        if (val.is_channel()) return Value(vm->arena().allocate_string("channel"));
        if (val.is_file()) return Value(vm->arena().allocate_string("file"));
        if (val.is_database()) return Value(vm->arena().allocate_string("database"));
        return Value(vm->arena().allocate_string("object"));
      });
  vm->globals->values["type"] = vm->globals->values["typeof"];

  NativeRegistry::register_builtin(
      vm, "assert", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1)
          return Value();
        Value cond = vm->peek(arg_count - 1);
        std::string msg = "Assertion failed";
        if (arg_count > 1) {
          msg = vm->peek(0).to_string();
        }
        if (!cond.as_bool()) {
          vm->has_error = true;
          vm->error_value = Value(vm->arena().allocate_string(msg));
        }
        return Value();
      });

  // recursive mutex with shared state so clones share the underlying OS lock
  NativeRegistry::register_builtin(
      vm, "create_lock", 0, [](VM *vm, int arg_count) -> Value {
        return Value(vm->arena().allocate<ObjLock>());
      });

  // acquire lock with scoped block and release when done
  NativeRegistry::register_builtin(
      vm, "lock_block", 2, [](VM *vm, int arg_count) -> Value {
        if (arg_count != 2) return Value();
        Value v_lock = vm->peek(1);
        Value block = vm->peek(0);
        if (v_lock.is_lock()) {
          ObjLock *lock_obj = v_lock.as_lock();
          std::lock_guard<std::recursive_mutex> lock(lock_obj->state->mutex);
          lock_obj->state->owner.store(std::this_thread::get_id(), std::memory_order_release);
          if (vm->call_value(block, 0)) {
            return vm->run((int)vm->frames.size() - 1);
          }
          return Value();
        }
        return Value();
      });

  // acquire lock and track owning thread id
  NativeRegistry::register_builtin(
      vm, "lock_acquire", 1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1) return Value(false);
        Value v = vm->peek(0);
        if (v.is_lock()) {
          ObjLock *lock_obj = v.as_lock();
          lock_obj->state->mutex.lock();
          lock_obj->state->owner.store(std::this_thread::get_id(), std::memory_order_release);
          lock_obj->state->lock_count.fetch_add(1, std::memory_order_relaxed);
          return Value(true);
        }
        return Value(false);
      });

  // release lock and verify owner matches current thread
  NativeRegistry::register_builtin(
      vm, "lock_release", 1, [](VM *vm, int arg_count) -> Value {
        if (arg_count < 1) return Value(false);
        Value v = vm->peek(0);
        if (v.is_lock()) {
          ObjLock *lock_obj = v.as_lock();
          std::thread::id current_id = std::this_thread::get_id();
          std::thread::id owner_id = lock_obj->state->owner.load(std::memory_order_acquire);
          if (owner_id != current_id) {
            vm->has_error = true;
            vm->error_value = Value(vm->arena().allocate_string("RuntimeError: Cannot release lock not owned by current thread"));
            return Value(false);
          }
          int count = lock_obj->state->lock_count.fetch_sub(1, std::memory_order_relaxed);
          if (count <= 1) {
            lock_obj->state->owner.store(std::thread::id{}, std::memory_order_release);
          }
          lock_obj->state->mutex.unlock();
          return Value(true);
        }
        return Value(false);
      });

  NativeRegistry::register_builtin(
      vm, "std_time_now", 0, [](VM *vm, int arg_count) -> Value {
        auto now = std::chrono::system_clock::now();
        double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
        return Value(seconds);
      });

  NativeRegistry::register_builtin(
      vm, "std_time_ms", 0, [](VM *vm, int arg_count) -> Value {
        auto now = std::chrono::system_clock::now();
        double ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return Value(ms);
      });

  NativeRegistry::register_builtin(
      vm, "std_time_sleep", 1, [](VM *vm, int arg_count) -> Value {
        if (arg_count >= 1) {
          double s = vm->peek(0).as_number();
          std::this_thread::sleep_for(std::chrono::milliseconds((int)(s * 1000)));
        }
        return Value();
      });

  NativeRegistry::register_builtin(
      vm, "std_time_format", -1, [](VM *vm, int arg_count) -> Value {
        double ts = 0;
        std::string fmt = "%Y-%m-%d %H:%M:%S";
        if (arg_count >= 1) ts = vm->peek(arg_count - 1).as_number();
        if (arg_count >= 2) fmt = vm->peek(arg_count - 2).to_string();
        std::time_t t = (std::time_t)ts;
        std::tm tm_buf;
        pal::localtime_safe(&t, &tm_buf);
        char buf[128];
        std::strftime(buf, sizeof(buf), fmt.c_str(), &tm_buf);
        return Value(vm->arena().allocate_string(buf));
      });

  NativeRegistry::register_builtin(
      vm, "test_block", 2, [](VM *vm, int arg_count) -> Value {
        std::string name = vm->peek(1).to_string();
        Value block = vm->peek(0);
        std::cout << "[TEST] Running: " << name << " ... ";
        if (vm->call_value(block, 0)) {
          Value res = vm->run((int)vm->frames.size() - 1);
          if (!vm->has_error) {
            std::cout << "PASSED" << std::endl;
          } else {
            std::cout << "FAILED" << std::endl;
          }
          return res;
        }
        std::cout << "FAILED" << std::endl;
        return Value();
      });



  NativeRegistry::register_builtin(
      vm, "throw", 1, [](VM *vm, int arg_count) -> Value {
        std::string msg = vm->peek(0).to_string();
        vm->has_error = true;
        vm->error_value = Value(vm->arena().allocate_string(msg));
        return Value();
      });

  NativeRegistry::register_builtin(
      vm, "xor", 2, [](VM *vm, int arg_count) -> Value {
        uint64_t a = (uint64_t)vm->peek(1).as_number();
        uint64_t b = (uint64_t)vm->peek(0).as_number();
        return Value((double)(a ^ b));
      });

  NativeRegistry::register_builtin(
      vm, "band", 2, [](VM *vm, int arg_count) -> Value {
        uint64_t a = (uint64_t)vm->peek(1).as_number();
        uint64_t b = (uint64_t)vm->peek(0).as_number();
        return Value((double)(a & b));
      });

  NativeRegistry::register_builtin(
      vm, "bor", 2, [](VM *vm, int arg_count) -> Value {
        uint64_t a = (uint64_t)vm->peek(1).as_number();
        uint64_t b = (uint64_t)vm->peek(0).as_number();
        return Value((double)(a | b));
      });

  NativeRegistry::register_builtin(
      vm, "bnot", 1, [](VM *vm, int arg_count) -> Value {
        uint64_t a = (uint64_t)vm->peek(0).as_number();
        return Value((double)(~a));
      });

  auto ask_handler = [](VM *vm, int arg_count) -> Value {
    if (arg_count >= 1) {
      std::string prompt = vm->peek(arg_count - 1).to_string();
      if (!prompt.empty()) {
        std::cout << prompt;
        std::cout.flush();
      }
    }
    std::string line;
    if (!std::getline(std::cin, line)) {
      return Value();
    }
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    return Value(vm->arena().allocate_string(line));
  };

  NativeRegistry::register_builtin(vm, "__shl_ask__", -1, ask_handler);
  NativeRegistry::register_builtin(vm, "ask", -1, ask_handler);
  NativeRegistry::register_builtin(vm, "input", -1, ask_handler);

  auto json_stringify_fn = [](VM *vm, int arg_count) -> Value {
    if (arg_count < 1) return Value(vm->arena().allocate_string("null"));
    Value v = vm->peek(arg_count - 1);
    int indent_size = (arg_count >= 2) ? (int)vm->peek(arg_count - 2).as_number() : 0;

    std::unordered_set<const void*> seen;
    auto json_escape = [](const std::string& s) -> std::string {
      std::string res = "\"";
      for (char c : s) {
        switch (c) {
          case '"': res += "\\\""; break;
          case '\\': res += "\\\\"; break;
          case '\b': res += "\\b"; break;
          case '\f': res += "\\f"; break;
          case '\n': res += "\\n"; break;
          case '\r': res += "\\r"; break;
          case '\t': res += "\\t"; break;
          default: res += c; break;
        }
      }
      res += "\"";
      return res;
    };
    std::function<std::string(Value, int)> stringify = [&](Value val, int depth) -> std::string {
      if (depth > 1000)
        throw std::runtime_error("json_stringify: nesting too deep");
      if (val.is_null()) return "null";
      if (val.is_bool()) return val.as_bool() ? "true" : "false";
      if (val.is_number()) {
        double num = val.as_number();
        if (std::floor(num) == num && !std::isinf(num) && !std::isnan(num) && std::abs(num) < 1e15) {
          return std::to_string((long long)num);
        }
        std::ostringstream ss;
        ss << num;
        return ss.str();
      }
      if (val.is_string()) return json_escape(val.as_string());
      if (val.is_list()) {
        auto *list = static_cast<ObjList*>(val.get_obj());
        if (!seen.insert(list).second)
          throw std::runtime_error("json_stringify: cyclic structure");
        std::string res;
        if (list->elements.empty()) res = "[]";
        else if (indent_size > 0) {
          std::string indent_str(depth * indent_size, ' ');
          std::string inner_indent((depth + 1) * indent_size, ' ');
          res = "[\n";
          for (size_t i = 0; i < list->elements.size(); ++i) {
            if (i > 0) res += ",\n";
            res += inner_indent + stringify(list->elements[i], depth + 1);
          }
          res += "\n" + indent_str + "]";
        } else {
          res = "[";
          for (size_t i = 0; i < list->elements.size(); ++i) {
            if (i > 0) res += ", ";
            res += stringify(list->elements[i], depth + 1);
          }
          res += "]";
        }
        seen.erase(list);
        return res;
      }
      if (val.is_dict()) {
        auto *dict = static_cast<ObjDict*>(val.get_obj());
        if (!seen.insert(dict).second)
          throw std::runtime_error("json_stringify: cyclic structure");
        std::string res;
        if (dict->elements.empty()) res = "{}";
        else if (indent_size > 0) {
          std::string indent_str(depth * indent_size, ' ');
          std::string inner_indent((depth + 1) * indent_size, ' ');
          res = "{\n";
          bool first = true;
          for (auto &pair : dict->elements) {
            if (!first) res += ",\n";
            first = false;
            res += inner_indent + json_escape(pair.first);
            res += ": " + stringify(pair.second, depth + 1);
          }
          res += "\n" + indent_str + "}";
        } else {
          res = "{";
          bool first = true;
          for (auto &pair : dict->elements) {
            if (!first) res += ", ";
            first = false;
            res += json_escape(pair.first);
            res += ": ";
            res += stringify(pair.second, depth + 1);
          }
          res += "}";
        }
        seen.erase(dict);
        return res;
      }
      return "\"" + val.to_string() + "\"";
    };
    try {
      return Value(vm->arena().allocate_string(stringify(v, 0)));
    } catch (const std::exception& e) {
      vm->has_error = true;
      vm->error_value = Value(vm->arena().allocate_string(e.what()));
      return Value();
    }
  };

  NativeRegistry::register_builtin(vm, "json_stringify", -1, json_stringify_fn);
  NativeRegistry::register_builtin(vm, "std_json_stringify", -1, json_stringify_fn);

  auto json_parse_core = [](VM *vm, const std::string &s, bool &error_out) -> Value {
    size_t pos = 0;
    error_out = false;

    auto skip_ws = [&]() {
      while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
    };
    auto peek_c = [&]() -> char {
      skip_ws();
      return (pos < s.size()) ? s[pos] : '\0';
    };
    auto match_c = [&](char expected) -> bool {
      skip_ws();
      if (pos < s.size() && s[pos] == expected) {
        pos++;
        return true;
      }
      return false;
    };
    auto parse_str = [&]() -> std::string {
      if (pos >= s.size() || s[pos] != '"') { error_out = true; return ""; }
      pos++; // consume opening quote
      std::string out;
      while (pos < s.size()) {
        char c = s[pos++];
        if (c == '"') return out;
        if (c == '\\') {
          if (pos >= s.size()) { error_out = true; return out; }
          char esc = s[pos++];
          switch (esc) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            default: out += esc; break;
          }
        } else {
          out += c;
        }
      }
      error_out = true;
      return out;
    };

    std::function<Value(int)> parse_val = [&](int depth) -> Value {
      skip_ws();
      if (pos >= s.size()) { error_out = true; return Value(); }
      if (depth > 1000) { error_out = true; return Value(); }
      char c = s[pos];
      if (c == 'n' && s.substr(pos, 4) == "null") {
        pos += 4;
        return Value();
      }
      if (c == 't' && s.substr(pos, 4) == "true") {
        pos += 4;
        return Value(true);
      }
      if (c == 'f' && s.substr(pos, 5) == "false") {
        pos += 5;
        return Value(false);
      }
      if (c == '"') {
        std::string str_val = parse_str();
        return Value(vm->arena().allocate_string(str_val));
      }
      if (c == '[') {
        pos++;
        auto *list = vm->arena().allocate<ObjList>();
        skip_ws();
        if (match_c(']')) return Value(list);
        while (true) {
          Value elem = parse_val(depth + 1);
          if (error_out) break;
          list->elements.push_back(elem);
          skip_ws();
          if (match_c(']')) break;
          if (!match_c(',')) { error_out = true; break; }
        }
        return Value(list);
      }
      if (c == '{') {
        pos++;
        auto *dict = vm->arena().allocate<ObjDict>();
        skip_ws();
        if (match_c('}')) return Value(dict);
        while (true) {
          skip_ws();
          if (peek_c() != '"') { error_out = true; break; }
          std::string key = parse_str();
          skip_ws();
          if (!match_c(':')) { error_out = true; break; }
          Value v = parse_val(depth + 1);
          if (error_out) break;
          dict->elements[key] = v;
          skip_ws();
          if (match_c('}')) break;
          if (!match_c(',')) { error_out = true; break; }
        }
        return Value(dict);
      }
      if (c == '-' || isdigit(c)) {
        size_t start = pos;
        if (s[pos] == '-') pos++;
        while (pos < s.size() && isdigit(s[pos])) pos++;
        if (pos < s.size() && s[pos] == '.') {
          pos++;
          while (pos < s.size() && isdigit(s[pos])) pos++;
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
          pos++;
          if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
          while (pos < s.size() && isdigit(s[pos])) pos++;
        }
        double num = 0;
        try {
          num = std::stod(s.substr(start, pos - start));
        } catch (...) {
          num = 0;
        }
        return Value(num);
      }
      error_out = true;
      return Value();
    };

    Value result = parse_val(0);
    skip_ws();
    if (pos < s.size()) error_out = true;
    return result;
  };

  auto json_parse_fn = [json_parse_core](VM *vm, int arg_count) -> Value {
    if (arg_count < 1) return Value();
    std::string s = vm->peek(arg_count - 1).to_string();
    bool error = false;
    Value result = json_parse_core(vm, s, error);
    if (error) {
      vm->has_error = true;
      vm->error_value = Value(vm->arena().allocate_string("Invalid JSON string"));
      return Value();
    }
    return result;
  };

  NativeRegistry::register_builtin(vm, "json_parse", -1, json_parse_fn);
  NativeRegistry::register_builtin(vm, "std_json_parse", -1, json_parse_fn);

  NativeRegistry::register_builtin(vm, "json_is_valid", 1, [json_parse_core](VM *vm, int arg_count) -> Value {
    if (arg_count < 1) return Value(false);
    std::string s = vm->peek(0).to_string();
    bool error = false;
    json_parse_core(vm, s, error);
    return Value(!error);
  });

  // string builtins
  NativeRegistry::register_builtin(vm, "str_trim", 1, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(0).to_string();
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return Value(vm->arena().allocate_string(""));
    size_t last = s.find_last_not_of(" \t\n\r");
    return Value(vm->arena().allocate_string(s.substr(first, last - first + 1)));
  });

  NativeRegistry::register_builtin(vm, "str_trim_left", 1, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(0).to_string();
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return Value(vm->arena().allocate_string(""));
    return Value(vm->arena().allocate_string(s.substr(first)));
  });

  NativeRegistry::register_builtin(vm, "str_trim_right", 1, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(0).to_string();
    size_t last = s.find_last_not_of(" \t\n\r");
    if (last == std::string::npos) return Value(vm->arena().allocate_string(""));
    return Value(vm->arena().allocate_string(s.substr(0, last + 1)));
  });

  NativeRegistry::register_builtin(vm, "str_upper", 1, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(0).to_string();
    for (char &c : s) c = (char)std::toupper(c);
    return Value(vm->arena().allocate_string(s));
  });

  NativeRegistry::register_builtin(vm, "str_lower", 1, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(0).to_string();
    for (char &c : s) c = (char)std::tolower(c);
    return Value(vm->arena().allocate_string(s));
  });

  NativeRegistry::register_builtin(vm, "str_capitalize", 1, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(0).to_string();
    if (!s.empty()) s[0] = (char)std::toupper(s[0]);
    return Value(vm->arena().allocate_string(s));
  });

  NativeRegistry::register_builtin(vm, "str_starts_with", 2, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(1).to_string();
    std::string prefix = vm->peek(0).to_string();
    return Value(s.rfind(prefix, 0) == 0);
  });

  NativeRegistry::register_builtin(vm, "str_ends_with", 2, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(1).to_string();
    std::string suffix = vm->peek(0).to_string();
    if (suffix.size() > s.size()) return Value(false);
    return Value(s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0);
  });

  NativeRegistry::register_builtin(vm, "str_replace", 3, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(2).to_string();
    std::string from = vm->peek(1).to_string();
    std::string to = vm->peek(0).to_string();
    if (from.empty()) return Value(vm->arena().allocate_string(s));
    size_t start_pos = 0;
    while ((start_pos = s.find(from, start_pos)) != std::string::npos) {
      s.replace(start_pos, from.length(), to);
      start_pos += to.length();
    }
    return Value(vm->arena().allocate_string(s));
  });

  NativeRegistry::register_builtin(vm, "str_pad_left", 3, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(2).to_string();
    int width = (int)vm->peek(1).as_number();
    std::string pad = vm->peek(0).to_string();
    if (pad.empty()) pad = " ";
    while ((int)s.size() < width) {
      s = pad + s;
    }
    return Value(vm->arena().allocate_string(s));
  });

  NativeRegistry::register_builtin(vm, "str_pad_right", 3, [](VM *vm, int arg_count) -> Value {
    std::string s = vm->peek(2).to_string();
    int width = (int)vm->peek(1).as_number();
    std::string pad = vm->peek(0).to_string();
    if (pad.empty()) pad = " ";
    while ((int)s.size() < width) {
      s = s + pad;
    }
    return Value(vm->arena().allocate_string(s));
  });

  // collection builtins
  NativeRegistry::register_builtin(vm, "dict_keys", 1, [](VM *vm, int arg_count) -> Value {
    Value val = vm->peek(0);
    auto *list = vm->arena().allocate<ObjList>();
    if (val.is_dict()) {
      auto *dict = static_cast<ObjDict*>(val.get_obj());
      for (auto &pair : dict->elements) {
        list->elements.push_back(Value(vm->arena().allocate_string(pair.first)));
      }
    }
    return Value(list);
  });

  NativeRegistry::register_builtin(vm, "dict_values", 1, [](VM *vm, int arg_count) -> Value {
    Value val = vm->peek(0);
    auto *list = vm->arena().allocate<ObjList>();
    if (val.is_dict()) {
      auto *dict = static_cast<ObjDict*>(val.get_obj());
      for (auto &pair : dict->elements) {
        list->elements.push_back(pair.second);
      }
    }
    return Value(list);
  });

  NativeRegistry::register_builtin(vm, "dict_has", 2, [](VM *vm, int arg_count) -> Value {
    Value dict_val = vm->peek(1);
    std::string key = vm->peek(0).to_string();
    if (dict_val.is_dict()) {
      auto *dict = static_cast<ObjDict*>(dict_val.get_obj());
      return Value(dict->elements.count(key) > 0);
    }
    return Value(false);
  });

  NativeRegistry::register_builtin(vm, "list_reverse", 1, [](VM *vm, int arg_count) -> Value {
    Value val = vm->peek(0);
    auto *res = vm->arena().allocate<ObjList>();
    if (val.is_list()) {
      auto *list = static_cast<ObjList*>(val.get_obj());
      res->elements = list->elements;
      std::reverse(res->elements.begin(), res->elements.end());
    }
    return Value(res);
  });

  NativeRegistry::register_builtin(vm, "list_sort", 1, [](VM *vm, int arg_count) -> Value {
    Value val = vm->peek(0);
    auto *res = vm->arena().allocate<ObjList>();
    if (val.is_list()) {
      auto *list = static_cast<ObjList*>(val.get_obj());
      res->elements = list->elements;
      std::sort(res->elements.begin(), res->elements.end(), [](const Value &a, const Value &b) {
        if (a.is_number() && b.is_number()) {
          double na = a.as_number();
          double nb = b.as_number();
          if (std::isnan(na)) return false;
          if (std::isnan(nb)) return true;
          return na < nb;
        }
        if (a.is_string() && b.is_string()) return a.as_string() < b.as_string();
        return a.to_string() < b.to_string();
      });
    }
    return Value(res);
  });

  NativeRegistry::register_builtin(
      vm, "deepcopy", 1, [](VM *vm, int arg_count) -> Value {
        std::unordered_map<GCObject *, GCObject *> clones;
        return vm->peek(0).clone_val(vm->arena(), clones);
      });

  NativeRegistry::register_builtin(
      vm, "getattr", 2, [](VM *vm, int arg_count) -> Value {
        Value obj = vm->peek(1);
        Value name_val = vm->peek(0);
        if (!obj.is_instance())
          return builtin_error(vm, "getattr() first argument must be an instance");
        if (!name_val.is_string())
          return builtin_error(vm, "getattr() attribute name must be a string");
        auto *inst = static_cast<ObjInstance *>(obj.get_obj());
        std::string name = name_val.as_string();
        auto fit = inst->fields.find(name);
        if (fit != inst->fields.end())
          return fit->second;
        auto mit = inst->klass->methods.find(name);
        if (mit != inst->klass->methods.end())
          return vm->make_bound_method(inst, mit->second);
        return builtin_error(vm, "attribute '" + name + "' not found");
      });

  NativeRegistry::register_builtin(
      vm, "setattr", 3, [](VM *vm, int arg_count) -> Value {
        Value obj = vm->peek(2);
        Value name_val = vm->peek(1);
        Value val = vm->peek(0);
        if (!obj.is_instance())
          return builtin_error(vm, "setattr() first argument must be an instance");
        if (!name_val.is_string())
          return builtin_error(vm, "setattr() attribute name must be a string");
        static_cast<ObjInstance *>(obj.get_obj())->fields[name_val.as_string()] = val;
        return Value();
      });

  NativeRegistry::register_builtin(
      vm, "hasattr", 2, [](VM *vm, int arg_count) -> Value {
        Value obj = vm->peek(1);
        Value name_val = vm->peek(0);
        if (!name_val.is_string())
          return builtin_error(vm, "hasattr() attribute name must be a string");
        if (!obj.is_instance())
          return Value(false);
        auto *inst = static_cast<ObjInstance *>(obj.get_obj());
        std::string name = name_val.as_string();
        if (inst->fields.find(name) != inst->fields.end())
          return Value(true);
        return Value(inst->klass->methods.find(name) != inst->klass->methods.end());
      });

  NativeRegistry::register_builtin(
      vm, "dir", 1, [](VM *vm, int arg_count) -> Value {
        Value obj = vm->peek(0);
        auto *res = vm->arena().allocate<ObjList>();
        if (obj.is_instance()) {
          auto *inst = static_cast<ObjInstance *>(obj.get_obj());
          std::vector<std::string> names;
          for (auto &p : inst->fields)
            names.push_back(p.first);
          for (auto &p : inst->klass->methods)
            names.push_back(p.first);
          std::sort(names.begin(), names.end());
          names.erase(std::unique(names.begin(), names.end()), names.end());
          for (auto &n : names)
            res->elements.push_back(Value(vm->arena().allocate_string(n)));
        }
        return Value(res);
      });

  NativeRegistry::register_builtin(
      vm, "id", 1, [](VM *vm, int arg_count) -> Value {
        Value v = vm->peek(0);
        if (!v.is_obj() || !v.get_obj())
          return builtin_error(vm, "id() requires an object");
        return Value((double)(uintptr_t)v.get_obj());
      });

  NativeRegistry::register_builtin(
      vm, "repr", 1, [](VM *vm, int arg_count) -> Value {
        return Value(vm->arena().allocate_string(vm->peek(0).to_string()));
      });

  NativeRegistry::register_builtin(
      vm, "zip", -1, [](VM *vm, int arg_count) -> Value {
        for (int i = 0; i < arg_count; ++i)
          if (!vm->peek(i).is_list())
            return builtin_error(vm, "zip() arguments must be lists");
        auto *res = vm->arena().allocate<ObjList>();
        if (arg_count == 0)
          return Value(res);
        size_t n = static_cast<size_t>(-1);
        for (int i = 0; i < arg_count; ++i)
          n = (std::min)(n, static_cast<ObjList *>(vm->peek(i).get_obj())->elements.size());
        for (size_t k = 0; k < n; ++k) {
          auto *row = vm->arena().allocate<ObjList>();
          for (int i = arg_count - 1; i >= 0; --i)
            row->elements.push_back(
                static_cast<ObjList *>(vm->peek(i).get_obj())->elements[k]);
          res->elements.push_back(Value(row));
        }
        return Value(res);
      });

  NativeRegistry::register_builtin(
      vm, "enumerate", 1, [](VM *vm, int arg_count) -> Value {
        Value v = vm->peek(0);
        if (!v.is_list())
          return builtin_error(vm, "enumerate() argument must be a list");
        auto *res = vm->arena().allocate<ObjList>();
        auto &elems = static_cast<ObjList *>(v.get_obj())->elements;
        for (size_t i = 0; i < elems.size(); ++i) {
          auto *pair = vm->arena().allocate<ObjList>();
          pair->elements.push_back(Value((double)i));
          pair->elements.push_back(elems[i]);
          res->elements.push_back(Value(pair));
        }
        return Value(res);
      });

  NativeRegistry::register_builtin(
      vm, "reversed", 1, [](VM *vm, int arg_count) -> Value {
        Value v = vm->peek(0);
        if (!v.is_list())
          return builtin_error(vm, "reversed() argument must be a list");
        auto *res = vm->arena().allocate<ObjList>();
        auto &elems = static_cast<ObjList *>(v.get_obj())->elements;
        for (auto it = elems.rbegin(); it != elems.rend(); ++it)
          res->elements.push_back(*it);
        return Value(res);
      });

  NativeRegistry::register_builtin(
      vm, "any", 1, [](VM *vm, int arg_count) -> Value {
        Value v = vm->peek(0);
        if (!v.is_list())
          return builtin_error(vm, "any() argument must be a list");
        for (auto &e : static_cast<ObjList *>(v.get_obj())->elements)
          if (e.as_bool())
            return Value(true);
        return Value(false);
      });

  NativeRegistry::register_builtin(
      vm, "all", 1, [](VM *vm, int arg_count) -> Value {
        Value v = vm->peek(0);
        if (!v.is_list())
          return builtin_error(vm, "all() argument must be a list");
        for (auto &e : static_cast<ObjList *>(v.get_obj())->elements)
          if (!e.as_bool())
            return Value(false);
        return Value(true);
      });

  NativeRegistry::register_builtin(
      vm, "hex", 1, [](VM *vm, int arg_count) -> Value {
        Value v = vm->peek(0);
        if (!v.is_number())
          return builtin_error(vm, "hex() requires a number");
        double d = v.as_number();
        if (!std::isfinite(d) || std::floor(d) != d ||
            d >= 9.007199254740992e15 || d <= -9.007199254740992e15)
          return builtin_error(vm, "hex() requires a finite integer-valued number");
        long long n = (long long)d;
        char buf[32];
        if (n < 0)
          snprintf(buf, sizeof(buf), "-0x%llx",
                   (unsigned long long)(-(n + 1)) + 1);
        else
          snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)n);
        return Value(vm->arena().allocate_string(buf));
      });

  NativeRegistry::register_builtin(
      vm, "bin", 1, [](VM *vm, int arg_count) -> Value {
        Value v = vm->peek(0);
        if (!v.is_number())
          return builtin_error(vm, "bin() requires a number");
        double d = v.as_number();
        if (!std::isfinite(d) || std::floor(d) != d ||
            d >= 9.007199254740992e15 || d <= -9.007199254740992e15)
          return builtin_error(vm, "bin() requires a finite integer-valued number");
        long long n = (long long)d;
        unsigned long long u =
            n < 0 ? (unsigned long long)(-(n + 1)) + 1 : (unsigned long long)n;
        std::string s = n < 0 ? "-0b" : "0b";
        if (u == 0) {
          s += "0";
        } else {
          std::string bits;
          while (u) {
            bits.push_back((u & 1) ? '1' : '0');
            u >>= 1;
          }
          std::reverse(bits.begin(), bits.end());
          s += bits;
        }
        return Value(vm->arena().allocate_string(s));
      });

  NativeRegistry::register_builtin(
      vm, "divmod", 2, [](VM *vm, int arg_count) -> Value {
        Value av = vm->peek(1);
        Value bv = vm->peek(0);
        if (!av.is_number() || !bv.is_number())
          return builtin_error(vm, "divmod() requires numbers");
        double a = av.as_number();
        double b = bv.as_number();
        if (b == 0)
          return builtin_error(vm, "Division by zero");
        double q = std::floor(a / b);
        auto *res = vm->arena().allocate<ObjList>();
        res->elements.push_back(Value(q));
        res->elements.push_back(Value(a - q * b));
        return Value(res);
      });
}

} // namespace shell_lite

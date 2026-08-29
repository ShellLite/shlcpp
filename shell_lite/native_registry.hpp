#pragma once
#include "vm.hpp"
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace shell_lite {

template <typename T> struct is_optional : std::false_type {
  using value_type = T;
};

template <typename T> struct is_optional<std::optional<T>> : std::true_type {
  using value_type = T;
};

template <typename T> inline std::string expected_type_name() {
  using CleanT = std::decay_t<T>;
  if constexpr (is_optional<CleanT>::value) {
    return expected_type_name<typename is_optional<CleanT>::value_type>();
  } else if constexpr (std::is_same_v<CleanT, double> ||
                       std::is_same_v<CleanT, float>) {
    return "Number";
  } else if constexpr (std::is_same_v<CleanT, int> ||
                       std::is_same_v<CleanT, int64_t> ||
                       std::is_same_v<CleanT, size_t>) {
    return "Integer";
  } else if constexpr (std::is_same_v<CleanT, bool>) {
    return "Boolean";
  } else if constexpr (std::is_same_v<CleanT, std::string> ||
                       std::is_same_v<CleanT, std::string_view> ||
                       std::is_same_v<CleanT, const char *>) {
    return "String";
  } else if constexpr (std::is_same_v<CleanT, Value>) {
    return "Any";
  } else {
    return "Object";
  }
}

inline std::string actual_type_name(const Value &v) {
  if (v.is_null())
    return "Null";
  if (v.is_number())
    return "Number";
  if (v.is_bool())
    return "Boolean";
  if (v.is_string())
    return "String";
  if (v.is_list())
    return "List";
  if (v.is_dict())
    return "Dict";
  if (v.is_function() || v.is_closure())
    return "Function";
  if (v.is_class())
    return "Class";
  if (v.is_instance())
    return "Instance";
  return "Object";
}

template <typename T>
T extract_arg(VM *vm, int arg_idx, int total_args, const std::string &fn_name) {
  using CleanT = std::decay_t<T>;

  if constexpr (is_optional<CleanT>::value) {
    using InnerT = typename is_optional<CleanT>::value_type;
    if (arg_idx >= total_args) {
      return std::nullopt;
    }
    int stack_idx = total_args - 1 - arg_idx;
    Value v = vm->peek(stack_idx);
    if (v.is_null()) {
      return std::nullopt;
    }
    return extract_arg<InnerT>(vm, arg_idx, total_args, fn_name);
  } else {
    int stack_idx = total_args - 1 - arg_idx;
    Value v = vm->peek(stack_idx);

    if constexpr (std::is_same_v<CleanT, double> ||
                  std::is_same_v<CleanT, float>) {
      if (!v.is_number()) {
        vm->has_error = true;
        vm->error_value = Value(vm->arena().allocate_string(
            "TypeError: Expected 'Number' for argument " +
            std::to_string(arg_idx + 1) + " in '" + fn_name + "', got '" +
            actual_type_name(v) + "'"));
        return 0.0;
      }
      return static_cast<CleanT>(v.as_number());
    } else if constexpr (std::is_same_v<CleanT, int> ||
                         std::is_same_v<CleanT, int64_t> ||
                         std::is_same_v<CleanT, size_t>) {
      if (!v.is_number()) {
        vm->has_error = true;
        vm->error_value = Value(vm->arena().allocate_string(
            "TypeError: Expected 'Integer' for argument " +
            std::to_string(arg_idx + 1) + " in '" + fn_name + "', got '" +
            actual_type_name(v) + "'"));
        return 0;
      }
      return static_cast<CleanT>(v.as_number());
    } else if constexpr (std::is_same_v<CleanT, bool>) {
      return v.as_bool();
    } else if constexpr (std::is_same_v<CleanT, std::string>) {
      return v.to_string();
    } else if constexpr (std::is_same_v<CleanT, Value>) {
      return v;
    } else {
      return v;
    }
  }
}

inline Value make_value(VM *vm, double v) { return Value(v); }
inline Value make_value(VM *vm, float v) { return Value((double)v); }
inline Value make_value(VM *vm, int v) { return Value((double)v); }
inline Value make_value(VM *vm, int64_t v) { return Value((double)v); }
inline Value make_value(VM *vm, bool v) { return Value(v); }
inline Value make_value(VM *vm, const std::string &v) {
  return Value(vm->arena().allocate_string(v));
}
inline Value make_value(VM *vm, const char *v) {
  return Value(vm->arena().allocate_string(std::string(v)));
}
inline Value make_value(VM *vm, Value v) { return v; }
inline Value make_value(VM *vm, GCObject *v) { return Value(v); }

// --- Function Traits ---
template <typename T>
struct function_traits : public function_traits<decltype(&T::operator())> {};

template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) const> {
  using Ret = ReturnType;
  using args_tuple = std::tuple<Args...>;
  static constexpr std::size_t arity = sizeof...(Args);
};

template <typename ReturnType, typename... Args>
struct function_traits<ReturnType (*)(Args...)> {
  using Ret = ReturnType;
  using args_tuple = std::tuple<Args...>;
  static constexpr std::size_t arity = sizeof...(Args);
};

template <typename... Args> struct arity_calculator;

template <> struct arity_calculator<> {
  static constexpr size_t min_arity = 0;
  static constexpr size_t max_arity = 0;
};

template <typename First, typename... Rest>
struct arity_calculator<First, Rest...> {
  static constexpr bool is_opt = is_optional<std::decay_t<First>>::value;
  static constexpr size_t min_arity =
      (is_opt ? 0 : 1) + arity_calculator<Rest...>::min_arity;
  static constexpr size_t max_arity = 1 + arity_calculator<Rest...>::max_arity;
};

template <typename Tuple> struct tuple_arity;

template <typename... Args> struct tuple_arity<std::tuple<Args...>> {
  static constexpr size_t min_arity = arity_calculator<Args...>::min_arity;
  static constexpr size_t max_arity = arity_calculator<Args...>::max_arity;
};

template <typename Func, typename Ret, typename... Args, std::size_t... Is>
Value invoke_helper(VM *vm, int arg_count, const std::string &fn_name,
                    Func &func, std::tuple<Args...> &,
                    std::index_sequence<Is...>) {
  try {
    if constexpr (std::is_void_v<Ret>) {
      func(extract_arg<std::decay_t<Args>>(vm, (int)Is, arg_count, fn_name)...);
      if (vm->has_error)
        return Value();
      return Value();
    } else {
      auto res = func(
          extract_arg<std::decay_t<Args>>(vm, (int)Is, arg_count, fn_name)...);
      if (vm->has_error)
        return Value();
      return make_value(vm, res);
    }
  } catch (const std::exception &e) {
    vm->has_error = true;
    vm->error_value = Value(vm->arena().allocate_string(e.what()));
    return Value();
  }
}

template <typename Func> class NativeWrapper : public Callable {
  int min_args_;
  int max_args_;
  std::string name_;
  Func func_;

public:
  NativeWrapper(int expected_args, Func f)
      : min_args_(expected_args), max_args_(expected_args), name_("native"),
        func_(std::move(f)) {}

  NativeWrapper(int min_args, int max_args, std::string name, Func f)
      : min_args_(min_args), max_args_(max_args), name_(std::move(name)),
        func_(std::move(f)) {}

  Value call(VM *vm, int arg_count) override {
    if (min_args_ != -1 &&
        (arg_count < min_args_ || (max_args_ != -1 && arg_count > max_args_))) {
      vm->has_error = true;
      std::string msg;
      if (min_args_ == max_args_) {
        msg = "Expected " + std::to_string(min_args_) + " argument" +
              (min_args_ == 1 ? "" : "s") + " in '" + name_ + "', got " +
              std::to_string(arg_count);
      } else {
        msg = "Expected between " + std::to_string(min_args_) + " and " +
              std::to_string(max_args_) + " arguments in '" + name_ +
              "', got " + std::to_string(arg_count);
      }
      vm->error_value = Value(vm->arena().allocate_string(msg));
      return Value();
    }
    return func_(vm, arg_count);
  }
};

class NativeRegistry {
public:
  static void register_builtin(VM *vm, const std::string &name,
                               int expected_args,
                               std::function<Value(VM *, int)> func) {
    vm->globals->values[name] =
        Value(vm->arena().allocate<NativeWrapper<decltype(func)>>(
            expected_args, expected_args, name, std::move(func)));
  }

  template <typename Func>
  static void bind(VM *vm, const std::string &name, Func func) {
    using Traits = function_traits<Func>;
    using Ret = typename Traits::Ret;
    using ArgsTuple = typename Traits::args_tuple;

    constexpr int min_args =
        static_cast<int>(tuple_arity<ArgsTuple>::min_arity);
    constexpr int max_args =
        static_cast<int>(tuple_arity<ArgsTuple>::max_arity);

    std::function<Value(VM *, int)> wrapper =
        [func, name](VM *inner_vm, int arg_count) -> Value {
      auto f = func;
      ArgsTuple args;
      return invoke_helper<Func, Ret>(
          inner_vm, arg_count, name, f, args,
          std::make_index_sequence<Traits::arity>{});
    };

    vm->globals->values[name] =
        Value(vm->arena().allocate<NativeWrapper<decltype(wrapper)>>(
            min_args, max_args, name, wrapper));
  }
};

void register_stdlib_math(VM *vm);
void register_stdlib_web(VM *vm);
void register_stdlib_db(VM *vm);
void register_stdlib_io(VM *vm);
void register_stdlib_nlp(VM *vm);
void register_stdlib_ui(VM *vm);
void register_stdlib_core(VM *vm);
void register_stdlib_csv(VM *vm);
void register_stdlib_archive(VM *vm);
void register_stdlib_net(VM *vm);
void register_stdlib_os(VM *vm);

} // namespace shell_lite

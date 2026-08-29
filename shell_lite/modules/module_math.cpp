#include "../native_registry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>

namespace shell_lite {
// Declarative stdlib math registrations

static std::mt19937 &get_math_rng() {
  static thread_local std::mt19937 rng(std::random_device{}());
  return rng;
}

double math_max_fn(double a, double b) { return (std::max)(a, b); }
double math_min_fn(double a, double b) { return (std::min)(a, b); }
double math_clamp_fn(double val, double min_v, double max_v) {
  if (min_v > max_v)
    std::swap(min_v, max_v);
  return (std::max)(min_v, (std::min)(val, max_v));
}
double math_lerp_fn(double a, double b, double t) { return a + t * (b - a); }
double math_abs_fn(double a) { return std::abs(a); }
double math_floor_fn(double a) { return std::floor(a); }
double math_ceil_fn(double a) { return std::ceil(a); }
double math_sqrt_fn(double a) { return std::sqrt(a); }
double math_round_fn(double a) { return std::round(a); }

double math_sin_fn(double a) { return std::sin(a); }
double math_cos_fn(double a) { return std::cos(a); }
double math_tan_fn(double a) { return std::tan(a); }
double math_asin_fn(double a) { return std::asin(a); }
double math_acos_fn(double a) { return std::acos(a); }
double math_atan_fn(double a) { return std::atan(a); }
double math_atan2_fn(double y, double x) { return std::atan2(y, x); }
double math_log_fn(double a) { return std::log(a); }
double math_log10_fn(double a) { return std::log10(a); }
double math_log2_fn(double a) { return std::log2(a); }
double math_exp_fn(double a) { return std::exp(a); }
double math_pow_fn(double a, double b) { return std::pow(a, b); }

void register_stdlib_math(VM *vm) {
  NativeRegistry::bind(vm, "math_max", math_max_fn);
  NativeRegistry::bind(vm, "math_min", math_min_fn);
  NativeRegistry::bind(vm, "math_clamp", math_clamp_fn);
  NativeRegistry::bind(vm, "math_lerp", math_lerp_fn);
  NativeRegistry::bind(vm, "math_abs", math_abs_fn);
  NativeRegistry::bind(vm, "math_floor", math_floor_fn);
  NativeRegistry::bind(vm, "math_ceil", math_ceil_fn);
  NativeRegistry::bind(vm, "math_sqrt", math_sqrt_fn);
  NativeRegistry::bind(vm, "math_round", math_round_fn);
  NativeRegistry::bind(vm, "abs", math_abs_fn);
  NativeRegistry::bind(vm, "round", math_round_fn);
  NativeRegistry::bind(vm, "floor", math_floor_fn);
  NativeRegistry::bind(vm, "ceil", math_ceil_fn);
  NativeRegistry::bind(vm, "sqrt", math_sqrt_fn);

  NativeRegistry::bind(vm, "math_sin", math_sin_fn);
  NativeRegistry::bind(vm, "math_cos", math_cos_fn);
  NativeRegistry::bind(vm, "math_tan", math_tan_fn);
  NativeRegistry::bind(vm, "math_asin", math_asin_fn);
  NativeRegistry::bind(vm, "math_acos", math_acos_fn);
  NativeRegistry::bind(vm, "math_atan", math_atan_fn);
  NativeRegistry::bind(vm, "math_atan2", math_atan2_fn);
  NativeRegistry::bind(vm, "sin", math_sin_fn);
  NativeRegistry::bind(vm, "cos", math_cos_fn);
  NativeRegistry::bind(vm, "tan", math_tan_fn);
  NativeRegistry::bind(vm, "asin", math_asin_fn);
  NativeRegistry::bind(vm, "acos", math_acos_fn);
  NativeRegistry::bind(vm, "atan", math_atan_fn);
  NativeRegistry::bind(vm, "atan2", math_atan2_fn);

  NativeRegistry::bind(vm, "math_log", math_log_fn);
  NativeRegistry::bind(vm, "math_log10", math_log10_fn);
  NativeRegistry::bind(vm, "math_log2", math_log2_fn);
  NativeRegistry::bind(vm, "math_exp", math_exp_fn);
  NativeRegistry::bind(vm, "math_pow", math_pow_fn);
  NativeRegistry::bind(vm, "log", math_log_fn);
  NativeRegistry::bind(vm, "log10", math_log10_fn);
  NativeRegistry::bind(vm, "log2", math_log2_fn);
  NativeRegistry::bind(vm, "exp", math_exp_fn);
  NativeRegistry::bind(vm, "pow", math_pow_fn);

  // Mathematical Constants
  vm->globals->values["PI"] = Value(3.14159265358979323846);
  vm->globals->values["E"] = Value(2.71828182845904523536);
  vm->globals->values["TAU"] = Value(6.28318530717958647692);
  vm->globals->values["INF"] = Value(std::numeric_limits<double>::infinity());

  NativeRegistry::register_builtin(
      vm, "randint", -1, [](VM *vm, int arg_count) -> Value {
        if (arg_count >= 2) {
          long long min_v = (long long)vm->peek(arg_count - 1).as_number();
          long long max_v = (long long)vm->peek(arg_count - 2).as_number();
          if (min_v > max_v)
            std::swap(min_v, max_v);
          std::uniform_int_distribution<long long> dist(min_v, max_v);
          return Value((double)dist(get_math_rng()));
        }
        return Value((double)0);
      });
  NativeRegistry::register_builtin(
      vm, "random", -1, [](VM *vm, int arg_count) -> Value {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return Value(dist(get_math_rng()));
      });
}

} // namespace shell_lite

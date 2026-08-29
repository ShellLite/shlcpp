#pragma once

#include "error_context.hpp"
#include <functional>
#include <string>
#include <vector>

namespace shell_lite {

struct Value;

class ErrorReporter {
public:
  static void report(const shlcppError &err);
  static void report(const Value &val, const SourceLocation &loc = {}, const std::vector<std::string> &backtrace = {});
  static void set_sink(std::function<void(const shlcppError&)> sink);
};

} // namespace shell_lite

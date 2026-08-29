#include "error_reporter.hpp"
#include "value.hpp"
#include <iostream>

namespace shell_lite {

static std::function<void(const shlcppError&)> g_error_sink = [](const shlcppError& err) {
    std::cerr << "Error: " << err.formatted() << std::endl;
};

void ErrorReporter::report(const shlcppError &err) {
    if (g_error_sink) {
        g_error_sink(err);
    }
}

void ErrorReporter::report(const Value &val, const SourceLocation &loc, const std::vector<std::string> &backtrace) {
    RuntimeError err(val.to_string(), loc);
    err.backtrace_frames = backtrace;
    report(err);
}

void ErrorReporter::set_sink(std::function<void(const shlcppError&)> sink) {
    g_error_sink = std::move(sink);
}

} // namespace shell_lite

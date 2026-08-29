#ifndef _WIN32
#include "pal/platform.hpp"
#include <filesystem>
#include <cstdlib>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif
#include <dlfcn.h>

namespace fs = std::filesystem;

namespace shell_lite {
namespace pal {

std::string get_executable_path() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  if (size > 0) {
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) == 0) {
      return fs::path(path.data()).parent_path().string();
    }
  }
#else
  std::error_code ec;
  fs::path p = fs::canonical("/proc/self/exe", ec);
  if (!ec) {
    return p.parent_path().string();
  }
#endif
  return "";
}

char get_path_delim() {
  return ':';
}

void localtime_safe(const time_t* timer, struct tm* buf) {
  localtime_r(timer, buf);
}

std::string get_os_name() {
#if defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

void clear_console() {
  // Empty or ANSI escape code can be used for Linux
}

double execute_process(const std::string& cmd) {
  return (double)std::system(cmd.c_str());
}

void* load_shared_library(const std::string& path, std::string& err_out) {
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
      err_out = dlerror();
  }
  return handle;
}

void* get_function_pointer(void* handle, const std::string& func_name) {
  return dlsym(handle, func_name.c_str());
}

bool set_cursor_pos(double x, double y, std::string& err_out) {
  err_out = "GUI automation is only supported on Windows";
  return false;
}

bool send_text_input(const std::string& text, std::string& err_out) {
  err_out = "GUI automation is only supported on Windows";
  return false;
}

bool send_key_input(const std::string& key, std::string& err_out) {
  err_out = "GUI automation is only supported on Windows";
  return false;
}

} // namespace pal
} // namespace shell_lite
#endif

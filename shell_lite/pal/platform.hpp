#pragma once

#include <string>
#include <ctime>

namespace shell_lite {
namespace pal {

// Filesystem/Paths
std::string get_executable_path();
char get_path_delim();

// Time
void localtime_safe(const time_t* timer, struct tm* buf);

// OS/Process
std::string get_os_name();
void clear_console();
double execute_process(const std::string& cmd);

// Dynamic Libraries
void* load_shared_library(const std::string& path, std::string& err_out);
void* get_function_pointer(void* handle, const std::string& func_name);

// GUI Automation
bool set_cursor_pos(double x, double y, std::string& err_out);
bool send_text_input(const std::string& text, std::string& err_out);
bool send_key_input(const std::string& key, std::string& err_out);

} // namespace pal
} // namespace shell_lite

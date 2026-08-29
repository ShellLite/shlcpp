#ifdef _WIN32
#include "pal/platform.hpp"
#include <windows.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace shell_lite {
namespace pal {

std::string get_executable_path() {
  char path[MAX_PATH];
  DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
  if (len > 0) {
    return fs::path(std::string(path, len)).parent_path().string();
  }
  return "";
}

char get_path_delim() {
  return ';';
}

void localtime_safe(const time_t* timer, struct tm* buf) {
  localtime_s(buf, timer);
}

std::string get_os_name() {
  return "windows";
}

void clear_console() {
  std::system("cls");
}

double execute_process(const std::string& cmd) {
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  std::vector<char> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back('\0');

  if (!CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
      return (double)std::system(cmd.c_str());
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return (double)exit_code;
}

void* load_shared_library(const std::string& path, std::string& err_out) {
  HMODULE handle = LoadLibraryA(path.c_str());
  if (!handle) err_out = "Failed to load plugin: " + path;
  return (void*)handle;
}

void* get_function_pointer(void* handle, const std::string& func_name) {
  return (void*)GetProcAddress((HMODULE)handle, func_name.c_str());
}

bool set_cursor_pos(double x, double y, std::string& err_out) {
  SetCursorPos((int)x, (int)y);
  INPUT inputs[2] = {};
  inputs[0].type = INPUT_MOUSE;
  inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
  inputs[1].type = INPUT_MOUSE;
  inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
  SendInput(2, inputs, sizeof(INPUT));
  return true;
}

bool send_text_input(const std::string& text, std::string& err_out) {
  for (char c : text) {
      INPUT inputs[2] = {};
      inputs[0].type = INPUT_KEYBOARD;
      inputs[0].ki.wScan = static_cast<WCHAR>(static_cast<unsigned char>(c));
      inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
      inputs[1].type = INPUT_KEYBOARD;
      inputs[1].ki.wScan = static_cast<WCHAR>(static_cast<unsigned char>(c));
      inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
      SendInput(2, inputs, sizeof(INPUT));
  }
  return true;
}

bool send_key_input(const std::string& key, std::string& err_out) {
  int vk = 0;
  if (key == "enter" || key == "Enter") vk = VK_RETURN;
  else if (key == "tab" || key == "Tab") vk = VK_TAB;
  else if (key == "space" || key == "Space") vk = VK_SPACE;
  else if (key == "backspace" || key == "Backspace") vk = VK_BACK;
  else if (key == "shift" || key == "Shift") vk = VK_SHIFT;
  else if (key == "ctrl" || key == "Ctrl") vk = VK_CONTROL;
  else if (key == "alt" || key == "Alt") vk = VK_MENU;
  else if (key == "esc" || key == "Esc") vk = VK_ESCAPE;
  else if (key.length() == 1) vk = VkKeyScanA(key[0]) & 0xFF;

  if (vk != 0) {
      INPUT inputs[2] = {};
      inputs[0].type = INPUT_KEYBOARD;
      inputs[0].ki.wVk = (WORD)vk;
      inputs[1].type = INPUT_KEYBOARD;
      inputs[1].ki.wVk = (WORD)vk;
      inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
      SendInput(2, inputs, sizeof(INPUT));
      return true;
  }
  err_out = "Unknown key: " + key;
  return false;
}

} // namespace pal
} // namespace shell_lite
#endif

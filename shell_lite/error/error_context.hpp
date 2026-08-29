#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace shell_lite {

enum class ErrorKind {
  SyntaxError,
  CompileError,
  RuntimeError,
  ModuleLoadError,
  ConcurrencyError,
  DatabaseError,
  IOError,
};

struct SourceLocation {
  std::string file;
  int line = 0;
  int col = 0;
  std::string source_line;
  std::string suggestion;

  SourceLocation() = default;
  SourceLocation(std::string f, int l = 0, int c = 0, std::string sl = "", std::string sugg = "")
      : file(std::move(f)), line(l), col(c), source_line(std::move(sl)), suggestion(std::move(sugg)) {}
};

inline std::string extract_source_line(std::string_view source, int target_line) {
  if (target_line <= 0 || source.empty()) return "";
  int current_line = 1;
  size_t start = 0;
  for (size_t i = 0; i <= source.size(); ++i) {
    if (i == source.size() || source[i] == '\n') {
      if (current_line == target_line) {
        size_t end = i;
        if (end > start && source[end - 1] == '\r') end--;
        return std::string(source.substr(start, end - start));
      }
      current_line++;
      start = i + 1;
    }
  }
  return "";
}

struct shlcppError : public std::runtime_error {
  ErrorKind kind;
  SourceLocation location;
  std::vector<std::string> backtrace_frames;

  shlcppError(ErrorKind k, const std::string &msg, SourceLocation loc = {})
      : std::runtime_error(msg), kind(k), location(std::move(loc)) {}

  std::string formatted() const {
      std::string prefix;
      switch (kind) {
          case ErrorKind::SyntaxError: prefix = "SyntaxError"; break;
          case ErrorKind::CompileError: prefix = "CompileError"; break;
          case ErrorKind::RuntimeError: prefix = "RuntimeError"; break;
          case ErrorKind::ModuleLoadError: prefix = "ModuleLoadError"; break;
          case ErrorKind::ConcurrencyError: prefix = "ConcurrencyError"; break;
          case ErrorKind::DatabaseError: prefix = "DatabaseError"; break;
          case ErrorKind::IOError: prefix = "IOError"; break;
      }
      
      std::string result = prefix;
      if (!location.file.empty()) {
          result += " in " + location.file;
          if (location.line > 0) result += ":" + std::to_string(location.line);
          if (location.col > 0) result += ":" + std::to_string(location.col);
      } else if (location.line > 0) {
          result += " at line " + std::to_string(location.line);
          if (location.col > 0) result += ":" + std::to_string(location.col);
      }
      result += ": " + std::string(what());
      
      if (!location.source_line.empty() && location.line > 0) {
          std::string lnum = std::to_string(location.line);
          std::string pad(lnum.size(), ' ');
          result += "\n  " + lnum + " | " + location.source_line;
          int col_idx = location.col > 0 ? location.col - 1 : 0;
          if (col_idx < 0) col_idx = 0;
          result += "\n  " + pad + " | " + std::string(col_idx, ' ') + "^";
          if (!location.suggestion.empty()) {
              result += " <-- " + location.suggestion;
          }
      } else if (!location.suggestion.empty()) {
          result += "\n  Suggestion: " + location.suggestion;
      }
      
      for (const auto& frame : backtrace_frames) {
          result += "\n  " + frame;
      }
      
      return result;
  }
};

using ShellLiteError = shlcppError;

struct SyntaxError : shlcppError {
  SyntaxError(const std::string &msg, SourceLocation loc = {})
      : shlcppError(ErrorKind::SyntaxError, msg, loc) {}
};

struct CompileError : shlcppError {
  CompileError(const std::string &msg, SourceLocation loc = {})
      : shlcppError(ErrorKind::CompileError, msg, loc) {}
};

struct RuntimeError : shlcppError {
  RuntimeError(const std::string &msg, SourceLocation loc = {})
      : shlcppError(ErrorKind::RuntimeError, msg, loc) {}
};

struct ConcurrencyError : shlcppError {
  ConcurrencyError(const std::string &msg, SourceLocation loc = {})
      : shlcppError(ErrorKind::ConcurrencyError, msg, loc) {}
};

struct IOError : shlcppError {
  IOError(const std::string &msg, SourceLocation loc = {})
      : shlcppError(ErrorKind::IOError, msg, loc) {}
};

} // namespace shell_lite

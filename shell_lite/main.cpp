#include <functional>
#include <fstream>
#include <iostream>
#include <filesystem>
#include "version.hpp"
#include "error/error_context.hpp"
#include "error/error_reporter.hpp"
#include "compiler.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "ast_printer.hpp"
#include "formatter.hpp"
#include "vm.hpp"

#ifdef _WIN32
#define SHL_EXPORT __declspec(dllexport)
#else
#define SHL_EXPORT __attribute__((visibility("default")))
#endif

static int run_and_report(std::function<int()> body) {
  try {
    return body();
  } catch (const shell_lite::shlcppError &e) {
    shell_lite::ErrorReporter::report(e);
    if (e.kind == shell_lite::ErrorKind::SyntaxError || e.kind == shell_lite::ErrorKind::CompileError) {
      return 2;
    }
    if (e.kind == shell_lite::ErrorKind::IOError) {
      return 3;
    }
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << std::endl;
    return 1;
  }
}

static void run_and_report_void(std::function<void()> body) {
  try {
    body();
  } catch (const shell_lite::shlcppError &e) {
    shell_lite::ErrorReporter::report(e);
  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << std::endl;
  }
}

static bool is_empty_or_comments_only(const std::string &source) {
  for (size_t i = 0; i < source.size(); ++i) {
    char c = source[i];
    if (std::isspace(static_cast<unsigned char>(c)))
      continue;
    if (c == '#') {
      while (i < source.size() && source[i] != '\n')
        i++;
      continue;
    }
    if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
      i += 2;
      while (i + 1 < source.size() &&
             !(source[i] == '*' && source[i + 1] == '/'))
        i++;
      i++; // park on / uh so the loop hops past it
      continue;
    }
    return false;
  }
  return true;
}

extern "C" SHL_EXPORT int run_shl_file_args(const char *file_path, int argc, const char **argv) {
  if (!file_path) return 1;
  std::string fp(file_path);
  if (fp.size() >= 5 && fp.substr(fp.size() - 5) == ".shbc") {
    return run_and_report([&]() -> int {
      std::ifstream in(fp, std::ios::binary);
      if (!in.is_open()) {
        std::cerr << "Error: Could not open .shbc file: " << fp << std::endl;
        return 3;
      }
      shell_lite::VM vm;
      std::vector<std::string> cli_args;
      for (int i = 0; i < argc; ++i) {
        if (argv && argv[i]) cli_args.push_back(argv[i]);
      }
      vm.set_cli_args(cli_args);
      std::filesystem::path script_dir = std::filesystem::path(file_path).parent_path();
      if (!script_dir.empty()) {
        vm.search_paths.insert(vm.search_paths.begin(), script_dir.string());
      }
      shell_lite::ObjFunction *function = shell_lite::ObjFunction::deserialize(in, vm.arena());
      vm.interpret(function);
      if (vm.has_unhandled_error()) return 1;
      vm.run_loop();
      return 0;
    });
  }

  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << file_path << std::endl;
    return 3;
  }
  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
  return run_and_report([&]() -> int {
    shell_lite::Parser parser(source);
    auto nodes = parser.parse();
    if (nodes.empty()) {
      if (is_empty_or_comments_only(source)) return 0;
      std::cerr << "Error: Parser returned no nodes/statements." << std::endl;
      return 2;
    }

    shell_lite::VM vm;
    std::vector<std::string> cli_args;
    for (int i = 0; i < argc; ++i) {
      if (argv && argv[i]) cli_args.push_back(argv[i]);
    }
    vm.set_cli_args(cli_args);
    std::filesystem::path script_dir = std::filesystem::path(file_path).parent_path();
    if (!script_dir.empty()) {
      vm.search_paths.insert(vm.search_paths.begin(), script_dir.string());
      for (const auto &candidate : {
             script_dir / "stdlib",
             script_dir / ".." / "stdlib",
             script_dir / ".." / ".." / "stdlib",
             script_dir / "shell_lite" / "stdlib",
             script_dir / ".." / "shell_lite" / "stdlib",
             script_dir / ".." / ".." / "shell_lite" / "stdlib"
           }) {
        if (std::filesystem::exists(candidate)) {
          vm.search_paths.push_back(std::filesystem::absolute(candidate).string());
        }
      }
    }
    shell_lite::Compiler compiler(&vm);
    shell_lite::ObjFunction *function = compiler.compile(file_path, nodes);
    if (!function) {
      std::cerr << "Error: Compiler returned nullptr." << std::endl;
      return 2;
    }

    vm.interpret(function);
    if (vm.has_unhandled_error()) return 1;
    vm.run_loop();
    return 0;
  });
}

extern "C" SHL_EXPORT int run_shl_file(const char *file_path) {
  return run_shl_file_args(file_path, 0, nullptr);
}

void run_repl() {
  std::cout << "===========================================" << std::endl;
  std::cout << " " << SHELL_LITE_BUILD_NAME << " v" << SHELL_LITE_VERSION << " Interactive REPL" << std::endl;
  std::cout << " Type 'exit' to quit, 'help' for instructions." << std::endl;
  std::cout << "===========================================" << std::endl;

  shell_lite::VM vm;
  std::string accumulated_code = "";

  while (true) {
    if (accumulated_code.empty()) {
      std::cout << "shl> ";
    } else {
      std::cout << "... ";
    }
    std::cout.flush();

    std::string line;
    if (!std::getline(std::cin, line)) {
      std::cout << std::endl;
      break;
    }

    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (accumulated_code.empty()) {
      std::string trimmed = line;
      size_t first = trimmed.find_first_not_of(" \t");
      if (first != std::string::npos) {
        trimmed = trimmed.substr(first);
        size_t last = trimmed.find_last_not_of(" \t");
        if (last != std::string::npos) trimmed = trimmed.substr(0, last + 1);
      }
      if (trimmed == "exit" || trimmed == "quit") {
        break;
      }
      if (trimmed == "help") {
        std::cout << "shlcpp REPL commands:\n"
                  << "  exit, quit - Exit the interactive shell\n"
                  << "  help       - Display this help message\n"
                  << "Write statements or expressions directly (e.g. say 2 + 2)\n";
        continue;
      }
      if (trimmed.empty()) {
        continue;
      }
    }

    if (!accumulated_code.empty() && line.empty()) {
      // Empty line signals end of multiline input
    } else {
      accumulated_code += line + "\n";
      size_t first = line.find_first_not_of(" \t");
      if (first != std::string::npos) {
        std::string word = line.substr(first);
        if (word.rfind("if ", 0) == 0 || word.rfind("while ", 0) == 0 ||
            word.rfind("to ", 0) == 0 || word.rfind("for ", 0) == 0 ||
            word.rfind("try", 0) == 0 || word.rfind("structure ", 0) == 0 ||
            word.rfind("class ", 0) == 0 || word.rfind("thing ", 0) == 0 ||
            word.rfind("can ", 0) == 0 || word.rfind("repeat ", 0) == 0 ||
            word.rfind("loop ", 0) == 0 || word.rfind("match ", 0) == 0) {
          continue; // Wait for multiline body
        }
      }
    }

    run_and_report_void([&]() {
      shell_lite::Parser parser(accumulated_code);
      auto nodes = parser.parse();
      if (!nodes.empty()) {
        shell_lite::Compiler compiler(&vm);
        shell_lite::ObjFunction *function = compiler.compile("<repl>", nodes);
        if (function) {
          shell_lite::Value res = vm.interpret(function);
          if (!vm.has_unhandled_error()) {
            vm.run_loop();
          } else {
            vm.clear_error();
          }
        }
      }
    });

    accumulated_code.clear();
  }
}

static int handle_check(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: shlcpp check <file.shl>" << std::endl;
    return 1;
  }
  std::string file_path = argv[2];
  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << file_path << std::endl;
    return 3;
  }
  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
  return run_and_report([&]() -> int {
    shell_lite::Parser parser(source);
    auto nodes = parser.parse();
    for (const auto &diag : parser.diagnostics()) {
      shell_lite::ErrorReporter::report(diag);
    }
    if (nodes.empty() && !is_empty_or_comments_only(source)) {
      std::cerr << "Error: Parser returned no nodes/statements." << std::endl;
      return 2;
    }
    if (parser.has_diagnostics() && nodes.empty()) {
      return 2;
    }
    std::cout << "Syntax OK: " << file_path << " (" << nodes.size() << " top-level statements)" << std::endl;
    return 0;
  });
}

static int handle_ast(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: shlcpp ast <file.shl>" << std::endl;
    return 1;
  }
  std::string file_path = argv[2];
  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << file_path << std::endl;
    return 3;
  }
  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
  return run_and_report([&]() -> int {
    shell_lite::Parser parser(source);
    auto nodes = parser.parse();
    for (const auto &diag : parser.diagnostics()) {
      shell_lite::ErrorReporter::report(diag);
    }
    shell_lite::AstPrinter printer(std::cout);
    printer.print(nodes);
    return 0;
  });
}

static int handle_eval(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: shlcpp -e <code>" << std::endl;
    return 1;
  }
  std::string source = argv[2];
  return run_and_report([&]() -> int {
    shell_lite::Parser parser(source);
    auto nodes = parser.parse();
    if (nodes.empty()) {
      return 0;
    }
    shell_lite::VM vm;
    std::vector<std::string> cli_args;
    for (int i = 3; i < argc; ++i) {
      cli_args.push_back(argv[i]);
    }
    vm.set_cli_args(cli_args);
    shell_lite::Compiler compiler(&vm);
    shell_lite::ObjFunction *function = compiler.compile("<eval>", nodes);
    if (!function) {
      std::cerr << "Error: Compiler returned nullptr." << std::endl;
      return 2;
    }
    vm.interpret(function);
    if (vm.has_unhandled_error()) return 1;
    vm.run_loop();
    return 0;
  });
}

static int handle_compile(int argc, char *argv[]) {
  if (argc < 4) {
    std::cerr << "Usage: shlcpp -c <file.shl> <file.shbc>" << std::endl;
    return 1;
  }
  std::string in_file = argv[2];
  std::string out_file = argv[3];
  std::ifstream file(in_file);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << in_file << std::endl;
    return 3;
  }
  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
  return run_and_report([&]() -> int {
    shell_lite::Parser parser(source);
    auto nodes = parser.parse();
    shell_lite::VM vm;
    shell_lite::Compiler compiler(&vm);
    shell_lite::ObjFunction *function = compiler.compile(in_file, nodes);
    if (!function) {
      std::cerr << "Error: Compiler returned nullptr." << std::endl;
      return 2;
    }
    
    std::ofstream out(out_file, std::ios::binary);
    if (!out.is_open()) {
      std::cerr << "Error: Could not open output file: " << out_file << std::endl;
      return 3;
    }
    function->serialize(out);
    std::cout << "Compiled " << in_file << " to " << out_file << std::endl;
    return 0;
  });
}

static int handle_version(int argc, char *argv[]) {
  std::cout << SHELL_LITE_BUILD_NAME << " v" << SHELL_LITE_VERSION << std::endl;
  return 0;
}

static int handle_run_file(int argc, char *argv[]) {
  std::string file_path = argv[1];
  if (file_path.size() >= 5 && file_path.substr(file_path.size() - 5) == ".shbc") {
    return run_and_report([&]() -> int {
      std::ifstream in(file_path, std::ios::binary);
      if (!in.is_open()) {
        std::cerr << "Error: Could not open .shbc file: " << file_path << std::endl;
        return 3;
      }
      
      shell_lite::VM vm;
      std::vector<std::string> cli_args;
      for (int i = 2; i < argc; ++i) {
        cli_args.push_back(argv[i]);
      }
      vm.set_cli_args(cli_args);
      std::filesystem::path script_dir = std::filesystem::path(file_path).parent_path();
      if (!script_dir.empty()) {
        vm.search_paths.insert(vm.search_paths.begin(), script_dir.string());
      }
      shell_lite::ObjFunction *function = shell_lite::ObjFunction::deserialize(in, vm.arena());
      vm.interpret(function);
      if (!vm.has_unhandled_error()) vm.run_loop();
      return vm.has_unhandled_error() ? 1 : 0;
    });
  }

  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file: " << file_path << std::endl;
    return 3;
  }
  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
  return run_and_report([&]() -> int {
    shell_lite::Parser parser(source);
    auto nodes = parser.parse();
    if (nodes.empty()) {
      if (is_empty_or_comments_only(source)) return 0;
      std::cerr << "Error: Parser returned no nodes/statements." << std::endl;
      return 2;
    }

    shell_lite::VM vm;
    std::vector<std::string> cli_args;
    for (int i = 2; i < argc; ++i) {
      cli_args.push_back(argv[i]);
    }
    vm.set_cli_args(cli_args);
    std::filesystem::path script_dir = std::filesystem::path(file_path).parent_path();
    if (!script_dir.empty()) {
      vm.search_paths.insert(vm.search_paths.begin(), script_dir.string());
      for (const auto &candidate : {
             script_dir / "stdlib",
             script_dir / ".." / "stdlib",
             script_dir / ".." / ".." / "stdlib",
             script_dir / "shell_lite" / "stdlib",
             script_dir / ".." / "shell_lite" / "stdlib",
             script_dir / ".." / ".." / "shell_lite" / "stdlib"
           }) {
        if (std::filesystem::exists(candidate)) {
          vm.search_paths.push_back(std::filesystem::absolute(candidate).string());
        }
      }
    }
    shell_lite::Compiler compiler(&vm);
    shell_lite::ObjFunction *function = compiler.compile(file_path, nodes);
    if (!function) {
      std::cerr << "Error: Compiler returned nullptr." << std::endl;
      return 2;
    }

    vm.interpret(function);
    if (vm.has_unhandled_error()) return 1;
    vm.run_loop();
    return 0;
  });
}

struct CLICommand {
  std::string name;
  std::string alias;
  std::string usage;
  std::string description;
  std::function<int(int, char *[])> handler;
};

namespace {

std::vector<std::string> split_lines_simple(const std::string &s) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t nl = s.find('\n', pos);
    if (nl == std::string::npos) {
      lines.push_back(s.substr(pos));
      break;
    }
    lines.push_back(s.substr(pos, nl - pos));
    pos = nl + 1;
  }
  return lines;
}

// tiny unified diff soooo bails to full bodies when the table gets too big
std::string unified_diff(const std::string &old_text, const std::string &new_text,
                         const std::string &path) {
  std::vector<std::string> a = split_lines_simple(old_text);
  std::vector<std::string> b = split_lines_simple(new_text);
  size_t n = a.size(), m = b.size();
  std::ostringstream out;
  out << "--- " << path << "\n+++ " << path << " (formatted)\n";
  const size_t kMaxCells = 4000000;
  if ((n + 1) * (m + 1) > kMaxCells) {
    for (auto &l : a)
      out << "-" << l << "\n";
    for (auto &l : b)
      out << "+" << l << "\n";
    return out.str();
  }
  std::vector<int> dp((n + 1) * (m + 1), 0);
  auto at = [&](size_t i, size_t j) -> int & { return dp[i * (m + 1) + j]; };
  for (size_t i = n; i-- > 0;)
    for (size_t j = m; j-- > 0;)
      at(i, j) = (a[i] == b[j]) ? at(i + 1, j + 1) + 1
                                : std::max(at(i + 1, j), at(i, j + 1));
  struct Op {
    char kind;
    std::string line;
  };
  std::vector<Op> ops;
  size_t i = 0, j = 0;
  while (i < n && j < m) {
    if (a[i] == b[j]) {
      ops.push_back({' ', a[i]});
      i++;
      j++;
    } else if (at(i + 1, j) >= at(i, j + 1)) {
      ops.push_back({'-', a[i]});
      i++;
    } else {
      ops.push_back({'+', b[j]});
      j++;
    }
  }
  while (i < n)
    ops.push_back({'-', a[i++]});
  while (j < m)
    ops.push_back({'+', b[j++]});
  const size_t kCtx = 3;
  size_t k = 0;
  bool in_hunk = false;
  size_t hunk_a = 0, hunk_b = 0;
  std::vector<Op> hunk;
  auto flush_hunk = [&]() {
    if (hunk.empty())
      return;
    size_t ca = 0, cb = 0, hd = 0, ad = 0;
    for (auto &o : hunk) {
      if (o.kind != '+')
        ca++;
      if (o.kind != '-')
        cb++;
    }
    size_t lead = 0;
    while (lead < hunk.size() && lead < kCtx && hunk[lead].kind == ' ')
      lead++;
    out << "@@ -" << (hunk_a + 1) << "," << ca << " +" << (hunk_b + 1) << ","
        << cb << " @@\n";
    for (auto &o : hunk)
      out << o.kind << o.line << "\n";
    hunk.clear();
    in_hunk = false;
  };
  size_t p = 0;
  while (p < ops.size()) {
    if (ops[p].kind == ' ') {
      size_t q = p;
      while (q < ops.size() && ops[q].kind == ' ')
        q++;
      size_t run = q - p;
      if (run > 2 * kCtx) {
        if (in_hunk) {
          for (size_t t = 0; t < kCtx; t++)
            hunk.push_back(ops[p + t]);
          flush_hunk();
        }
        p = q - kCtx;
        hunk_a = hunk_b = 0;
        size_t ii = 0, jj = 0;
        for (size_t t = 0; t < p; t++) {
          if (ops[t].kind != '+')
            ii++;
          if (ops[t].kind != '-')
            jj++;
        }
        hunk_a = ii;
        hunk_b = jj;
        for (size_t t = p; t < p + kCtx && t < q; t++)
          hunk.push_back(ops[t]);
        in_hunk = true;
        p = q;
        continue;
      }
      for (size_t t = p; t < q; t++) {
        if (!in_hunk) {
          size_t start = (t >= kCtx) ? t - kCtx : 0;
          hunk_a = hunk_b = 0;
          for (size_t u = 0; u < start; u++) {
            if (ops[u].kind != '+')
              hunk_a++;
            if (ops[u].kind != '-')
              hunk_b++;
          }
          for (size_t u = start; u < t; u++)
            hunk.push_back(ops[u]);
          in_hunk = true;
        }
        hunk.push_back(ops[t]);
      }
      p = q;
    } else {
      if (!in_hunk) {
        size_t start = (p >= kCtx) ? p - kCtx : 0;
        hunk_a = hunk_b = 0;
        for (size_t u = 0; u < start; u++) {
          if (ops[u].kind != '+')
            hunk_a++;
          if (ops[u].kind != '-')
            hunk_b++;
        }
        for (size_t u = start; u < p; u++)
          hunk.push_back(ops[u]);
        in_hunk = true;
      }
      hunk.push_back(ops[p]);
      p++;
    }
  }
  if (in_hunk)
    flush_hunk();
  return out.str();
}

// drop carriage returns so crlf files compare clean against lf output
static std::string strip_cr(const std::string &s) {
  std::string r;
  r.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n')
      continue;
    r.push_back(s[i]);
  }
  return r;
}

int atomic_write_file(const std::string &path, const std::string &content) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (fs::is_symlink(fs::symlink_status(path, ec)))
    return -2; // nah not touching symlinks
  std::string tmp = path + ".shlfmt.tmp";
  for (int attempt = 0; attempt < 100; attempt++) {
    std::string cand = (attempt == 0) ? tmp : tmp + "." + std::to_string(attempt);
    if (fs::exists(cand, ec))
      continue;
    tmp = cand;
    break;
  }
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
      return -1;
    f << content;
    f.flush();
    if (!f)
      return -1;
  }
#ifdef _WIN32
  // rename fails over an existing file on windows so drop it first
  fs::remove(path, ec);
  ec.clear();
#endif
  fs::rename(tmp, path, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return -1;
  }
  return 0;
}

std::string format_source_text(const std::string &source,
                               shell_lite::FormatOptions opts,
                               const std::string &filename = "") {
  if (!filename.empty())
    shell_lite::apply_editorconfig(filename, opts);
  shell_lite::Parser parser(source);
  auto nodes = parser.parse();
  for (const auto &diag : parser.diagnostics())
    shell_lite::ErrorReporter::report(diag);
  if (nodes.empty() && !is_empty_or_comments_only(source))
    throw shell_lite::SyntaxError("formatter: parser returned no statements");
  shell_lite::Formatter fmt(opts);
  return fmt.format(nodes, source);
}

}

static int handle_fmt(int argc, char *argv[]) {
  shell_lite::FormatOptions opts;
  std::vector<std::string> files;
  bool show_help = false;
  for (int k = 2; k < argc; k++) {
    std::string a = argv[k];
    if (a == "--check")
      opts.check = true;
    else if (a == "--diff")
      opts.diff = true;
    else if (a == "--stdin")
      opts.stdin_mode = true;
    else if (a == "--help" || a == "-h")
      show_help = true;
    else if (a.rfind("--range=", 0) == 0) {
      std::string r = a.substr(8);
      size_t c = r.find(':');
      if (c == std::string::npos) {
        std::cerr << "Usage: shlcpp fmt [--range START:END] ..." << std::endl;
        return 1;
      }
      opts.range_start = std::atoi(r.substr(0, c).c_str());
      opts.range_end = std::atoi(r.substr(c + 1).c_str());
      if (opts.range_start < 1 || opts.range_end < opts.range_start) {
        std::cerr << "Error: invalid --range (want 1-based START:END)" << std::endl;
        return 1;
      }
    } else if (a.rfind("--edition=", 0) == 0) {
      // no editions yet so dis does nothing for now
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << "Unknown option: " << a << std::endl;
      return 1;
    } else {
      files.push_back(a);
    }
  }
  if (show_help) {
    std::cout << "Usage: shlcpp fmt [options] [file.shl ...]\n"
                 "\nOptions:\n"
                 "  --check        do not write; exit 1 if any file needs formatting\n"
                 "  --diff         print a unified diff of the changes\n"
                 "  --stdin        read source from stdin, write formatted to stdout\n"
                 "  --range=S:E    only reformat statements fully inside lines S..E\n"
                 "  --edition=YEAR accepted no-op hook for future editions\n";
    return 0;
  }
  if (opts.stdin_mode) {
    if (!files.empty()) {
      std::cerr << "Error: --stdin takes no file arguments" << std::endl;
      return 1;
    }
    return run_and_report([&]() -> int {
      std::string source((std::istreambuf_iterator<char>(std::cin)),
                         std::istreambuf_iterator<char>());
      std::string out = format_source_text(source, opts);
      if (opts.check) {
        bool same = (out == source) || (strip_cr(out) == strip_cr(source));
        return same ? 0 : 1;
      }
      if (opts.diff) {
        if (out != source)
          std::cout << unified_diff(source, out, "<stdin>");
        return 0;
      }
      std::cout << out;
      return 0;
    });
  }
  if (files.empty()) {
    std::cerr << "Usage: shlcpp fmt [options] <file.shl> [...]" << std::endl;
    return 1;
  }
  if (opts.check)
    opts.write = false;
  if (opts.diff)
    opts.write = false;
  return run_and_report([&]() -> int {
    int rc = 0;
    for (const auto &path : files) {
      std::ifstream file(path, std::ios::binary);
      if (!file.is_open()) {
        std::cerr << "Error: Could not open file: " << path << std::endl;
        rc = 3;
        continue;
      }
      std::string source((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
      file.close();
      std::string out = format_source_text(source, opts, path);
      if (out == source || strip_cr(out) == strip_cr(source)) {
        continue;
      }
      if (opts.check) {
        std::cout << "would reformat " << path << std::endl;
        rc = 1;
        continue;
      }
      if (opts.diff) {
        std::cout << unified_diff(source, out, path);
        continue;
      }
      int w = atomic_write_file(path, out);
      if (w == -2) {
        std::cerr << "Error: refusing to format symlink: " << path << std::endl;
        rc = 3;
      } else if (w != 0) {
        std::cerr << "Error: could not write file: " << path << std::endl;
        rc = 3;
      }
    }
    return rc;
  });
}

static const std::vector<CLICommand> CLI_COMMANDS = {
  {"check",   "-k",            "check <file.shl>",         "Validate syntax and parser AST without execution", handle_check},
  {"ast",     "-a",            "ast <file.shl>",           "Output Abstract Syntax Tree in JSON format",       handle_ast},
  {"compile", "-c",            "-c, --compile <in> <out>", "Compile source script to bytecode (.shbc)",        handle_compile},
  {"eval",    "-e",            "-e, --eval <code>",        "Evaluate inline code string directly",             handle_eval},
  {"version", "-v",            "-v, --version",            "Display runtime version",                          handle_version},
  {"fmt",     "",              "fmt [options] <file.shl>", "Format source files (gofmt-style, in place)",      handle_fmt},
};

static int handle_help(int argc, char *argv[]) {
  std::cout << SHELL_LITE_BUILD_NAME << " v" << SHELL_LITE_VERSION << std::endl;
  std::cout << "Usage: shlcpp [subcommand|options] [file.shl] [args...]\n" << std::endl;
  std::cout << "Subcommands & Options:" << std::endl;
  for (const auto &cmd : CLI_COMMANDS) {
    std::cout << "  " << cmd.name;
    if (!cmd.alias.empty()) std::cout << ", " << cmd.alias;
    int pad = 24 - (int)(cmd.name.size() + (cmd.alias.empty() ? 0 : cmd.alias.size() + 2));
    if (pad < 2) pad = 2;
    std::cout << std::string(pad, ' ') << cmd.description << std::endl;
  }
  std::cout << "  help, -h, --help        Display this help message" << std::endl;
  std::cout << "\nExit Codes:" << std::endl;
  std::cout << "  0 : Success" << std::endl;
  std::cout << "  1 : Runtime error / unhandled exception" << std::endl;
  std::cout << "  2 : Syntax / parse / compilation error" << std::endl;
  std::cout << "  3 : File I/O error" << std::endl;
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    run_repl();
    return 0;
  }

  std::string arg1 = argv[1];
  if (arg1 == "--help" || arg1 == "-h" || arg1 == "help") {
    return handle_help(argc, argv);
  }

  for (const auto &cmd : CLI_COMMANDS) {
    if (arg1 == cmd.name || arg1 == cmd.alias || 
        arg1 == ("--" + cmd.name) || (cmd.name == "compile" && arg1 == "--compile") ||
        (cmd.name == "eval" && arg1 == "--eval")) {
      return cmd.handler(argc, argv);
    }
  }

  return handle_run_file(argc, argv);
}

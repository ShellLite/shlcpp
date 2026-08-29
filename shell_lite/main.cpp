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

static const std::vector<CLICommand> CLI_COMMANDS = {
  {"check",   "-k",            "check <file.shl>",         "Validate syntax and parser AST without execution", handle_check},
  {"ast",     "-a",            "ast <file.shl>",           "Output Abstract Syntax Tree in JSON format",       handle_ast},
  {"compile", "-c",            "-c, --compile <in> <out>", "Compile source script to bytecode (.shbc)",        handle_compile},
  {"eval",    "-e",            "-e, --eval <code>",        "Evaluate inline code string directly",             handle_eval},
  {"version", "-v",            "-v, --version",            "Display runtime version",                          handle_version},
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

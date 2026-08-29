#include "../native_registry.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace shell_lite {

void register_stdlib_io(VM* vm) {
    NativeRegistry::register_builtin(vm, "file_open", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count < 1) return Value();
                std::string path = vm->peek(arg_count - 1).to_string();
        
        
                auto* file_obj = vm->arena().allocate<ObjFile>(path);
                file_obj->stream.open(path, std::ios::binary | std::ios::in);
                if (!file_obj->stream.is_open()) {
                    vm->has_error = true;
                    vm->error_value = Value(vm->arena().allocate_string("Could not open file: " + path));
                    return Value();
                }
                file_obj->is_open = true;
                return Value(file_obj);
    });
    NativeRegistry::register_builtin(vm, "file_read", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count < 1) return Value();
        Value v_file = vm->peek(arg_count - 1);
        if (!v_file.is_file()) return Value();
        ObjFile* file_obj = static_cast<ObjFile*>(v_file.get_obj());
        if (!file_obj->is_open) return Value();

        if (arg_count > 1) {
            size_t size = (size_t)vm->peek(arg_count - 2).as_number();
            std::string buffer(size, '\0');
            file_obj->stream.read(&buffer[0], size);
            std::streamsize bytes_read = file_obj->stream.gcount();
            buffer.resize(bytes_read);
            return Value(vm->arena().allocate_string(buffer));
        } else {
            std::string content((std::istreambuf_iterator<char>(file_obj->stream)),
                                std::istreambuf_iterator<char>());
            return Value(vm->arena().allocate_string(content));
        }
    });
    NativeRegistry::register_builtin(vm, "file_readline", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 1) return Value();
                Value v_file = vm->peek(0);
                if (!v_file.is_file()) return Value();
                ObjFile* file_obj = static_cast<ObjFile*>(v_file.get_obj());
                if (!file_obj->is_open) return Value();
        
                std::string line;
                if (std::getline(file_obj->stream, line)) {
                    return Value(vm->arena().allocate_string(line));
                }
                return Value();
    });
    NativeRegistry::register_builtin(vm, "file_close", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count != 1) return Value();
                Value v_file = vm->peek(0);
                if (!v_file.is_file()) return Value();
                ObjFile* file_obj = static_cast<ObjFile*>(v_file.get_obj());
                if (file_obj->is_open) {
                    file_obj->stream.close();
                    file_obj->is_open = false;
                }
                return Value();
    });
    NativeRegistry::bind(vm, "io_read", [](std::string path) -> std::string {
        std::ifstream f(path);
        if (!f.is_open()) {
            throw std::runtime_error("Could not open file: " + path);
        }
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return content;
    });
    NativeRegistry::bind(vm, "io_write", [](std::string path, std::string content) -> void {
        std::ofstream f(path);
        if (!f.is_open()) {
            throw std::runtime_error("Could not open file for writing: " + path);
        }
        f << content;
    });
    NativeRegistry::bind(vm, "io_append", [](std::string path, std::string content) -> void {
        std::ofstream f(path, std::ios::app);
        if (!f.is_open()) {
            throw std::runtime_error("Could not open file for append: " + path);
        }
        f << content;
    });
    NativeRegistry::bind(vm, "write", [](std::string path, std::string content) -> void {
        std::ofstream f(path);
        if (!f.is_open()) {
            throw std::runtime_error("Could not open file for writing: " + path);
        }
        f << content;
    });
    NativeRegistry::bind(vm, "read", [](std::string path) -> std::string {
        std::ifstream f(path);
        if (!f.is_open()) {
            throw std::runtime_error("Could not open file: " + path);
        }
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    });
    NativeRegistry::bind(vm, "std_io_read", [](std::string path) -> std::string {
        std::ifstream f(path);
        if (!f.is_open()) {
            throw std::runtime_error("Could not open file: " + path);
        }
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    });
    NativeRegistry::bind(vm, "std_io_write", [](std::string path, std::string content) -> void {
        std::ofstream f(path);
        if (!f.is_open()) {
            throw std::runtime_error("Could not open file for writing: " + path);
        }
        f << content;
    });
    NativeRegistry::bind(vm, "io_write_bytes", [](std::string path, std::string content) -> void {
        std::ofstream f(path, std::ios::binary);
        if (!f.is_open()) {
            throw std::runtime_error("Could not open file for binary writing: " + path);
        }
        f.write(content.data(), content.size());
    });
    NativeRegistry::bind(vm, "io_read_bytes", [](std::string path) -> std::string {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            throw std::runtime_error("Could not open file for binary reading: " + path);
        }
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    });
    NativeRegistry::bind(vm, "io_exists", [](std::string path) -> bool {
        return std::filesystem::exists(path);
    });
    NativeRegistry::bind(vm, "std_io_exists", [](std::string path) -> bool {
        return std::filesystem::exists(path);
    });
    NativeRegistry::bind(vm, "io_delete", [](std::string path) -> void {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    });
    NativeRegistry::bind(vm, "std_io_delete", [](std::string path) -> void {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    });
    NativeRegistry::bind(vm, "io_copy", [](std::string src, std::string dest) -> void {
        std::filesystem::copy(src, dest, std::filesystem::copy_options::overwrite_existing);
    });
    NativeRegistry::bind(vm, "io_rename", [](std::string old_name, std::string new_name) -> void {
        std::filesystem::rename(old_name, new_name);
    });
    NativeRegistry::bind(vm, "io_mkdir", [](std::string path) -> void {
        std::filesystem::create_directories(path);
    });
    NativeRegistry::register_builtin(vm, "io_listdir", 1, [](VM* vm, int arg_count) -> Value {
        std::string path = vm->peek(0).to_string();
        auto* list = vm->arena().allocate<ObjList>();
        std::error_code ec;
        std::filesystem::directory_iterator it(path, ec);
        if (!ec) {
            for (const auto& entry : it) {
                list->elements.push_back(Value(vm->arena().allocate_string(entry.path().string())));
            }
        }
        return Value(list);
    });

    // Path Helpers
    NativeRegistry::bind(vm, "path_join", [](std::string a, std::string b) -> std::string {
        return (std::filesystem::path(a) / b).string();
    });
    NativeRegistry::bind(vm, "path_dirname", [](std::string path) -> std::string {
        return std::filesystem::path(path).parent_path().string();
    });
    NativeRegistry::bind(vm, "path_basename", [](std::string path) -> std::string {
        return std::filesystem::path(path).filename().string();
    });
    NativeRegistry::bind(vm, "path_extension", [](std::string path) -> std::string {
        return std::filesystem::path(path).extension().string();
    });
    NativeRegistry::bind(vm, "path_stem", [](std::string path) -> std::string {
        return std::filesystem::path(path).stem().string();
    });

    // File Metadata
    NativeRegistry::bind(vm, "file_size", [](std::string path) -> double {
        std::error_code ec;
        auto sz = std::filesystem::file_size(path, ec);
        if (ec) return 0.0;
        return (double)sz;
    });

    // Line I/O
    NativeRegistry::register_builtin(vm, "read_lines", 1, [](VM* vm, int arg_count) -> Value {
        std::string path = vm->peek(0).to_string();
        auto* list = vm->arena().allocate<ObjList>();
        std::ifstream f(path);
        if (f.is_open()) {
            std::string line;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                list->elements.push_back(Value(vm->arena().allocate_string(line)));
            }
        }
        return Value(list);
    });
    NativeRegistry::register_builtin(vm, "write_lines", 2, [](VM* vm, int arg_count) -> Value {
        std::string path = vm->peek(1).to_string();
        Value lines_val = vm->peek(0);
        std::ofstream f(path);
        if (f.is_open() && lines_val.is_list()) {
            auto* list = static_cast<ObjList*>(lines_val.get_obj());
            for (const auto& elem : list->elements) {
                f << elem.to_string() << "\n";
            }
            return Value(true);
        }
        return Value(false);
    });
}

} // namespace shell_lite

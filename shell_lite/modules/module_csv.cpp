#include "../native_registry.hpp"
#include <fstream>
#include <sstream>
#include <string>

namespace shell_lite {

static Value parse_csv_stream_impl(std::istream& file, VM* vm) {
    auto* list = vm->arena().allocate<ObjList>();
    auto* row = vm->arena().allocate<ObjList>();
    std::string cell;
    bool in_quotes = false;
    char ch;
    bool has_data = false;

    while (file.get(ch)) {
        has_data = true;
        if (ch == '"') {
            if (in_quotes && file.peek() == '"') {
                cell += '"';
                file.get();
            } else {
                in_quotes = !in_quotes;
            }
        } else if (ch == ',' && !in_quotes) {
            row->elements.push_back(Value(vm->arena().allocate_string(cell)));
            cell.clear();
        } else if ((ch == '\n' || ch == '\r') && !in_quotes) {
            if (ch == '\r' && file.peek() == '\n') {
                file.get();
            }
            row->elements.push_back(Value(vm->arena().allocate_string(cell)));
            cell.clear();
            if (!row->elements.empty()) {
                list->elements.push_back(Value(row));
                row = vm->arena().allocate<ObjList>();
            }
        } else {
            cell += ch;
        }
    }
    if (has_data && (!cell.empty() || !row->elements.empty())) {
        row->elements.push_back(Value(vm->arena().allocate_string(cell)));
        list->elements.push_back(Value(row));
    }
    return Value(list);
}

static Value csv_read_impl(VM* vm, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return Value(vm->arena().allocate<ObjList>());
    return parse_csv_stream_impl(file, vm);
}

static Value csv_parse_impl(VM* vm, const std::string& text) {
    std::istringstream ss(text);
    return parse_csv_stream_impl(ss, vm);
}

static bool csv_write_impl(const std::string& path, Value write_data, bool append) {
    if (!write_data.is_list()) return false;
    std::ofstream file(path, append ? std::ios::app : std::ios::out);
    if (!file.is_open()) return false;
    ObjList* list = static_cast<ObjList*>(write_data.get_obj());
    for (const auto& row_val : list->elements) {
        if (!row_val.is_list()) continue;
        ObjList* row = static_cast<ObjList*>(row_val.get_obj());
        for (size_t i = 0; i < row->elements.size(); ++i) {
            std::string s = row->elements[i].to_string();
            if (s.find(',') != std::string::npos || s.find('"') != std::string::npos || s.find('\n') != std::string::npos) {
                file << '"';
                for (char c : s) {
                    if (c == '"') file << '"';
                    file << c;
                }
                file << '"';
            } else {
                file << s;
            }
            if (i < row->elements.size() - 1) file << ",";
        }
        file << "\n";
    }
    return true;
}

static std::string csv_serialize_impl(Value list_val) {
    if (!list_val.is_list()) return "";
    std::ostringstream ss;
    ObjList* list = static_cast<ObjList*>(list_val.get_obj());
    for (const auto& row_val : list->elements) {
        if (!row_val.is_list()) continue;
        ObjList* row = static_cast<ObjList*>(row_val.get_obj());
        for (size_t i = 0; i < row->elements.size(); ++i) {
            std::string s = row->elements[i].to_string();
            if (s.find(',') != std::string::npos || s.find('"') != std::string::npos || s.find('\n') != std::string::npos) {
                ss << '"';
                for (char c : s) {
                    if (c == '"') ss << '"';
                    ss << c;
                }
                ss << '"';
            } else {
                ss << s;
            }
            if (i < row->elements.size() - 1) ss << ",";
        }
        ss << "\n";
    }
    return ss.str();
}

void register_stdlib_csv(VM* vm) {
    NativeRegistry::bind(vm, "std_csv_read", [vm](std::string path) -> Value {
        return csv_read_impl(vm, path);
    });

    NativeRegistry::bind(vm, "std_csv_parse", [vm](std::string text) -> Value {
        return csv_parse_impl(vm, text);
    });

    NativeRegistry::bind(vm, "std_csv_write", [](std::string path, Value data) -> bool {
        return csv_write_impl(path, data, false);
    });

    NativeRegistry::bind(vm, "std_csv_append", [](std::string path, Value data) -> bool {
        return csv_write_impl(path, data, true);
    });

    NativeRegistry::bind(vm, "std_csv_serialize", [](Value data) -> std::string {
        return csv_serialize_impl(data);
    });

    // Backwards-compatible legacy dispatcher
    NativeRegistry::register_builtin(vm, "csv_op", -1, [](VM* vm, int arg_count) -> Value {
        if (arg_count < 2) return Value();
        std::string op = vm->peek(arg_count - 1).to_string();
        std::string path_or_text = vm->peek(arg_count - 2).to_string();
        Value data = (arg_count >= 3) ? vm->peek(arg_count - 3) : Value();

        if (op == "read") {
            return csv_read_impl(vm, path_or_text);
        } else if (op == "parse") {
            return csv_parse_impl(vm, path_or_text);
        } else if (op == "write" || op == "append") {
            Value write_data = (arg_count >= 3) ? data : vm->peek(arg_count - 2);
            std::string target_path = (arg_count >= 3) ? path_or_text : "";
            return Value(csv_write_impl(target_path, write_data, op == "append"));
        } else if (op == "serialize") {
            Value list_val = (arg_count >= 2) ? vm->peek(0) : Value();
            return Value(vm->arena().allocate_string(csv_serialize_impl(list_val)));
        }
        return Value();
    });
}

} // namespace shell_lite

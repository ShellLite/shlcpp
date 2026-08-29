#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <deque>
#include <unordered_map>
#include "gc.hpp"

namespace shell_lite {

class GCArena;
struct ObjString;
struct ObjList;
struct ObjDict;
struct Callable;
struct ObjFunction;
struct ObjClosure;
struct ObjClass;
struct ObjInstance;
struct ObjModule;
struct ObjFile;
struct ObjTask;
struct ObjChannel;
struct ObjIterator;
struct ObjDatabase;
struct ObjLock;

enum class ValueType : uint8_t { VAL_NULL, VAL_BOOL, VAL_NUMBER, VAL_OBJ };

struct Value {
    std::variant<std::monostate, bool, double, GCObject*> as;

    Value() : as(std::monostate{}) {}
    Value(double v) : as(v) {}
    Value(bool v) : as(v) {}
    Value(GCObject* v) : as(v) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(as); }
    bool is_number() const { return std::holds_alternative<double>(as); }
    bool is_bool() const { return std::holds_alternative<bool>(as); }
    bool is_obj() const { return std::holds_alternative<GCObject*>(as); }

    GCObject* get_obj() const { return std::get<GCObject*>(as); }

    bool is_string() const { return is_obj() && get_obj()->type == ObjType::STRING; }
    bool is_list() const { return is_obj() && get_obj()->type == ObjType::LIST; }
    bool is_dict() const { return is_obj() && get_obj()->type == ObjType::DICT; }
    bool is_callable() const { return is_obj() && (get_obj()->type == ObjType::CALLABLE || get_obj()->type == ObjType::FUNCTION || get_obj()->type == ObjType::CLOSURE); }
    bool is_function() const { return is_obj() && get_obj()->type == ObjType::FUNCTION; }
    bool is_closure() const { return is_obj() && get_obj()->type == ObjType::CLOSURE; }
    bool is_class() const { return is_obj() && get_obj()->type == ObjType::CLASS; }
    bool is_instance() const { return is_obj() && get_obj()->type == ObjType::INSTANCE; }
    bool is_module() const { return is_obj() && get_obj()->type == ObjType::MODULE; }
    bool is_file() const { return is_obj() && get_obj()->type == ObjType::FILE_OBJ; }
    bool is_task() const { return is_obj() && get_obj()->type == ObjType::TASK; }
    bool is_channel() const { return is_obj() && get_obj()->type == ObjType::CHANNEL; }
    bool is_iterator() const { return is_obj() && get_obj()->type == ObjType::ITERATOR; }
    bool is_database() const { return is_obj() && get_obj()->type == ObjType::DATABASE; }
    bool is_lock() const { return is_obj() && get_obj()->type == ObjType::LOCK; }

    ObjDatabase* as_database() const { return reinterpret_cast<ObjDatabase*>(get_obj()); }
    ObjLock* as_lock() const { return reinterpret_cast<ObjLock*>(get_obj()); }

    double as_number() const { return std::get<double>(as); }
    bool as_bool() const {
        if (is_bool()) return std::get<bool>(as);
        if (is_number()) return std::get<double>(as) != 0;
        if (is_string()) return !as_string().empty();
        if (is_null()) return false;
        return true;
    }
    const std::string& as_string() const;
    Value clone_val(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) const;

    void mark();
    std::string to_string() const;

    Value operator+(const Value& other) const;
    Value operator-(const Value& other) const;
    Value operator*(const Value& other) const;
    Value operator/(const Value& other) const;

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool operator<(const Value& other) const;
    bool operator<=(const Value& other) const { return *this < other || *this == other; }
    bool operator>(const Value& other) const { return !(*this <= other); }
    bool operator>=(const Value& other) const { return !(*this < other); }
};

void serialize_value(std::ostream &out, const Value &val);
Value deserialize_value(std::istream &in, GCArena &arena);

}
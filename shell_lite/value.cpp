#include "value.hpp"
#include "gc.hpp"
#include "objects.hpp"
#include <sstream>

namespace shell_lite {

const std::string& Value::as_string() const {
    return static_cast<ObjString*>(get_obj())->data;
}

Value Value::clone_val(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) const {
    if (!is_obj() || !get_obj()) return *this;
    GCObject* obj = get_obj();
    if (obj->flags.load(std::memory_order_acquire) & GC_FLAG_FROZEN) {
        return *this;
    }
    GCObject* cloned_obj = obj->clone(target, clones);
    if (!cloned_obj) return Value();
    return Value(cloned_obj);
}

static std::string format_value_helper(const Value& val, int depth) {
    if (val.is_null()) return "null";
    if (val.is_number()) {
        double v = val.as_number();
        if (std::isnan(v)) return "nan";
        if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
        if (std::isfinite(v) && std::abs(v) <= 9007199254740992.0 && v == std::floor(v)) {
            return std::to_string((long long)v);
        }
        std::ostringstream ss;
        ss << v;
        return ss.str();
    }
    if (val.is_bool()) return val.as_bool() ? "true" : "false";
    if (val.is_string()) {
        if (depth > 0) {
            return "\"" + val.as_string() + "\"";
        }
        return val.as_string();
    }
    if (val.is_list()) {
        if (depth >= 10) return "[...]";
        auto* list = static_cast<ObjList*>(val.get_obj());
        std::string res = "[";
        for (size_t i = 0; i < list->elements.size(); ++i) {
            if (i > 0) res += ", ";
            res += format_value_helper(list->elements[i], depth + 1);
        }
        res += "]";
        return res;
    }
    if (val.is_dict()) {
        if (depth >= 10) return "{...}";
        auto* dict = static_cast<ObjDict*>(val.get_obj());
        std::string res = "{";
        size_t idx = 0;
        for (const auto& pair : dict->elements) {
            if (idx > 0) res += ", ";
            res += "\"" + pair.first + "\": " + format_value_helper(pair.second, depth + 1);
            idx++;
        }
        res += "}";
        return res;
    }
    if (val.is_callable()) return "<callable>";
    if (val.is_instance()) {
        auto* inst = static_cast<ObjInstance*>(val.get_obj());
        std::string name = inst->klass ? inst->klass->name : "instance";
        return "<instance " + name + ">";
    }
    if (val.is_class()) {
        auto* k = static_cast<ObjClass*>(val.get_obj());
        return "<class " + k->name + ">";
    }
    if (val.is_module()) {
        auto* m = static_cast<ObjModule*>(val.get_obj());
        return "<module " + m->name + ">";
    }
    if (val.is_file()) return "<file " + static_cast<ObjFile*>(val.get_obj())->path + ">";
    if (val.is_task()) return "<task>";
    if (val.is_channel()) return "<channel>";
    if (val.is_iterator()) return "<iterator>";
    return "unknown";
}

std::string Value::to_string() const {
    return format_value_helper(*this, 0);
}

Value Value::operator+(const Value& other) const {
    if (is_string() || other.is_string()) return Value(GCArena::instance().allocate_string(to_string() + other.to_string()));
    if (is_number() && other.is_number()) return as_number() + other.as_number();
    if (is_list() && other.is_list()) {
        auto *l1 = static_cast<ObjList *>(get_obj());
        auto *l2 = static_cast<ObjList *>(other.get_obj());
        auto *res = GCArena::instance().allocate<ObjList>();
        res->elements = l1->elements;
        res->elements.insert(res->elements.end(), l2->elements.begin(), l2->elements.end());
        return Value(res);
    }
    return Value();
}

Value Value::operator-(const Value& other) const { if (is_number() && other.is_number()) return as_number() - other.as_number(); return Value(); }

Value Value::operator*(const Value& other) const {
    if (is_number() && other.is_number()) return as_number() * other.as_number();
    if (is_string() && other.is_number()) {
        std::string res;
        for (int i = 0; i < (int)other.as_number(); ++i) res += as_string();
        return Value(GCArena::instance().allocate_string(res));
    }
    return Value();
}

Value Value::operator/(const Value& other) const { if (is_number() && other.is_number() && other.as_number() != 0) return as_number() / other.as_number(); return Value(); }

bool Value::operator<(const Value& other) const {
    if (is_number() && other.is_number()) return as_number() < other.as_number();
    if (is_string() && other.is_string()) return as_string() < other.as_string();
    return false;
}

bool Value::operator==(const Value& other) const {
    if (is_string() && other.is_string()) return as_string() == other.as_string();
    if (is_list() && other.is_list()) {
        auto* l1 = static_cast<ObjList*>(get_obj());
        auto* l2 = static_cast<ObjList*>(other.get_obj());
        if (l1 == l2) return true;
        if (l1->elements.size() != l2->elements.size()) return false;
        for (size_t i = 0; i < l1->elements.size(); ++i) {
            if (!(l1->elements[i] == l2->elements[i])) return false;
        }
        return true;
    }
    if (is_dict() && other.is_dict()) {
        auto* d1 = static_cast<ObjDict*>(get_obj());
        auto* d2 = static_cast<ObjDict*>(other.get_obj());
        if (d1 == d2) return true;
        if (d1->elements.size() != d2->elements.size()) return false;
        for (const auto& pair : d1->elements) {
            auto it = d2->elements.find(pair.first);
            if (it == d2->elements.end()) return false;
            if (!(pair.second == it->second)) return false;
        }
        return true;
    }
    return as == other.as;
}

// serialize val for channel transfer
void serialize_value(std::ostream &out, const Value &val) {
    if (val.is_null()) {
        uint8_t tag = 0;
        out.write(reinterpret_cast<const char*>(&tag), 1);
    } else if (val.is_bool()) {
        uint8_t tag = 1;
        out.write(reinterpret_cast<const char*>(&tag), 1);
        uint8_t b = val.as_bool() ? 1 : 0;
        out.write(reinterpret_cast<const char*>(&b), 1);
    } else if (val.is_number()) {
        uint8_t tag = 2;
        out.write(reinterpret_cast<const char*>(&tag), 1);
        double num = val.as_number();
        out.write(reinterpret_cast<const char*>(&num), sizeof(num));
    } else if (val.is_string()) {
        uint8_t tag = 3;
        out.write(reinterpret_cast<const char*>(&tag), 1);
        const std::string &s = val.as_string();
        uint32_t len = static_cast<uint32_t>(s.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        if (len > 0) out.write(s.data(), len);
    } else if (val.is_list()) {
        uint8_t tag = 4;
        out.write(reinterpret_cast<const char*>(&tag), 1);
        auto *l = static_cast<ObjList*>(val.get_obj());
        uint32_t count = static_cast<uint32_t>(l->elements.size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto &elem : l->elements) {
            serialize_value(out, elem);
        }
    } else if (val.is_dict()) {
        uint8_t tag = 5;
        out.write(reinterpret_cast<const char*>(&tag), 1);
        auto *d = static_cast<ObjDict*>(val.get_obj());
        uint32_t count = static_cast<uint32_t>(d->elements.size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto &pair : d->elements) {
            uint32_t klen = static_cast<uint32_t>(pair.first.size());
            out.write(reinterpret_cast<const char*>(&klen), sizeof(klen));
            if (klen > 0) out.write(pair.first.data(), klen);
            serialize_value(out, pair.second);
        }
    } else if (val.is_instance()) {
        uint8_t tag = 6;
        out.write(reinterpret_cast<const char*>(&tag), 1);
        auto *inst = static_cast<ObjInstance*>(val.get_obj());
        std::string cls_name = inst->klass ? std::string(inst->klass->name) : "";
        uint32_t clen = static_cast<uint32_t>(cls_name.size());
        out.write(reinterpret_cast<const char*>(&clen), sizeof(clen));
        if (clen > 0) out.write(cls_name.data(), clen);
        uint32_t count = static_cast<uint32_t>(inst->fields.size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto &pair : inst->fields) {
            uint32_t klen = static_cast<uint32_t>(pair.first.size());
            out.write(reinterpret_cast<const char*>(&klen), sizeof(klen));
            if (klen > 0) out.write(pair.first.data(), klen);
            serialize_value(out, pair.second);
        }
    } else {
        uint8_t tag = 0;
        out.write(reinterpret_cast<const char*>(&tag), 1);
    }
}

// deserialize val from channel stream
Value deserialize_value(std::istream &in, GCArena &arena) {
    uint8_t tag = 0;
    if (!in.read(reinterpret_cast<char*>(&tag), 1)) {
        return Value();
    }
    switch (tag) {
    case 0: return Value();
    case 1: {
        uint8_t b = 0;
        in.read(reinterpret_cast<char*>(&b), 1);
        return Value(b != 0);
    }
    case 2: {
        double num = 0.0;
        in.read(reinterpret_cast<char*>(&num), sizeof(num));
        return Value(num);
    }
    case 3: {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string s(len, '\0');
        if (len > 0) in.read(&s[0], len);
        return Value(arena.allocate_string(s));
    }
    case 4: {
        uint32_t count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        auto *l = arena.allocate<ObjList>();
        l->elements.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            l->elements.push_back(deserialize_value(in, arena));
        }
        return Value(l);
    }
    case 5: {
        uint32_t count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        auto *d = arena.allocate<ObjDict>();
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t klen = 0;
            in.read(reinterpret_cast<char*>(&klen), sizeof(klen));
            std::string k(klen, '\0');
            if (klen > 0) in.read(&k[0], klen);
            Value val = deserialize_value(in, arena);
            d->elements[k] = val;
        }
        return Value(d);
    }
    case 6: {
        uint32_t clen = 0;
        in.read(reinterpret_cast<char*>(&clen), sizeof(clen));
        std::string cls_name(clen, '\0');
        if (clen > 0) in.read(&cls_name[0], clen);
        auto *inst = arena.allocate<ObjInstance>();
        if (!cls_name.empty()) {
            inst->klass = arena.allocate<ObjClass>(cls_name);
        }
        uint32_t count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t klen = 0;
            in.read(reinterpret_cast<char*>(&klen), sizeof(klen));
            std::string k(klen, '\0');
            if (klen > 0) in.read(&k[0], klen);
            Value val = deserialize_value(in, arena);
            inst->fields[k] = val;
        }
        return Value(inst);
    }
    default:
        return Value();
    }
}

}
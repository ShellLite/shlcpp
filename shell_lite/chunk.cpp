#include "chunk.hpp"
#include "value.hpp"
#include "objects.hpp"
#include <iostream>
namespace shell_lite {

int Chunk::add_constant(Value value) {
  constants.push_back(value);
  return (int)constants.size() - 1;
}

Chunk *Chunk::clone(GCArena &target,
                    std::unordered_map<GCObject *, GCObject *> &clones) {
  auto *c = new Chunk();
  c->code = code;
  c->locations = locations;
  for (const auto &v : constants) {
    c->constants.push_back(v.clone_val(target, clones));
  }
  return c;
}

template<typename T>
void write_bin(std::ostream& out, const T& val) {
    out.write(reinterpret_cast<const char*>(&val), sizeof(T));
}

template<typename T>
void read_bin(std::istream& in, T& val) {
    in.read(reinterpret_cast<char*>(&val), sizeof(T));
}

void Chunk::serialize(std::ostream& out) const {
    static constexpr uint32_t SHBC_MAGIC = 0x43424853;
    static constexpr uint32_t SHBC_VERSION = 0x00010000;
    write_bin(out, SHBC_MAGIC);
    write_bin(out, SHBC_VERSION);

    uint32_t const_count = constants.size();
    write_bin(out, const_count);
    for (const auto& v : constants) {
        if (v.is_null()) {
            write_bin(out, (uint8_t)0x00);
        } else if (v.is_bool()) {
            write_bin(out, (uint8_t)0x01);
            write_bin(out, (uint8_t)(v.as_bool() ? 1 : 0));
        } else if (v.is_number()) {
            write_bin(out, (uint8_t)0x02);
            write_bin(out, v.as_number());
        } else if (v.is_string()) {
            write_bin(out, (uint8_t)0x03);
            const std::string& s = v.as_string();
            uint32_t len = s.length();
            write_bin(out, len);
            out.write(s.data(), len);
        } else if (v.is_function()) {
            write_bin(out, (uint8_t)0x04);
            static_cast<ObjFunction*>(v.get_obj())->serialize(out);
        } else {
            throw std::runtime_error("Cannot serialize unsupported constant type");
        }
    }

    uint32_t code_size = code.size();
    write_bin(out, code_size);
    out.write(reinterpret_cast<const char*>(code.data()), code_size);

    uint32_t loc_count = locations.size();
    write_bin(out, loc_count);
    for (const auto& loc : locations) {
        write_bin(out, (uint32_t)loc.line);
        write_bin(out, (uint32_t)loc.col);
        write_bin(out, (uint32_t)loc.count);
    }
}

Chunk* Chunk::deserialize(std::istream& in, GCArena& arena) {
    auto* c = new Chunk();
    uint32_t magic = 0;
    read_bin(in, magic);
    if (!in || magic != 0x43424853) {
        delete c;
        throw std::runtime_error("Invalid SHBC bytecode: missing magic header");
    }
    uint32_t version = 0;
    read_bin(in, version);
    if (!in || version > 0x00010000) {
        delete c;
        throw std::runtime_error("Unsupported SHBC bytecode version");
    }

    uint32_t const_count = 0;
    read_bin(in, const_count);
    if (!in || const_count > 1000000) {
        delete c;
        throw std::runtime_error("Invalid constant count in bytecode");
    }
    for (uint32_t i = 0; i < const_count; ++i) {
        uint8_t tag = 0;
        read_bin(in, tag);
        if (!in) { delete c; throw std::runtime_error("Unexpected EOF in bytecode constants"); }
        if (tag == 0x00) {
            c->constants.push_back(Value());
        } else if (tag == 0x01) {
            uint8_t b = 0; read_bin(in, b);
            c->constants.push_back(Value(b != 0));
        } else if (tag == 0x02) {
            double d = 0; read_bin(in, d);
            c->constants.push_back(Value(d));
        } else if (tag == 0x03) {
            uint32_t len = 0; read_bin(in, len);
            if (!in || len > 10000000) { delete c; throw std::runtime_error("Invalid string length in bytecode"); }
            std::string s(len, '\0');
            in.read(&s[0], len);
            if (!in) { delete c; throw std::runtime_error("Unexpected EOF reading string constant"); }
            c->constants.push_back(Value(arena.allocate_string(s)));
        } else if (tag == 0x04) {
            c->constants.push_back(Value(ObjFunction::deserialize(in, arena)));
        } else {
            delete c;
            throw std::runtime_error("Invalid constant type tag in SHBC");
        }
    }

    uint32_t code_size = 0;
    read_bin(in, code_size);
    if (!in || code_size > 10000000) {
        delete c;
        throw std::runtime_error("Invalid code size in bytecode");
    }
    c->code.resize(code_size);
    if (code_size > 0) {
        in.read(reinterpret_cast<char*>(c->code.data()), code_size);
        if (!in) { delete c; throw std::runtime_error("Unexpected EOF reading bytecode"); }
    }

    uint32_t loc_count = 0;
    read_bin(in, loc_count);
    if (!in || loc_count > 1000000) {
        delete c;
        throw std::runtime_error("Invalid location count in bytecode");
    }
    for (uint32_t i = 0; i < loc_count; ++i) {
        uint32_t line = 0, col = 0, count = 0;
        read_bin(in, line); read_bin(in, col); read_bin(in, count);
        c->locations.push_back({(int)line, (int)col, (int)count});
    }

    if (!c->verify()) {
        delete c;
        throw std::runtime_error("Bytecode verification failed for chunk");
    }
    return c;
}

bool Chunk::verify() const {
    size_t ip = 0;
    while (ip < code.size()) {
        uint8_t op = code[ip++];
        if (op > OP_HALT) return false;
        switch (op) {
            case OP_CONSTANT:
            case OP_GET_GLOBAL:
            case OP_DEFINE_GLOBAL:
            case OP_SET_GLOBAL:
            case OP_GET_PROPERTY:
            case OP_SET_PROPERTY:
            case OP_CLASS:
            case OP_METHOD:
            case OP_PROPERTY:
            case OP_GET_SELF_PROPERTY:
            case OP_SET_SELF_PROPERTY: {
                if (ip + 2 > code.size()) return false;
                uint16_t idx = (uint16_t)((code[ip] << 8) | code[ip + 1]);
                ip += 2;
                if (idx >= constants.size()) return false;
                break;
            }
            case OP_GET_LOCAL:
            case OP_SET_LOCAL:
            case OP_GET_UPVALUE:
            case OP_SET_UPVALUE: {
                if (ip + 2 > code.size()) return false;
                ip += 2;
                break;
            }
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
            case OP_LOOP:
            case OP_TRY: {
                if (ip + 2 > code.size()) return false;
                ip += 2;
                break;
            }
            case OP_FOR_ITER: {
                if (ip + 2 > code.size()) return false;
                uint16_t offset = (uint16_t)((code[ip] << 8) | code[ip + 1]);
                ip += 2;
                if (ip + offset > code.size()) return false;
                break;
            }
            case OP_INVOKE: {
                if (ip + 3 > code.size()) return false;
                uint16_t idx = (uint16_t)((code[ip] << 8) | code[ip + 1]);
                ip += 3;
                if (idx >= constants.size()) return false;
                break;
            }
            case OP_CLOSURE: {
                if (ip + 2 > code.size()) return false;
                uint16_t idx = (uint16_t)((code[ip] << 8) | code[ip + 1]);
                ip += 2;
                if (idx >= constants.size()) return false;
                if (!constants[idx].is_function()) return false;
                auto* fn = static_cast<ObjFunction*>(constants[idx].get_obj());
                int uvs = fn->upvalue_count;
                if (ip + uvs * 3 > code.size()) return false;
                ip += uvs * 3;
                break;
            }
            case OP_CALL:
            case OP_NATIVE_CALL:
            case OP_LIST:
            case OP_DICT:
            case OP_LIST_APPEND:
            case OP_SPAWN: {
                if (ip + 1 > code.size()) return false;
                ip += 1;
                break;
            }
            default:
                break;
        }
    }
    return ip == code.size();
}

} // namespace shell_lite

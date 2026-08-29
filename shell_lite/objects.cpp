#include "objects.hpp"
#include "value.hpp"
#include "chunk.hpp"
#include "vm.hpp"
#include <sstream>
#include <sqlite3.h>

namespace shell_lite {

ObjFunction::ObjFunction() : Callable(ObjType::FUNCTION), arity(0), upvalue_count(0), chunk(std::make_unique<Chunk>()) {}
ObjFunction::~ObjFunction() = default;

Value ObjFunction::call(VM* vm, int arg_count) {
    return Value();
}

void ObjFunction::serialize(std::ostream& out) const {
    static constexpr uint32_t SHBC_FILE_MAGIC = 0x43424853; // "SHBC"
    static constexpr uint16_t SHBC_VERSION_MAJOR = 1;
    static constexpr uint16_t SHBC_VERSION_MINOR = 0;

    out.write(reinterpret_cast<const char*>(&SHBC_FILE_MAGIC), sizeof(SHBC_FILE_MAGIC));
    out.write(reinterpret_cast<const char*>(&SHBC_VERSION_MAJOR), sizeof(SHBC_VERSION_MAJOR));
    out.write(reinterpret_cast<const char*>(&SHBC_VERSION_MINOR), sizeof(SHBC_VERSION_MINOR));

    uint16_t a = arity;
    out.write(reinterpret_cast<const char*>(&a), sizeof(a));
    uint16_t u = upvalue_count;
    out.write(reinterpret_cast<const char*>(&u), sizeof(u));
    
    uint32_t nl = static_cast<uint32_t>(name.length());
    out.write(reinterpret_cast<const char*>(&nl), sizeof(nl));
    if (nl > 0) out.write(name.data(), nl);
    
    uint32_t sl = static_cast<uint32_t>(source_file.length());
    out.write(reinterpret_cast<const char*>(&sl), sizeof(sl));
    if (sl > 0) out.write(source_file.data(), sl);
    
    if (chunk) {
        chunk->serialize(out);
    } else {
        Chunk empty_chunk;
        empty_chunk.serialize(out);
    }
}

ObjFunction* ObjFunction::deserialize(std::istream& in, GCArena& arena) {
    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!in || magic != 0x43424853) {
        throw std::runtime_error("Invalid SHBC bytecode: missing magic header (expected 'SHBC' / 0x43424853)");
    }
    uint16_t major = 0, minor = 0;
    in.read(reinterpret_cast<char*>(&major), sizeof(major));
    in.read(reinterpret_cast<char*>(&minor), sizeof(minor));
    if (!in || major != 1) {
        throw std::runtime_error("Unsupported SHBC bytecode format version");
    }

    auto* f = arena.allocate<ObjFunction>();
    uint16_t a = 0; in.read(reinterpret_cast<char*>(&a), sizeof(a));
    if (!in) throw std::runtime_error("Unexpected EOF reading function arity");
    f->arity = a;
    
    uint16_t u = 0; in.read(reinterpret_cast<char*>(&u), sizeof(u));
    if (!in) throw std::runtime_error("Unexpected EOF reading upvalue count");
    f->upvalue_count = u;
    
    uint32_t nl = 0; in.read(reinterpret_cast<char*>(&nl), sizeof(nl));
    if (!in || nl > 65536) throw std::runtime_error("Invalid function name length in bytecode");
    std::string n(nl, '\0');
    if (nl > 0) {
        in.read(&n[0], nl);
        if (!in) throw std::runtime_error("Unexpected EOF reading function name");
    }
    f->name = n;
    
    uint32_t sl = 0; in.read(reinterpret_cast<char*>(&sl), sizeof(sl));
    if (!in || sl > 65536) throw std::runtime_error("Invalid source file name length in bytecode");
    std::string s(sl, '\0');
    if (sl > 0) {
        in.read(&s[0], sl);
        if (!in) throw std::runtime_error("Unexpected EOF reading source file name");
    }
    f->source_file = s;
    
    f->chunk = std::unique_ptr<Chunk>(Chunk::deserialize(in, arena));
    return f;
}


Value ObjClosure::call(VM* vm, int arg_count) {
    if (vm->call(this, arg_count)) {
        return vm->run();
    }
    return Value();
}


void ObjChannel::send(const Value &val) {
    std::ostringstream ss(std::ios::binary);
    serialize_value(ss, val);
    std::string payload = ss.str();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed) return;
        state->queue.push_back(std::move(payload));
    }
    state->cv.notify_one();
}

void ObjChannel::send_shared(const Value &val) {
    if (val.is_obj() && val.get_obj()) {
        val.get_obj()->flags.fetch_or(GC_FLAG_SHARED, std::memory_order_release);
    }
    send(val);
}

void ObjChannel::transfer(Value &val) {
    if (val.is_obj() && val.get_obj()) {
        val.get_obj()->flags.fetch_or(GC_FLAG_SHARED, std::memory_order_release);
    }
    send(val);
    val = Value();
}

Value ObjChannel::receive(GCArena &target_arena) {
    std::string payload;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait(lock, [this] { return !state->queue.empty() || state->closed; });
        if (state->queue.empty() && state->closed) return Value();
        payload = std::move(state->queue.front());
        state->queue.pop_front();
    }
    std::istringstream ss(payload, std::ios::binary);
    return deserialize_value(ss, target_arena);
}

void ObjChannel::close() {
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->closed = true;
    }
    state->cv.notify_all();
}

bool ObjChannel::is_closed() const {
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->closed;
}

GCObject *ObjChannel::clone(GCArena &target,
                            std::unordered_map<GCObject *, GCObject *> &clones) {
    if (clones.count(this)) return clones[this];
    auto *ch = target.allocate<ObjChannel>(state);
    clones[this] = ch;
    return ch;
}



GCObject* ObjString::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* s = target.allocate_string(data);
    clones[this] = s;
    return s;
}

GCObject* ObjList::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* l = target.allocate<ObjList>();
    clones[this] = l;
    for (const auto& v : elements) l->elements.push_back(v.clone_val(target, clones));
    return l;
}

GCObject* ObjDict::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* d = target.allocate<ObjDict>();
    clones[this] = d;
    for (const auto& pair : elements) d->elements[pair.first] = pair.second.clone_val(target, clones);
    return d;
}

GCObject* ObjFunction::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* f = target.allocate<ObjFunction>();
    clones[this] = f;
    f->arity = arity;
    f->upvalue_count = upvalue_count;
    f->name = name;
    f->source_file = source_file;
    if (chunk) f->chunk = std::unique_ptr<Chunk>(chunk->clone(target, clones));
    return f;
}

GCObject* ObjUpvalue::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* uv = target.allocate<ObjUpvalue>(nullptr);
    clones[this] = uv;

    uv->closed = (location ? *location : closed).clone_val(target, clones);
    uv->location = &uv->closed;
    return uv;
}

GCObject* ObjClosure::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* f_clone = static_cast<ObjFunction*>(function->clone(target, clones));
    auto* c = target.allocate<ObjClosure>(f_clone);
    clones[this] = c;
    c->module_globals = module_globals;
    for (int i = 0; i < (int)upvalues.size(); ++i) {
        if (upvalues[i]) c->upvalues[i] = static_cast<ObjUpvalue*>(upvalues[i]->clone(target, clones));
    }
    return c;
}

GCObject* ObjClass::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* c = target.allocate<ObjClass>(name);
    clones[this] = c;
    for (const auto& pair : methods) {
        c->methods[pair.first] = static_cast<ObjClosure*>(pair.second->clone(target, clones));
    }
    return c;
}

GCObject* ObjInstance::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* inst = target.allocate<ObjInstance>();
    clones[this] = inst;
    if (klass) inst->klass = static_cast<ObjClass*>(klass->clone(target, clones));
    for (const auto& pair : fields) inst->fields[pair.first] = pair.second.clone_val(target, clones);
    return inst;
}

GCObject* ObjModule::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* m = target.allocate<ObjModule>();
    clones[this] = m;
    m->name = name;
    for (const auto& pair : globals) m->globals[pair.first] = pair.second.clone_val(target, clones);
    return m;
}

void ObjIterator::mark_children() {
    iterable.mark();
}

GCObject* ObjIterator::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    if (clones.count(this)) return clones[this];
    auto* iter = target.allocate<ObjIterator>(iterable.clone_val(target, clones));
    clones[this] = iter;
    iter->index = index;
    iter->dict_keys = dict_keys;
    return iter;
}

ObjDatabase::ObjDatabase(std::string path)
    : GCObject(ObjType::DATABASE), filename(std::move(path)), is_open(false) {
    if (sqlite3_open(filename.c_str(), &conn) == SQLITE_OK) {
        is_open = true;
    }
}

ObjDatabase::~ObjDatabase() {
    std::lock_guard<std::mutex> lock(mutex);
    if (conn) {
        sqlite3_close(conn);
        conn = nullptr;
        is_open = false;
    }
}

GCObject* ObjDatabase::clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) {
    throw std::runtime_error("Cannot pass raw database connections across threads");
}

}


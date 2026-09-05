#include "gc.hpp"
#include "value.hpp"
#include "vm.hpp"
#include <algorithm>
#include <mutex>
#include <cstdlib>

namespace shell_lite {

static thread_local GCArena* g_current_arena = nullptr;

// cache starting threshold from env or default
size_t GCArena::get_initial_gc_threshold() {
    const char* env = std::getenv("SHL_GC_THRESHOLD");
    if (env) {
        try {
            size_t val = std::stoull(env);
            if (val > 0) return val;
        } catch (...) {}
    }
    return DEFAULT_INITIAL_GC_THRESHOLD;
}

GCArena::GCArena(VM* vm)
    : vm_(vm),
      bytes_allocated_(0),
      initial_gc_threshold_(get_initial_gc_threshold()),
      next_gc_(initial_gc_threshold_) {
    g_current_arena = this;
}

#include "objects.hpp"

static size_t get_object_size(GCObject* obj) {
    switch (obj->type) {
        case ObjType::STRING: return sizeof(ObjString);
        case ObjType::LIST: return sizeof(ObjList);
        case ObjType::DICT: return sizeof(ObjDict);
        case ObjType::FUNCTION: return sizeof(ObjFunction);
        case ObjType::CLOSURE: return sizeof(ObjClosure);
        case ObjType::CLASS: return sizeof(ObjClass);
        case ObjType::INSTANCE: return sizeof(ObjInstance);
        case ObjType::MODULE: return sizeof(ObjModule);
        case ObjType::FILE_OBJ: return sizeof(ObjFile);
        case ObjType::TASK: return sizeof(ObjTask);
        case ObjType::CHANNEL: return sizeof(ObjChannel);
        case ObjType::ITERATOR: return sizeof(ObjIterator);
        case ObjType::UPVALUE: return sizeof(ObjUpvalue);
        case ObjType::DATABASE: return sizeof(ObjDatabase);
        case ObjType::LOCK: return sizeof(ObjLock);
        default: return sizeof(GCObject);
    }
}

GCArena::~GCArena() {
    if (g_current_arena == this) {
        g_current_arena = nullptr;
    }
}

// thread-local fallback when running without explicit vm
GCArena& GCArena::instance() {
    if (!g_current_arena) {
        static thread_local GCArena fallback_arena(nullptr);
        g_current_arena = &fallback_arena;
    }
    return *g_current_arena;
}

ObjString* GCArena::allocate_string(const std::string& str) {
    std::lock_guard<std::recursive_mutex> lock(gc_mutex_);
    bytes_allocated_ += str.size();
    return allocate<ObjString>(str);
}

// deduplicate string via arena intern table
ObjString* GCArena::intern(const std::string& str) {
    std::lock_guard<std::recursive_mutex> lock(gc_mutex_);
    auto it = strings_.find(str);
    if (it != strings_.end()) return it->second;

    auto* s = allocate<ObjString>(str);
    strings_[str] = s;
    return s;
}

void GCArena::collect() {
    std::lock_guard<std::recursive_mutex> lock(gc_mutex_);
    collect_internal();
}

// hand off heap objects and interned strings to target arena
void GCArena::transfer_to(GCArena& target) {
    if (this == &target) return;
    std::scoped_lock lock(gc_mutex_, target.gc_mutex_);
    
    if (first_object_) {
        GCObject* curr = first_object_;
        while (curr->next_gc != nullptr) {
            curr = curr->next_gc;
        }
        curr->next_gc = target.first_object_;
        target.first_object_ = first_object_;
        target.bytes_allocated_ += bytes_allocated_;
        first_object_ = nullptr;
        bytes_allocated_ = 0;
    }

    for (auto& pair : strings_) {
        if (target.strings_.find(pair.first) == target.strings_.end()) {
            target.strings_[pair.first] = pair.second;
        }
    }
    strings_.clear();
}

// guard unanchored object so gc doesn't nuke it mid-allocation -_-
void GCArena::push_temp_root(GCObject* obj) {
    if (!obj) return;
    std::lock_guard<std::recursive_mutex> lock(gc_mutex_);
    temp_roots_.push_back(obj);
}

void GCArena::remove_temp_root(GCObject* obj) {
    if (!obj) return;
    std::lock_guard<std::recursive_mutex> lock(gc_mutex_);
    for (auto it = temp_roots_.rbegin(); it != temp_roots_.rend(); ++it) {
        if (*it == obj) {
            temp_roots_.erase((it + 1).base());
            return;
        }
    }
}

static size_t get_object_live_bytes(GCObject* obj) {
    if (!obj) return 0;
    size_t sz = get_object_size(obj);
    switch (obj->type) {
        case ObjType::STRING: {
            auto* s = static_cast<ObjString*>(obj);
            sz += s->data.capacity();
            break;
        }
        case ObjType::LIST: {
            auto* l = static_cast<ObjList*>(obj);
            sz += l->elements.capacity() * sizeof(Value);
            break;
        }
        case ObjType::DICT: {
            auto* d = static_cast<ObjDict*>(obj);
            sz += d->elements.size() * (3 * sizeof(void*) + sizeof(std::string) + sizeof(Value));
            for (const auto& pair : d->elements) {
                sz += pair.first.capacity();
            }
            break;
        }
        case ObjType::FUNCTION: {
            auto* f = static_cast<ObjFunction*>(obj);
            sz += f->name.capacity() + f->source_file.capacity();
            if (f->chunk) {
                sz += f->chunk->code.capacity() * sizeof(uint8_t) +
                      f->chunk->constants.capacity() * sizeof(Value) +
                      f->chunk->locations.capacity() * sizeof(LocationEntry);
            }
            break;
        }
        case ObjType::INSTANCE: {
            auto* inst = static_cast<ObjInstance*>(obj);
            sz += inst->fields.bucket_count() * sizeof(void*) + inst->fields.size() * (sizeof(std::string) + sizeof(Value));
            break;
        }
        case ObjType::DATABASE: {
            auto* db = static_cast<ObjDatabase*>(obj);
            sz += db->filename.capacity();
            break;
        }
        default:
            break;
    }
    return sz;
}

// full mark-sweep pass: clear marks, trace roots, sweep dead objects and bump threshold
void GCArena::collect_internal() {
    for (GCObject* obj = first_object_; obj != nullptr; obj = obj->next_gc) {
        obj->marked = false;
    }

    if (vm_) {
        vm_->mark_roots();
    }

    for (GCObject* root : temp_roots_) {
        if (root) {
            Value(root).mark();
        }
    }

    size_t live_bytes = 0;

    // clean dead interned strings BEFORE sweep so we don't use-after-free freed strings -_-
    for (auto it = strings_.begin(); it != strings_.end(); ) {
        if (!it->second->marked) {
            it = strings_.erase(it);
        } else {
            live_bytes += it->first.capacity();
            ++it;
        }
    }

    // sweep dead objects from intrusive list and reclaim memory
    GCObject* prev = nullptr;
    GCObject* curr = first_object_;
    while (curr != nullptr) {
        if (curr->marked) {
            live_bytes += get_object_live_bytes(curr);
            prev = curr;
            curr = curr->next_gc;
        } else {
            GCObject* unreached = curr;
            curr = curr->next_gc;
            if (prev != nullptr) {
                prev->next_gc = curr;
            } else {
                first_object_ = curr;
            }
            delete unreached;
        }
    }

    bytes_allocated_ = live_bytes;
    next_gc_ = std::max(initial_gc_threshold_, bytes_allocated_ * GC_GROWTH_FACTOR);
}

void Value::mark() {
    if (!is_obj() || !get_obj() || get_obj()->marked) return;
    get_obj()->marked = true;
    
    if (is_task()) {
        auto* task = static_cast<ObjTask*>(get_obj());
        if (task->completed) task->result.mark();
    } else if (is_channel()) {
        // channel queue has its own mutex and strings, no gc children
    } else {
        get_obj()->mark_children();
    }
}

void ObjList::mark_children() {
    for (auto& v : elements) v.mark();
}

void ObjDict::mark_children() {
    for (auto& pair : elements) pair.second.mark();
}

void ObjFunction::mark_children() {
    if (chunk) {
        for (auto& v : chunk->constants) v.mark();
    }
}

void ObjClosure::mark_children() {
    if (function && !function->marked) {
        function->marked = true;
        function->mark_children();
    }
    for (auto* uv : upvalues) {
        if (uv && !uv->marked) {
            uv->marked = true;
            uv->mark_children();
        }
    }
}

void ObjUpvalue::mark_children() {
    closed.mark();
}

void ObjClass::mark_children() {
    for (auto& pair : methods) {
        if (pair.second && !pair.second->marked) {
            pair.second->marked = true;
            pair.second->mark_children();
        }
    }
}

void ObjInstance::mark_children() {
    if (klass && !klass->marked) {
        klass->marked = true;
        klass->mark_children();
    }
    for (auto& pair : fields) pair.second.mark();
}

void ObjModule::mark_children() {
    for (auto& pair : globals) pair.second.mark();
}

}

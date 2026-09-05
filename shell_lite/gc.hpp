#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <mutex>
#include <atomic>

namespace shell_lite {

class GCArena;

enum class ObjType : uint8_t {
    STRING, LIST, DICT, FUNCTION, CLOSURE, CLASS, INSTANCE, MODULE, FILE_OBJ, TASK, CHANNEL, ITERATOR, CALLABLE, UPVALUE, DATABASE, LOCK
};

static constexpr uint8_t GC_FLAG_SHARED = 1 << 0;
static constexpr uint8_t GC_FLAG_FROZEN = 1 << 1;

struct GCObject {
    ObjType type;
    bool marked = false;
    std::atomic<uint8_t> flags{0};
    GCObject* next_gc = nullptr;
    
    explicit GCObject(ObjType t) : type(t) {}
    virtual ~GCObject() = default;
    virtual void mark_children() {}
    virtual GCObject* clone(GCArena& target, std::unordered_map<GCObject*, GCObject*>& clones) { return nullptr; }
};

#if defined(_M_X64) || defined(__x86_64__)
static_assert(sizeof(GCObject) == 24, "GCObject layout changed unexpectedly on x64");
#endif

class VM;
struct ObjString;

class GCArena {
public:
    static constexpr size_t DEFAULT_INITIAL_GC_THRESHOLD = 1024 * 1024; // 1MB default initial GC threshold
    static constexpr size_t GC_GROWTH_FACTOR = 2;

    static size_t get_initial_gc_threshold();

    explicit GCArena(VM* vm);
    ~GCArena();

    static GCArena& instance();

    GCArena(const GCArena&) = delete;
    GCArena& operator=(const GCArena&) = delete;

    template <typename T, typename... Args>
    T* allocate(Args&&... args) {
        std::lock_guard<std::recursive_mutex> lock(gc_mutex_);
        if (bytes_allocated_ > next_gc_) {
            collect_internal();
        }

        T* ptr = new T(std::forward<Args>(args)...);
        ptr->next_gc = first_object_;
        first_object_ = ptr;
        bytes_allocated_ += sizeof(T);
        return ptr;
    }

    ObjString* allocate_string(const std::string& str);
    ObjString* intern(const std::string& str);

    void collect();
    void transfer_to(GCArena& target);

    void push_temp_root(GCObject* obj);
    void remove_temp_root(GCObject* obj);

private:
    void collect_internal();

    GCObject* first_object_ = nullptr;
    std::unordered_map<std::string, ObjString*> strings_;
    std::vector<GCObject*> temp_roots_;
    VM* vm_;

    size_t bytes_allocated_;
    size_t next_gc_;
    size_t initial_gc_threshold_;
    std::recursive_mutex gc_mutex_;
};

class GCRootGuard {
public:
    explicit GCRootGuard(GCArena& arena, GCObject* obj = nullptr)
        : arena_(&arena), obj_(obj) {
        if (arena_ && obj_) arena_->push_temp_root(obj_);
    }
    ~GCRootGuard() {
        if (arena_ && obj_) arena_->remove_temp_root(obj_);
    }
    void reset(GCObject* new_obj) {
        if (arena_ && obj_) arena_->remove_temp_root(obj_);
        obj_ = new_obj;
        if (arena_ && obj_) arena_->push_temp_root(obj_);
    }
    GCObject* get() const { return obj_; }

    GCRootGuard(const GCRootGuard&) = delete;
    GCRootGuard& operator=(const GCRootGuard&) = delete;
    GCRootGuard(GCRootGuard&& o) noexcept : arena_(o.arena_), obj_(o.obj_) {
        o.arena_ = nullptr;
        o.obj_ = nullptr;
    }
    GCRootGuard& operator=(GCRootGuard&& o) noexcept {
        if (this != &o) {
            if (arena_ && obj_) arena_->remove_temp_root(obj_);
            arena_ = o.arena_;
            obj_ = o.obj_;
            o.arena_ = nullptr;
            o.obj_ = nullptr;
        }
        return *this;
    }
private:
    GCArena* arena_;
    GCObject* obj_;
};

}
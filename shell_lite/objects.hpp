#pragma once
#include "gc.hpp"
#include "value.hpp"
#include <condition_variable>
#include <deque>
#include <fstream>
#include <future>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <sqlite3.h>

namespace shell_lite {

class VM;

struct ObjString : public GCObject {
  std::string data;
  explicit ObjString(std::string s)
      : GCObject(ObjType::STRING), data(std::move(s)) {}
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

// file stream on disk, cant pass this to other threads
struct ObjFile : public GCObject {
  std::ifstream stream;
  std::string path;
  bool is_open;

  explicit ObjFile(std::string p)
      : GCObject(ObjType::FILE_OBJ), path(std::move(p)), is_open(false) {}
  ~ObjFile() {
    if (stream.is_open())
      stream.close();
  }
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override {
    throw std::runtime_error("Cannot pass file handles across threads");
  }
};

struct ObjDatabase : public GCObject {
  sqlite3 *conn = nullptr;
  std::string filename;
  std::mutex mutex;
  bool is_open = false;

  explicit ObjDatabase(std::string path);
  ~ObjDatabase() override;
  void mark_children() override {}
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

// async task waiting on a future until work is cooked
struct ObjTask : public GCObject {
  std::shared_future<std::string> future;
  bool completed;
  Value result;

  explicit ObjTask(std::shared_future<std::string> f)
      : GCObject(ObjType::TASK), future(std::move(f)), completed(false) {}
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override {
    if (clones.count(this)) return clones[this];
    auto *t = target.allocate<ObjTask>(future);
    clones[this] = t;
    if (completed) {
      t->completed = true;
      t->result = result.clone_val(target, clones);
    }
    return t;
  }
};

// shared mutex and lock counter so cloned locks share the same lock
struct LockState {
  std::recursive_mutex mutex;
  std::atomic<std::thread::id> owner{std::thread::id{}};
  std::atomic<int> lock_count{0};
};

struct ObjLock : public GCObject {
  std::shared_ptr<LockState> state;

  ObjLock()
      : GCObject(ObjType::LOCK), state(std::make_shared<LockState>()) {}
  explicit ObjLock(std::shared_ptr<LockState> s)
      : GCObject(ObjType::LOCK), state(std::move(s)) {}
  void mark_children() override {}
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override {
    if (clones.count(this)) return clones[this];
    auto *l = target.allocate<ObjLock>(state);
    clones[this] = l;
    return l;
  }
};

// thread safe queue and condvar for channel messages
struct ChannelState {
  std::mutex mutex;
  std::condition_variable cv;
  bool closed{false};
  std::deque<std::string> queue;
};

struct ObjChannel : public GCObject {
  std::shared_ptr<ChannelState> state;

  ObjChannel()
      : GCObject(ObjType::CHANNEL), state(std::make_shared<ChannelState>()) {}
  explicit ObjChannel(std::shared_ptr<ChannelState> s)
      : GCObject(ObjType::CHANNEL), state(std::move(s)) {}

  void send(const Value &val);
  void send_shared(const Value &val);
  void transfer(Value &val);
  Value receive(GCArena &target_arena);
  void close();
  bool is_closed() const;

  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

struct ObjList : public GCObject {
  std::vector<Value> elements;
  ObjList() : GCObject(ObjType::LIST) {}
  void mark_children() override;
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

struct ObjDict : public GCObject {
  std::map<std::string, Value> elements;
  ObjDict() : GCObject(ObjType::DICT) {}
  void mark_children() override;
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

struct ObjIterator : public GCObject {
  Value iterable;
  size_t index;
  std::vector<std::string> dict_keys;

  explicit ObjIterator(Value iter)
      : GCObject(ObjType::ITERATOR), iterable(iter), index(0) {
    if (iterable.is_dict()) {
      auto* dict = static_cast<ObjDict*>(iterable.get_obj());
      dict_keys.reserve(dict->elements.size());
      for (const auto& pair : dict->elements) {
        dict_keys.push_back(pair.first);
      }
    }
  }
  void mark_children() override;
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

struct Callable : public GCObject {
  Callable(ObjType t = ObjType::CALLABLE) : GCObject(t) {}
  virtual ~Callable() = default;
  virtual Value call(VM *vm, int arg_count) = 0;
};

struct ObjFunction : public Callable {
  int arity;
  int upvalue_count;
  std::unique_ptr<struct Chunk> chunk;
  std::string name;
  std::string source_file;

  ObjFunction();
  ~ObjFunction();

  Value call(VM *vm, int arg_count) override;
  void mark_children() override;
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
  void serialize(std::ostream& out) const;
  static ObjFunction* deserialize(std::istream& in, GCArena& arena);
};

// captured local variable on stack or copied to heap
struct ObjUpvalue : public GCObject {
  Value *location;
  Value closed;
  ObjUpvalue *next;

  explicit ObjUpvalue(Value *slot)
      : GCObject(ObjType::UPVALUE), location(slot), next(nullptr) {}
  void mark_children() override;
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

struct GlobalsTable;

// remap module globals so cloned workers dont step on each other
class TableCloneScope {
public:
  TableCloneScope(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones);
  ~TableCloneScope();

  void map_table(GlobalsTable *source, std::shared_ptr<GlobalsTable> target);
  std::shared_ptr<GlobalsTable>
  get_or_clone_table(const std::shared_ptr<GlobalsTable> &source);

  static TableCloneScope *current();

private:
  GCArena &target_;
  std::unordered_map<GCObject *, GCObject *> &clones_;
  std::unordered_map<GlobalsTable *, std::shared_ptr<GlobalsTable>> table_map_;
  TableCloneScope *prev_;
};

struct ObjClosure : public Callable {
  ObjFunction *function;
  std::vector<ObjUpvalue *> upvalues;
  std::shared_ptr<GlobalsTable> module_globals;

  explicit ObjClosure(ObjFunction *f)
      : Callable(ObjType::CLOSURE), function(f) {
    upvalues.resize(f->upvalue_count, nullptr);
  }
  Value call(VM *vm, int arg_count) override;
  void mark_children() override;
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

struct ObjClass : public GCObject {
  std::string name;
  std::unordered_map<std::string, ObjClosure *> methods;
  std::vector<std::string> default_fields;

  explicit ObjClass(std::string n)
      : GCObject(ObjType::CLASS), name(std::move(n)) {}
  void mark_children() override;
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

struct ObjInstance : public GCObject {
  ObjClass *klass;
  ObjInstance() : GCObject(ObjType::INSTANCE), klass(nullptr) {}
  explicit ObjInstance(ObjClass *k) : GCObject(ObjType::INSTANCE), klass(k) {}
  std::unordered_map<std::string, Value> fields;
  void mark_children() override;
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

struct ObjModule : public GCObject {
  std::string name;
  ObjModule() : GCObject(ObjType::MODULE) {}
  explicit ObjModule(std::string n)
      : GCObject(ObjType::MODULE), name(std::move(n)) {}
  std::unordered_map<std::string, Value> globals;
  void mark_children() override;
  GCObject *clone(GCArena &target,
                  std::unordered_map<GCObject *, GCObject *> &clones) override;
};

} // namespace shell_lite
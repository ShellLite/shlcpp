#pragma once
#include "gc.hpp"
#include "value.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>

namespace shell_lite {

enum OpCode : uint8_t {
  OP_CONSTANT,
  OP_NULL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_SET_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_EQUAL,
  OP_NOT_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_GREATER_EQUAL,
  OP_LESS_EQUAL,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_NEGATE,
  OP_PRINT,
  OP_PRINT_COLOR,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_NATIVE_CALL,
  OP_INVOKE,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_RETURN,
  OP_TRY,
  OP_CATCH,
  OP_END_TRY,
  OP_THROW,
  OP_LIST,
  OP_DICT,
  OP_GET_INDEX,
  OP_SET_INDEX,
  OP_SLICE,
  OP_LIST_APPEND,
  OP_CLASS,
  OP_METHOD,
  OP_PROPERTY,
  OP_GET_SELF_PROPERTY,
  OP_SET_SELF_PROPERTY,
  OP_GET_ITER,
  OP_FOR_ITER,
  OP_SPAWN,
  OP_CHANNEL,
  OP_SEND,
  OP_RECEIVE,
  OP_DUP,

  OP_MOD,
  OP_POW,
  OP_BIT_AND,
  OP_BIT_OR,
  OP_BIT_XOR,
  OP_BIT_NOT,
  OP_LSHIFT,
  OP_RSHIFT,

  OP_IMPORT,
  OP_HALT
};

struct LocationEntry {
  int line;
  int col;
  int count;
};

struct Chunk {
  std::vector<uint8_t> code;
  std::vector<LocationEntry> locations;
  std::vector<Value> constants;

  void write(uint8_t byte, int line, int col) {
    code.push_back(byte);
    if (locations.empty() || locations.back().line != line ||
        locations.back().col != col) {
      locations.push_back({line, col, 1});
    } else {
      locations.back().count++;
    }
  }

  std::pair<int, int> get_location(int offset) {
    int current_offset = 0;
    for (const auto &entry : locations) {
      current_offset += entry.count;
      if (offset < current_offset)
        return {entry.line, entry.col};
    }
    return {-1, -1};
  }

  int add_constant(Value value);
  Chunk *clone(GCArena &target,
               std::unordered_map<GCObject *, GCObject *> &clones);
  void serialize(std::ostream& out) const;
  static Chunk* deserialize(std::istream& in, GCArena& arena);
  bool verify() const;
};

} // namespace shell_lite

#include "compiler.hpp"
#include "error/error_context.hpp"
#include "gc.hpp"
#include "vm.hpp"
#include <stdexcept>


namespace shell_lite {

struct Upvalue {
  uint16_t index;
  bool is_local;
};

struct CompilerState {
  struct CompilerState *enclosing;
  ObjFunction *function;
  std::vector<Local> locals;
  std::vector<Upvalue> upvalues;
  int scope_depth;
  VM *vm;

  CompilerState(CompilerState *enc, const std::string &name,
                const std::string &source_file, VM *vm_ptr)
      : enclosing(enc), scope_depth(0), vm(vm_ptr) {
    function = vm->arena().allocate<ObjFunction>();
    function->flags.fetch_or(GC_FLAG_FROZEN, std::memory_order_relaxed);
    function->name = name;
    function->source_file = source_file;
    locals.push_back({"", 0});
  }
};

class ProperCompiler : public Visitor {

  void compile_statement(Node *s) {
    if (!s)
      return;
    s->accept(this);
    if (s->pushes_value_as_statement()) {
      emit_byte(OP_POP);
    }
  }

public:
  CompilerState *state;
  int current_line = 0;
  int current_col = 0;
  std::string source_file;
  std::string current_namespace;
  bool compiling_method = false;

  std::vector<int> current_loop_starts;
  std::vector<std::vector<int>> current_loop_exits;
  std::vector<int> current_loop_depths;
  VM *vm;

  ProperCompiler(const std::string &source, VM *vm_ptr)
      : source_file(source), vm(vm_ptr) {
    state = new CompilerState(nullptr, "script", source_file, vm);
  }
  ~ProperCompiler() {
    while (state) {
      CompilerState *next = state->enclosing;
      delete state;
      state = next;
    }
  }

  ObjFunction *compile(const std::vector<Node *> &nodes) {
    for (auto *node : nodes) {
      update_loc(node);
      compile_statement(node);
    }
    emit_byte(OP_NULL);
    emit_byte(OP_RETURN);
    return state->function;
  }

  void update_loc(Node *n) {
    if (n) {
      current_line = n->line;
      current_col = n->col;
    }
  }

  void emit_byte(uint8_t byte) {
    state->function->chunk->write(byte, current_line, current_col);
  }
  void emit_bytes(uint8_t b1, uint8_t b2) {
    emit_byte(b1);
    emit_byte(b2);
  }
  void emit_short(uint16_t value) {
    emit_byte((value >> 8) & 0xff);
    emit_byte(value & 0xff);
  }

  int emit_jump(uint8_t instruction) {
    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return (int)state->function->chunk->code.size() - 2;
  }

  void patch_jump(int offset) {
    int jump = (int)state->function->chunk->code.size() - offset - 2;
    state->function->chunk->code[offset] = (jump >> 8) & 0xff;
    state->function->chunk->code[offset + 1] = jump & 0xff;
  }

  void emit_loop(int loop_start) {
    emit_byte(OP_LOOP);
    int offset = (int)state->function->chunk->code.size() - loop_start + 2;
    emit_short((uint16_t)offset);
  }

  void emit_assignment(const std::string &name) {
    int arg = resolve_local(state, name);
    if (arg != -1) {
      emit_byte(OP_SET_LOCAL);
      emit_short((uint16_t)arg);
      emit_byte(OP_POP);
    } else if ((arg = resolve_upvalue(state, name)) != -1) {
      emit_byte(OP_SET_UPVALUE);
      emit_short((uint16_t)arg);
      emit_byte(OP_POP);
    } else if (compiling_method && resolve_local(state, "self") != -1) {
      emit_byte(OP_SET_SELF_PROPERTY);
      emit_short(make_string_constant(name));
      emit_byte(OP_POP);
    } else if (state->enclosing != nullptr && state->scope_depth > 0) {
      state->locals.push_back({name, state->scope_depth});
    } else {
      emit_byte(OP_DEFINE_GLOBAL);
      emit_short(make_string_constant(name));
    }
  }

  void emit_binary_op(const std::string &op) {
    if (op == "+")
      emit_byte(OP_ADD);
    else if (op == "-")
      emit_byte(OP_SUBTRACT);
    else if (op == "*")
      emit_byte(OP_MULTIPLY);
    else if (op == "/")
      emit_byte(OP_DIVIDE);
    else if (op == "==")
      emit_byte(OP_EQUAL);
    else if (op == "!=")
      emit_byte(OP_NOT_EQUAL);
    else if (op == ">")
      emit_byte(OP_GREATER);
    else if (op == "<")
      emit_byte(OP_LESS);
    else if (op == "<=")
      emit_byte(OP_LESS_EQUAL);
    else if (op == ">=")
      emit_byte(OP_GREATER_EQUAL);
    else if (op == "%")
      emit_byte(OP_MOD);
    else if (op == "**")
      emit_byte(OP_POW);
    else if (op == "&")
      emit_byte(OP_BIT_AND);
    else if (op == "|")
      emit_byte(OP_BIT_OR);
    else if (op == "^")
      emit_byte(OP_BIT_XOR);
    else if (op == "<<")
      emit_byte(OP_LSHIFT);
    else if (op == ">>")
      emit_byte(OP_RSHIFT);
  }

  uint16_t make_constant(Value value) {
    if (value.is_obj() && value.get_obj()) {
      value.get_obj()->flags.fetch_or(GC_FLAG_FROZEN, std::memory_order_relaxed);
    }
    int index = state->function->chunk->add_constant(value);
    if (index > UINT16_MAX)
      throw CompileError("Too many constants",
                         {source_file, current_line, current_col});
    return (uint16_t)index;
  }

  uint16_t make_string_constant(const std::string &str) {
    return make_constant(Value(vm->arena().intern(str)));
  }

  void emit_constant(Value value) {
    emit_byte(OP_CONSTANT);
    emit_short(make_constant(value));
  }

  void begin_scope() { state->scope_depth++; }
  void end_scope() {
    state->scope_depth--;
    while (!state->locals.empty() &&
           state->locals.back().depth > state->scope_depth) {
      emit_byte(OP_POP);
      state->locals.pop_back();
    }
  }

  void emit_loop_exits(int target_depth) {
    int i = (int)state->locals.size() - 1;
    while (i >= 0 && state->locals[i].depth > target_depth) {
      emit_byte(OP_POP);
      i--;
    }
  }

  int resolve_local(CompilerState *c, const std::string &name) {
    for (int i = (int)c->locals.size() - 1; i >= 0; i--) {
      if (c->locals[i].name == name)
        return i;
    }
    return -1;
  }

  int add_upvalue(CompilerState *c, uint16_t index, bool is_local) {
    for (int i = 0; i < (int)c->upvalues.size(); i++) {
      if (c->upvalues[i].index == index && c->upvalues[i].is_local == is_local)
        return i;
    }
    c->upvalues.push_back({index, is_local});
    return (int)c->upvalues.size() - 1;
  }

  int resolve_upvalue(CompilerState *c, const std::string &name) {
    if (c->enclosing == nullptr)
      return -1;
    int local = resolve_local(c->enclosing, name);
    if (local != -1)
      return add_upvalue(c, (uint16_t)local, true);
    int upvalue = resolve_upvalue(c->enclosing, name);
    if (upvalue != -1)
      return add_upvalue(c, (uint16_t)upvalue, false);
    return -1;
  }

  void visit(Number *node) override {
    update_loc(node);
    emit_constant(Value(node->value));
  }
  void visit(String *node) override {
    update_loc(node);
    emit_constant(Value(vm->arena().allocate_string(std::string(node->value))));
  }
  void visit(Boolean *node) override {
    update_loc(node);
    emit_byte(node->value ? OP_TRUE : OP_FALSE);
  }

  void visit(VarAccess *node) override {
    update_loc(node);
    std::string name(node->name);
    int arg = resolve_local(state, name);
    if (arg != -1) {
      emit_byte(OP_GET_LOCAL);
      emit_short((uint16_t)arg);
    } else if ((arg = resolve_upvalue(state, name)) != -1) {
      emit_byte(OP_GET_UPVALUE);
      emit_short((uint16_t)arg);
    } else if (compiling_method && resolve_local(state, "self") != -1) {
      emit_byte(OP_GET_SELF_PROPERTY);
      emit_short(make_constant(Value(vm->arena().allocate_string(name))));
    } else {
      emit_byte(OP_GET_GLOBAL);
      emit_short(make_constant(Value(vm->arena().allocate_string(name))));
    }
  }

  void visit(Assign *node) override {
    update_loc(node);
    if (!node->value) {
      throw CompileError("Compilation aborted",
                         {source_file, current_line, current_col});
    }
    node->value->accept(this);
    emit_assignment(std::string(node->name));
  }

  void visit(TypedAssign *n) override {
    update_loc(n);
    visit(static_cast<Assign *>(n));
  }
  void visit(ConstAssign *n) override {
    update_loc(n);
    n->value->accept(this);
    emit_assignment(std::string(n->name));
  }

  void visit(BinOp *node) override {
    update_loc(node);
    if (!node->left || !node->right) {
      throw CompileError("Compilation aborted",
                         {source_file, current_line, current_col});
    }
    if (node->op == "and") {
      node->left->accept(this);
      int end_jump = emit_jump(OP_JUMP_IF_FALSE);
      emit_byte(OP_POP);
      node->right->accept(this);
      patch_jump(end_jump);
      return;
    }
    if (node->op == "or") {
      node->left->accept(this);
      int else_jump = emit_jump(OP_JUMP_IF_FALSE);
      int end_jump = emit_jump(OP_JUMP);
      patch_jump(else_jump);
      emit_byte(OP_POP);
      node->right->accept(this);
      patch_jump(end_jump);
      return;
    }
    node->left->accept(this);
    node->right->accept(this);
    emit_binary_op(std::string(node->op));
  }

  void visit(UnaryOp *node) override {
    update_loc(node);
    if (!node->right) {
      throw CompileError("Compilation aborted",
                         {source_file, current_line, current_col});
    }
    node->right->accept(this);
    if (node->op == "-")
      emit_byte(OP_NEGATE);
    else if (node->op == "not")
      emit_byte(OP_NOT);
    else if (node->op == "~")
      emit_byte(OP_BIT_NOT);
  }

  void visit(Print *node) override {
    update_loc(node);
    if (!node->expression) {
      throw CompileError("Compilation aborted",
                         {source_file, current_line, current_col});
    }
    node->expression->accept(this);
    if (node->color || node->style) {
      emit_byte(OP_CONSTANT);
      emit_short(make_constant(Value(vm->arena().allocate_string(
          std::string(node->color.value_or("none"))))));
      emit_byte(OP_CONSTANT);
      emit_short(make_constant(Value(vm->arena().allocate_string(
          std::string(node->style.value_or("none"))))));
      emit_byte(OP_PRINT_COLOR);
    } else {
      emit_byte(OP_PRINT);
    }
  }

  void visit(If *node) override {
    update_loc(node);
    if (!node->condition) {
      throw CompileError("Compilation aborted",
                         {source_file, current_line, current_col});
    }
    node->condition->accept(this);
    int then_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    for (auto *s : node->body)
      compile_statement(s);
    int else_jump = emit_jump(OP_JUMP);
    patch_jump(then_jump);
    emit_byte(OP_POP);
    for (auto *s : node->else_body)
      compile_statement(s);
    patch_jump(else_jump);
  }

  void visit(While *node) override {
    update_loc(node);
    if (!node->condition) {
      throw CompileError("Compilation aborted",
                         {source_file, current_line, current_col});
    }
    int loop_start = (int)state->function->chunk->code.size();
    current_loop_starts.push_back(loop_start);
    current_loop_exits.push_back(std::vector<int>());
    current_loop_depths.push_back(state->scope_depth);

    node->condition->accept(this);
    int exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    for (auto *s : node->body)
      compile_statement(s);
    emit_loop(loop_start);
    patch_jump(exit_jump);
    emit_byte(OP_POP);

    for (int exit : current_loop_exits.back()) {
      patch_jump(exit);
    }
    current_loop_starts.pop_back();
    current_loop_exits.pop_back();
    current_loop_depths.pop_back();
  }

  void visit(FunctionDef *node) override {
    update_loc(node);
    CompilerState *sub =
        new CompilerState(state, std::string(node->name), source_file, vm);
    CompilerState *old = state;
    state = sub;
    begin_scope();
    if (compiling_method) {
      state->locals[0] = {"self", 0};
    }
    for (auto &arg : node->args)
      state->locals.push_back(
          {std::string(std::get<0>(arg)), state->scope_depth});
    for (auto *stmt : node->body)
      compile_statement(stmt);
    // For init methods, implicitly return self (slot 1, after sentinel) instead
    // of null so that class constructors return the new instance.
    if (compiling_method && node->name == "init") {
      emit_byte(OP_GET_LOCAL);
      emit_short(0);
    } else {
      emit_byte(OP_NULL);
    }
    emit_byte(OP_RETURN);
    ObjFunction *func = state->function;
    func->arity = (int)node->args.size();
    func->upvalue_count = (int)state->upvalues.size();
    std::vector<Upvalue> uvs = state->upvalues;
    state = old;
    delete sub;

    emit_byte(OP_CLOSURE);
    emit_short(make_constant(Value(func)));
    for (auto &uv : uvs) {
      emit_byte(uv.is_local ? 1 : 0);
      emit_short(uv.index);
    }

    if (!compiling_method) {
      std::string fn_name =
          current_namespace.empty()
              ? std::string(node->name)
              : current_namespace + "::" + std::string(node->name);
      emit_byte(OP_DEFINE_GLOBAL);
      emit_short(make_constant(Value(vm->arena().allocate_string(fn_name))));
    }
  }

  void visit(Call *node) override {
    update_loc(node);
    std::string name(node->name);

    if (name == "anonymous_call" && node->callee) {
      node->callee->accept(this);
    } else {
      int arg = resolve_local(state, name);
      if (arg != -1) {
        emit_byte(OP_GET_LOCAL);
        emit_short((uint16_t)arg);
      } else if ((arg = resolve_upvalue(state, name)) != -1) {
        emit_byte(OP_GET_UPVALUE);
        emit_short((uint16_t)arg);
      } else {
        emit_byte(OP_GET_GLOBAL);
        emit_short(make_constant(Value(vm->arena().allocate_string(name))));
      }
    }

    for (auto *arg_node : node->args)
      arg_node->accept(this);

    if (!node->kwargs.empty()) {
      for (const auto &kw : node->kwargs) {
        emit_constant(
            Value(vm->arena().allocate_string(std::string(kw.first))));
        kw.second->accept(this);
      }
      emit_byte(OP_DICT);
      emit_byte((uint8_t)node->kwargs.size());
      emit_byte(OP_CALL);
      emit_byte((uint8_t)(node->args.size() + 1));
    } else {
      emit_byte(OP_CALL);
      emit_byte((uint8_t)node->args.size());
    }
  }

  void visit(SliceNode *node) override {
    update_loc(node);
    node->array->accept(this);
    if (node->start) {
      node->start->accept(this);
    } else {
      emit_byte(OP_NULL);
    }
    if (node->stop) {
      node->stop->accept(this);
    } else {
      emit_byte(OP_NULL);
    }
    if (node->step) {
      node->step->accept(this);
    } else {
      emit_byte(OP_NULL);
    }
    emit_byte(OP_SLICE);
  }

  void visit(DbInsertNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(
        make_constant(Value(vm->arena().allocate_string("std_db_insert"))));
    if (node->table)
      node->table->accept(this);
    else
      emit_byte(OP_NULL);
    if (node->data)
      node->data->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_CALL);
    emit_byte(2);
  }

  void visit(DbQueryNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(
        make_constant(Value(vm->arena().allocate_string("std_db_query_rows"))));
    if (node->query)
      node->query->accept(this);
    else
      emit_byte(OP_NULL);
    for (auto *param : node->params) {
      param->accept(this);
    }
    emit_byte(OP_CALL);
    emit_byte((uint8_t)(1 + node->params.size()));
  }

  void visit(DbFindNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(
        make_constant(Value(vm->arena().allocate_string("std_db_find"))));
    if (node->table)
      node->table->accept(this);
    else
      emit_byte(OP_NULL);
    if (node->conditions)
      node->conditions->accept(this);
    else
      emit_byte(OP_NULL);
    emit_constant(Value(node->find_all));
    emit_byte(OP_CALL);
    emit_byte(3);
  }

  void visit(DbDeleteNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(
        make_constant(Value(vm->arena().allocate_string("std_db_delete"))));
    if (node->table)
      node->table->accept(this);
    else
      emit_byte(OP_NULL);
    if (node->conditions)
      node->conditions->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_CALL);
    emit_byte(2);
  }

  void visit(WebListenNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(
        make_constant(Value(vm->arena().allocate_string("start_server"))));
    if (node->port)
      node->port->accept(this);
    else
      emit_constant(Value(8080.0));
    emit_byte(OP_CALL);
    emit_byte(1);
  }

  void visit(WebRouteNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(make_constant(
        Value(vm->arena().allocate_string("std_web_on_request"))));
    emit_constant(Value(vm->arena().allocate_string(node->method)));
    if (node->path)
      node->path->accept(this);
    else
      emit_byte(OP_NULL);
    if (node->handler)
      node->handler->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_CALL);
    emit_byte(3);
  }

  void visit(WebServeNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(
        make_constant(Value(vm->arena().allocate_string("serve_files_from"))));
    if (node->dir)
      node->dir->accept(this);
    else
      emit_byte(OP_NULL);
    if (node->route)
      node->route->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_CALL);
    emit_byte(2);
  }

  void visit(NlpAddNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(
        make_constant(Value(vm->arena().allocate_string("std_nlp_add"))));
    if (node->container)
      node->container->accept(this);
    else
      emit_byte(OP_NULL);
    if (node->item)
      node->item->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_CALL);
    emit_byte(2);
  }

  void visit(NlpRemoveNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(
        make_constant(Value(vm->arena().allocate_string("std_nlp_remove"))));
    if (node->container)
      node->container->accept(this);
    else
      emit_byte(OP_NULL);
    if (node->item)
      node->item->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_CALL);
    emit_byte(2);
  }

  void visit(NlpTimerNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(make_constant(Value(vm->arena().allocate_string(
        node->is_every ? "std_nlp_every" : "std_nlp_after"))));
    if (node->interval)
      node->interval->accept(this);
    else
      emit_constant(Value(1.0));
    emit_constant(Value(vm->arena().allocate_string(node->unit)));
    if (node->body)
      node->body->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_CALL);
    emit_byte(3);
  }

  void visit(FileWriteNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(make_constant(Value(
        vm->arena().allocate_string(node->is_append ? "append" : "write"))));
    if (node->path)
      node->path->accept(this);
    else
      emit_byte(OP_NULL);
    if (node->data)
      node->data->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_CALL);
    emit_byte(2);
  }

  void visit(FileReadNode *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(make_constant(Value(vm->arena().allocate_string("read"))));
    if (node->path)
      node->path->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_CALL);
    emit_byte(1);
  }

  void visit(Return *node) override {
    update_loc(node);
    if (node->value)
      node->value->accept(this);
    else
      emit_byte(OP_NULL);
    emit_byte(OP_RETURN);
  }

  void visit(Try *node) override {
    update_loc(node);
    int catch_jump = emit_jump(OP_TRY);
    for (auto *s : node->try_body)
      compile_statement(s);
    emit_byte(OP_END_TRY);
    int end_jump = emit_jump(OP_JUMP);
    patch_jump(catch_jump);
    begin_scope();
    state->locals.push_back({std::string(node->catch_var), state->scope_depth});
    for (auto *s : node->catch_body)
      compile_statement(s);
    end_scope();
    patch_jump(end_jump);
  }

  void visit(ListVal *node) override {
    update_loc(node);
    for (auto *e : node->elements)

      e->accept(this);
    emit_byte(OP_LIST);
    emit_byte((uint8_t)node->elements.size());
  }

  void visit(Dictionary *node) override {
    update_loc(node);
    for (auto &p : node->pairs) {
      p.first->accept(this);
      p.second->accept(this);
    }
    emit_byte(OP_DICT);
    emit_byte((uint8_t)node->pairs.size());
  }

  void visit(IndexAccess *node) override {
    update_loc(node);
    node->obj->accept(this);
    node->index->accept(this);
    emit_byte(OP_GET_INDEX);
  }
  void visit(IndexAssign *node) override {
    update_loc(node);
    node->obj->accept(this);
    node->index->accept(this);
    node->value->accept(this);
    emit_byte(OP_SET_INDEX);
  }

  void visit(PropertyAccess *node) override {
    update_loc(node);
    std::string name(node->instance_name);
    int arg = resolve_local(state, name);
    if (arg != -1) {
      emit_byte(OP_GET_LOCAL);
      emit_short((uint16_t)arg);
    } else if ((arg = resolve_upvalue(state, name)) != -1) {
      emit_byte(OP_GET_UPVALUE);
      emit_short((uint16_t)arg);
    } else if (compiling_method && resolve_local(state, "self") != -1) {
      emit_byte(OP_GET_SELF_PROPERTY);
      emit_short(make_constant(Value(vm->arena().allocate_string(name))));
    } else {
      emit_byte(OP_GET_GLOBAL);
      emit_short(make_constant(Value(vm->arena().allocate_string(name))));
    }

    emit_byte(OP_GET_PROPERTY);
    emit_short(make_constant(
        Value(vm->arena().allocate_string(std::string(node->property_name)))));
  }

  void visit(PropertyAssign *node) override {
    update_loc(node);
    std::string name(node->instance_name);
    int arg = resolve_local(state, name);
    if (arg != -1) {
      emit_byte(OP_GET_LOCAL);
      emit_short((uint16_t)arg);
    } else if ((arg = resolve_upvalue(state, name)) != -1) {
      emit_byte(OP_GET_UPVALUE);
      emit_short((uint16_t)arg);
    } else if (compiling_method && resolve_local(state, "self") != -1) {
      emit_byte(OP_GET_SELF_PROPERTY);
      emit_short(make_constant(Value(vm->arena().allocate_string(name))));
    } else {
      emit_byte(OP_GET_GLOBAL);
      emit_short(make_constant(Value(vm->arena().allocate_string(name))));
    }

    node->value->accept(this);
    emit_byte(OP_SET_PROPERTY);
    emit_short(make_constant(
        Value(vm->arena().allocate_string(std::string(node->property_name)))));
  }

  void visit(MethodCall *node) override {
    update_loc(node);
    std::string name(node->instance_name);
    int arg = resolve_local(state, name);
    if (arg != -1) {
      emit_byte(OP_GET_LOCAL);
      emit_short((uint16_t)arg);
    } else if ((arg = resolve_upvalue(state, name)) != -1) {
      emit_byte(OP_GET_UPVALUE);
      emit_short((uint16_t)arg);
    } else if (compiling_method && resolve_local(state, "self") != -1) {
      emit_byte(OP_GET_SELF_PROPERTY);
      emit_short(make_constant(Value(vm->arena().allocate_string(name))));
    } else {
      emit_byte(OP_GET_GLOBAL);
      emit_short(make_constant(Value(vm->arena().allocate_string(name))));
    }

    for (auto *a : node->args)

      a->accept(this);
    emit_byte(OP_INVOKE);
    emit_short(make_constant(
        Value(vm->arena().allocate_string(std::string(node->method_name)))));
    emit_byte((uint8_t)node->args.size());
  }

  void visit(ForIn *node) override {
    update_loc(node);
    node->iterable->accept(this);
    emit_byte(OP_GET_ITER);
    state->locals.push_back({"_iter", state->scope_depth});

    int loop_start = (int)state->function->chunk->code.size();
    current_loop_starts.push_back(loop_start);
    current_loop_exits.push_back(std::vector<int>());
    current_loop_depths.push_back(state->scope_depth);

    int exit_jump = emit_jump(OP_FOR_ITER);
    begin_scope();
    state->locals.push_back({std::string(node->var_name), state->scope_depth});
    for (auto *s : node->body)
      compile_statement(s);
    end_scope();

    emit_loop(loop_start);

    patch_jump(exit_jump);
    for (int exit : current_loop_exits.back()) {
      patch_jump(exit);
    }
    emit_byte(OP_POP);
    state->locals.pop_back();

    current_loop_starts.pop_back();
    current_loop_exits.pop_back();
    current_loop_depths.pop_back();
  }

  void visit(ClassDef *node) override {
    update_loc(node);
    emit_byte(OP_CLASS);
    emit_short(make_constant(
        Value(vm->arena().allocate_string(std::string(node->name)))));
    for (auto &prop : node->properties) {
      emit_byte(OP_PROPERTY);
      emit_short(make_constant(
          Value(vm->arena().allocate_string(std::string(prop.first)))));
    }
    for (auto *m : node->methods) {
      compiling_method = true;
      m->accept(this);
      compiling_method = false;
      emit_byte(OP_METHOD);
      emit_short(make_constant(
          Value(vm->arena().allocate_string(std::string(m->name)))));
    }
    emit_byte(OP_DEFINE_GLOBAL);
    emit_short(make_constant(
        Value(vm->arena().allocate_string(std::string(node->name)))));
  }

  void visit(Instantiation *node) override {
    update_loc(node);
    emit_byte(OP_GET_GLOBAL);
    emit_short(make_constant(
        Value(vm->arena().allocate_string(std::string(node->class_name)))));
    for (auto *arg : node->args)

      arg->accept(this);
    emit_byte(OP_CALL);
    emit_byte((uint8_t)node->args.size());
  }

  void visit(Spawn *node) override {
    update_loc(node);
    if (!node->call) {
      emit_byte(OP_NULL);
      return;
    }
    if (Call *c = dynamic_cast<Call *>(node->call)) {
      std::string name(c->name);
      int arg = resolve_local(state, name);
      if (arg != -1) {
        emit_byte(OP_GET_LOCAL);
        emit_short((uint16_t)arg);
      } else if ((arg = resolve_upvalue(state, name)) != -1) {
        emit_byte(OP_GET_UPVALUE);
        emit_short((uint16_t)arg);
      } else {
        emit_byte(OP_GET_GLOBAL);
        emit_short(make_constant(Value(vm->arena().allocate_string(name))));
      }

      for (auto *a : c->args)
        a->accept(this);
      emit_byte(OP_SPAWN);
      emit_byte((uint8_t)c->args.size());
    } else {
      node->call->accept(this);
      emit_byte(OP_SPAWN);
      emit_byte(0);
    }
  }

  void visit(Match *node) override {
    update_loc(node);
    node->match_expr->accept(this);
    std::vector<int> end_jumps;
    for (auto &c : node->cases) {
      emit_byte(OP_DUP);
      c.first->accept(this);
      emit_byte(OP_EQUAL);
      int next_jump = emit_jump(OP_JUMP_IF_FALSE);
      emit_byte(OP_POP);
      emit_byte(OP_POP);
      for (auto *s : c.second)

        s->accept(this);
      end_jumps.push_back(emit_jump(OP_JUMP));
      patch_jump(next_jump);
      emit_byte(OP_POP);
    }
    if (!node->default_case.empty()) {
      emit_byte(OP_POP);
      for (auto *s : node->default_case)

        s->accept(this);
    } else {
      emit_byte(OP_POP);
    }
    for (int j : end_jumps)
      patch_jump(j);
  }

  void visit(ListComprehension *node) override {
    update_loc(node);
    emit_byte(OP_LIST);
    emit_byte(0);
    state->locals.push_back({"", state->scope_depth});

    node->iterable->accept(this);
    emit_byte(OP_GET_ITER);
    state->locals.push_back({"", state->scope_depth});

    int loop_start = (int)state->function->chunk->code.size();
    current_loop_starts.push_back(loop_start);
    current_loop_exits.push_back(std::vector<int>());
    current_loop_depths.push_back(state->scope_depth);

    int exit_jump = emit_jump(OP_FOR_ITER);
    begin_scope();
    state->locals.push_back({std::string(node->var_name), state->scope_depth});
    int skip = -1;
    if (node->condition) {
      node->condition->accept(this);
      skip = emit_jump(OP_JUMP_IF_FALSE);
      emit_byte(OP_POP);
    }
    node->expr->accept(this);
    emit_byte(OP_LIST_APPEND);
    emit_byte(2);
    if (skip != -1) {
      patch_jump(skip);
      emit_byte(OP_POP);
    }
    end_scope();
    emit_loop(loop_start);
    patch_jump(exit_jump);
    for (int exit : current_loop_exits.back()) {
      patch_jump(exit);
    }
    emit_byte(OP_POP);

    state->locals.pop_back();
    state->locals.pop_back();

    current_loop_starts.pop_back();
    current_loop_exits.pop_back();
    current_loop_depths.pop_back();
  }

  void visit(AnonymousFunction *node) override {
    update_loc(node);
    CompilerState *sub = new CompilerState(state, "anonymous", source_file, vm);
    CompilerState *old = state;
    state = sub;
    begin_scope();
    for (auto &arg : node->args)
      state->locals.push_back({std::string(arg), state->scope_depth});
    if (std::holds_alternative<Node *>(node->body))
      std::get<Node *>(node->body)->accept(this);
    else
      for (auto *s : std::get<std::vector<Node *>>(node->body))
        s->accept(this);
    emit_byte(OP_RETURN);
    ObjFunction *func = state->function;
    func->arity = (int)node->args.size();
    func->upvalue_count = (int)state->upvalues.size();
    std::vector<Upvalue> uvs = state->upvalues;
    state = old;
    delete sub;
    emit_byte(OP_CLOSURE);
    emit_short(make_constant(Value(func)));
    for (auto &uv : uvs) {
      emit_byte(uv.is_local ? 1 : 0);
      emit_short(uv.index);
    }
  }

  void visit(Throw *n) override {
    update_loc(n);
    n->message->accept(this);
    emit_byte(OP_THROW);
  }

  void visit(Repeat *n) override {
    update_loc(n);
    n->count->accept(this);
    emit_constant(Value(0.0));
    state->locals.push_back({"", state->scope_depth});
    state->locals.push_back({"", state->scope_depth});
    uint16_t count_slot = (uint16_t)(state->locals.size() - 2);
    uint16_t idx_slot = (uint16_t)(state->locals.size() - 1);

    int loop_start = (int)state->function->chunk->code.size();
    current_loop_starts.push_back(loop_start);
    current_loop_exits.push_back(std::vector<int>());
    current_loop_depths.push_back(state->scope_depth);

    emit_byte(OP_GET_LOCAL);
    emit_short(idx_slot);
    emit_byte(OP_GET_LOCAL);
    emit_short(count_slot);
    emit_byte(OP_LESS);

    int exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);

    for (auto *s : n->body)
      compile_statement(s);

    emit_byte(OP_GET_LOCAL);
    emit_short(idx_slot);
    emit_constant(Value(1.0));
    emit_byte(OP_ADD);
    emit_byte(OP_SET_LOCAL);
    emit_short(idx_slot);
    emit_byte(OP_POP);

    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OP_POP);

    for (int exit : current_loop_exits.back())
      patch_jump(exit);

    emit_byte(OP_POP);
    emit_byte(OP_POP);

    state->locals.pop_back();
    state->locals.pop_back();

    current_loop_starts.pop_back();
    current_loop_exits.pop_back();
    current_loop_depths.pop_back();
  }
  void visit(Unless *n) override {
    update_loc(n);
    n->condition->accept(this);
    int run_body = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    int end_jump = emit_jump(OP_JUMP);
    patch_jump(run_body);
    emit_byte(OP_POP);
    for (auto *s : n->body)
      compile_statement(s);
    patch_jump(end_jump);
  }
  void visit(Forever *n) override {
    update_loc(n);
    int start = (int)state->function->chunk->code.size();
    current_loop_starts.push_back(start);
    current_loop_exits.push_back(std::vector<int>());
    current_loop_depths.push_back(state->scope_depth);
    for (auto *s : n->body)
      compile_statement(s);
    emit_loop(start);
    for (int exit : current_loop_exits.back())
      patch_jump(exit);
    current_loop_starts.pop_back();
    current_loop_exits.pop_back();
    current_loop_depths.pop_back();
  }
  void visit(Until *n) override {
    update_loc(n);
    int loop_start = (int)state->function->chunk->code.size();
    current_loop_starts.push_back(loop_start);
    current_loop_exits.push_back(std::vector<int>());
    current_loop_depths.push_back(state->scope_depth);
    n->condition->accept(this);
    int run_body = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    int exit_jump = emit_jump(OP_JUMP);
    patch_jump(run_body);
    emit_byte(OP_POP);
    for (auto *s : n->body)
      compile_statement(s);
    emit_loop(loop_start);
    patch_jump(exit_jump);
    for (int exit : current_loop_exits.back())
      patch_jump(exit);
    current_loop_starts.pop_back();
    current_loop_exits.pop_back();
    current_loop_depths.pop_back();
  }

  void visit(Import *node) override {
    update_loc(node);
    emit_constant(Value(vm->arena().allocate_string(std::string(node->path))));
    emit_byte(OP_NULL);
    emit_byte(OP_IMPORT);
    emit_byte(OP_POP);
  }

  void visit(ImportAs *node) override {
    update_loc(node);
    emit_constant(Value(vm->arena().allocate_string(std::string(node->path))));
    emit_constant(Value(vm->arena().allocate_string(std::string(node->alias))));
    emit_byte(OP_IMPORT);
    emit_byte(OP_POP);
  }

  void visit(FromImport *node) override {
    update_loc(node);
    emit_constant(
        Value(vm->arena().allocate_string(std::string(node->module_name))));
    emit_byte(OP_NULL);
    emit_byte(OP_IMPORT);

    for (auto &pair : node->names) {
      emit_byte(OP_DUP);
      emit_byte(OP_GET_PROPERTY);
      emit_short(make_constant(
          Value(vm->arena().allocate_string(std::string(pair.first)))));

      std::string alias = pair.second.has_value()
                              ? std::string(pair.second.value())
                              : std::string(pair.first);
      emit_byte(OP_DEFINE_GLOBAL);
      emit_short(make_constant(Value(vm->arena().allocate_string(alias))));
    }
    emit_byte(OP_POP);
  }

  void visit(PythonImport *node) override {
    update_loc(node);

    emit_constant(
        Value(vm->arena().allocate_string(std::string(node->module_name))));
    if (node->alias)
      emit_constant(
          Value(vm->arena().allocate_string(std::string(*node->alias))));
    else
      emit_byte(OP_NULL);
    emit_byte(OP_IMPORT);
    emit_byte(OP_POP);
  }

  void visit(Stop *n) override {
    update_loc(n);
    if (!current_loop_exits.empty()) {
      emit_loop_exits(current_loop_depths.back());
      current_loop_exits.back().push_back(emit_jump(OP_JUMP));
    } else {
      throw CompileError("Syntax error: 'stop' outside loop at line " +
                         std::to_string(current_line));
    }
  }
  void visit(Skip *n) override {
    update_loc(n);
    if (!current_loop_starts.empty()) {
      emit_loop_exits(current_loop_depths.back());
      emit_loop(current_loop_starts.back());
    } else {
      throw CompileError("Syntax error: 'skip' outside loop at line " +
                         std::to_string(current_line));
    }
  }

  void visit(Parallel *n) override {
    update_loc(n);
    emit_byte(OP_LIST);
    emit_byte(0); // empty list
    for (auto *s : n->body) {
      CompilerState *old = state;
      CompilerState *sub =
          new CompilerState(state, "parallel_block", source_file, vm);
      state = sub;
      begin_scope();
      s->accept(this);
      emit_byte(OP_NULL);
      emit_byte(OP_RETURN);
      ObjFunction *func = state->function;
      func->arity = 0;
      func->upvalue_count = (int)state->upvalues.size();
      std::vector<Upvalue> uvs = state->upvalues;
      state = old;
      delete sub;

      emit_byte(OP_CLOSURE);
      emit_short(make_constant(Value(func)));
      for (auto &uv : uvs) {
        emit_byte(uv.is_local ? 1 : 0);
        emit_short(uv.index);
      }

      emit_byte(OP_SPAWN);
      emit_byte(0);

      emit_byte(OP_LIST_APPEND);
      emit_byte(0);
    }
    call_native("gather", 1);
    emit_byte(OP_POP);
  }

  void visit(TryAlways *n) override {
    update_loc(n);
    int try_jump = emit_jump(OP_TRY);
    for (auto *s : n->try_body)
      compile_statement(s);
    emit_byte(OP_END_TRY);

    for (auto *s : n->always_body)
      compile_statement(s);
    int exit_jump = emit_jump(OP_JUMP);

    patch_jump(try_jump);
    if (!n->catch_body.empty()) {
      begin_scope();
      state->locals.push_back({std::string(n->catch_var), state->scope_depth});
      for (auto *s : n->catch_body)
        compile_statement(s);
      end_scope();
    } else {
      emit_byte(OP_POP);
    }
    for (auto *s : n->always_body)
      compile_statement(s);
    patch_jump(exit_jump);
  }

  void visit(For *n) override {
    update_loc(n);
    n->count->accept(this);
    emit_constant(Value(0.0));
    state->locals.push_back({"", state->scope_depth});
    state->locals.push_back({"", state->scope_depth});
    uint16_t count_slot = (uint16_t)(state->locals.size() - 2);
    uint16_t idx_slot = (uint16_t)(state->locals.size() - 1);

    int loop_start = (int)state->function->chunk->code.size();
    current_loop_starts.push_back(loop_start);
    current_loop_exits.push_back(std::vector<int>());
    current_loop_depths.push_back(state->scope_depth);

    emit_byte(OP_GET_LOCAL);
    emit_short(idx_slot);
    emit_byte(OP_GET_LOCAL);
    emit_short(count_slot);
    emit_byte(OP_LESS);

    int exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);

    for (auto *s : n->body)
      compile_statement(s);

    emit_byte(OP_GET_LOCAL);
    emit_short(idx_slot);
    emit_constant(Value(1.0));
    emit_byte(OP_ADD);
    emit_byte(OP_SET_LOCAL);
    emit_short(idx_slot);
    emit_byte(OP_POP);

    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OP_POP);

    for (int exit : current_loop_exits.back())
      patch_jump(exit);

    emit_byte(OP_POP);
    emit_byte(OP_POP);

    state->locals.pop_back();
    state->locals.pop_back();

    current_loop_starts.pop_back();
    current_loop_exits.pop_back();
    current_loop_depths.pop_back();
  }

  void visit(NamespaceDecl *node) override {
    update_loc(node);
    std::string old_ns = current_namespace;
    if (current_namespace.empty()) {
      current_namespace = node->name;
    } else {
      current_namespace = current_namespace + "::" + node->name;
    }
    for (auto *stmt : node->body) {
      compile_statement(stmt);
    }
    current_namespace = old_ns;
  }

  void call_native(const std::string &name, uint8_t arg_count) {
    emit_byte(OP_GET_GLOBAL);
    emit_short(make_string_constant(name));
    emit_byte(OP_NATIVE_CALL);
    emit_byte(arg_count);
  }
};

Compiler::Compiler(VM *vm) : vm_(vm) {}

ObjFunction *Compiler::compile(const std::string &name,
                               const std::vector<Node *> &nodes) {
  ProperCompiler compiler(name, vm_);
  return compiler.compile(nodes);
}

} // namespace shell_lite

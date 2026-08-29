#pragma once
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <string_view>
#include <tuple>

namespace shell_lite {

struct Number; struct String; struct VarAccess; struct Assign;
struct TypedAssign; struct PropertyAssign; struct UnaryOp; struct BinOp; struct Print;
struct If; struct While; struct ForIn; struct ListVal; struct Dictionary;
struct Boolean; struct FunctionDef; struct AnonymousFunction; struct Call; struct Return;
struct ClassDef; struct Instantiation; struct MethodCall; struct PropertyAccess;
struct Import; struct ImportAs; struct Try; struct TryAlways; struct Match;
struct ListComprehension; struct ConstAssign; struct IndexAccess; struct IndexAssign;
struct Stop; struct Skip; struct Throw; struct PythonImport; struct FromImport;
struct For; struct Unless; struct Repeat; struct Forever; struct Until;
struct Spawn; struct Parallel;
struct SliceNode; struct DbInsertNode; struct DbQueryNode; struct DbFindNode; struct DbDeleteNode;
struct WebListenNode; struct WebRouteNode; struct WebServeNode;
struct NlpAddNode; struct NlpRemoveNode; struct NlpTimerNode;
struct FileWriteNode; struct FileReadNode;
struct NamespaceDecl;
class Visitor {
public:
    virtual ~Visitor() = default;
    virtual void visit(Number* node) = 0;
    virtual void visit(String* node) = 0;
    virtual void visit(VarAccess* node) = 0;
    virtual void visit(Assign* node) = 0;
    virtual void visit(TypedAssign* node) = 0;
    virtual void visit(PropertyAssign* node) = 0;
    virtual void visit(UnaryOp* node) = 0;
    virtual void visit(BinOp* node) = 0;
    virtual void visit(Print* node) = 0;
    virtual void visit(If* node) = 0;
    virtual void visit(While* node) = 0;
    virtual void visit(ForIn* node) = 0;
    virtual void visit(ListVal* node) = 0;
    virtual void visit(Dictionary* node) = 0;
    virtual void visit(Boolean* node) = 0;
    virtual void visit(FunctionDef* node) = 0;
    virtual void visit(AnonymousFunction* node) = 0;
    virtual void visit(Call* node) = 0;
    virtual void visit(Return* node) = 0;
    virtual void visit(ClassDef* node) = 0;
    virtual void visit(Instantiation* node) = 0;
    virtual void visit(MethodCall* node) = 0;
    virtual void visit(PropertyAccess* node) = 0;
    virtual void visit(Import* node) = 0;
    virtual void visit(ImportAs* node) = 0;
    virtual void visit(Try* node) = 0;
    virtual void visit(TryAlways* node) = 0;
    virtual void visit(Match* node) = 0;
    virtual void visit(ListComprehension* node) = 0;
    virtual void visit(ConstAssign* node) = 0;
    virtual void visit(IndexAccess* node) = 0;
    virtual void visit(IndexAssign* node) = 0;
    virtual void visit(Stop* node) = 0;
    virtual void visit(Skip* node) = 0;
    virtual void visit(Throw* node) = 0;
    virtual void visit(PythonImport* node) = 0;
    virtual void visit(FromImport* node) = 0;
    virtual void visit(For* node) = 0;
    virtual void visit(Unless* node) = 0;
    virtual void visit(Repeat* node) = 0;
    virtual void visit(Forever* node) = 0;
    virtual void visit(Until* node) = 0;
    virtual void visit(Spawn* node) = 0;
    virtual void visit(Parallel* node) = 0;
    virtual void visit(SliceNode* node) = 0;
    virtual void visit(DbInsertNode* node) = 0;
    virtual void visit(DbQueryNode* node) = 0;
    virtual void visit(DbFindNode* node) = 0;
    virtual void visit(DbDeleteNode* node) = 0;
    virtual void visit(WebListenNode* node) = 0;
    virtual void visit(WebRouteNode* node) = 0;
    virtual void visit(WebServeNode* node) = 0;
    virtual void visit(NlpAddNode* node) = 0;
    virtual void visit(NlpRemoveNode* node) = 0;
    virtual void visit(NlpTimerNode* node) = 0;
    virtual void visit(FileWriteNode* node) = 0;
    virtual void visit(FileReadNode* node) = 0;
    virtual void visit(NamespaceDecl* node) = 0;
};

struct Node {
    int line = 0;
    int col = 0;
    int end_line = 0;
    int end_col = 0;
    void* type_info = nullptr;
    void* symbol_ref = nullptr;
    virtual std::string node_type() const { return "Node"; }
    virtual bool pushes_value_as_statement() const { return false; }
    virtual void accept(Visitor* v) = 0;
    virtual ~Node() = default;
};

struct Number : Node {
    double value;
    explicit Number(double v = 0) : value(v) {}
    std::string node_type() const override { return "Number"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct String : Node {
    std::string_view value;
    explicit String(std::string_view v = "") : value(v) {}
    std::string node_type() const override { return "String"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct VarAccess : Node {
    std::string_view name;
    explicit VarAccess(std::string_view n = "") : name(n) {}
    std::string node_type() const override { return "VarAccess"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Assign : Node {
    std::string_view name;
    Node* value;
    Assign(std::string_view n = "", Node* v = nullptr) : name(n), value(v) {}
    std::string node_type() const override { return "Assign"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct TypedAssign : Assign {
    std::string_view type_hint;
    TypedAssign() : Assign("", nullptr) {}
    std::string node_type() const override { return "TypedAssign"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct PropertyAssign : Node {
    std::string_view instance_name;
    std::string_view property_name;
    Node* value;
    std::string node_type() const override { return "PropertyAssign"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct UnaryOp : Node {
    std::string_view op;
    Node* right;
    UnaryOp(std::string_view o = "", Node* r = nullptr) : op(o), right(r) {}
    std::string node_type() const override { return "UnaryOp"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct BinOp : Node {
    Node* left = nullptr;
    std::string_view op;
    Node* right = nullptr;
    BinOp() = default;
    BinOp(Node* l, std::string_view o, Node* r) : left(l), op(o), right(r) {}
    std::string node_type() const override { return "BinOp"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Print : Node {
    Node* expression;
    std::optional<std::string_view> style;
    std::optional<std::string_view> color;
    explicit Print(Node* e = nullptr) : expression(e) {}
    std::string node_type() const override { return "Print"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct If : Node {
    Node* condition;
    std::vector<Node*> body;
    std::vector<Node*> else_body;
    explicit If(Node* c = nullptr, std::vector<Node*> b = {}, std::vector<Node*> eb = {}) : condition(c), body(b), else_body(eb) {}
    std::string node_type() const override { return "If"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct While : Node {
    Node* condition;
    std::vector<Node*> body;
    explicit While(Node* c = nullptr, std::vector<Node*> b = {}) : condition(c), body(b) {}
    std::string node_type() const override { return "While"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct ForIn : Node {
    std::string_view var_name;
    Node* iterable;
    std::vector<Node*> body;
    ForIn(std::string_view v = "", Node* i = nullptr) : var_name(v), iterable(i) {}
    std::string node_type() const override { return "ForIn"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct ListVal : Node {
    std::vector<Node*> elements;
    explicit ListVal(const std::vector<Node*>& e = {}) : elements(e) {}
    std::string node_type() const override { return "ListVal"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Dictionary : Node {
    std::vector<std::pair<Node*, Node*>> pairs;
    explicit Dictionary(const std::vector<std::pair<Node*, Node*>>& p = {}) : pairs(p) {}
    std::string node_type() const override { return "Dictionary"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Boolean : Node {
    bool value;
    explicit Boolean(bool v = false) : value(v) {}
    std::string node_type() const override { return "Boolean"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct FunctionDef : Node {
    std::string_view name;
    std::vector<std::tuple<std::string_view, Node*, std::optional<std::string_view>>> args;
    std::vector<Node*> body;
    std::optional<std::string_view> return_type;
    explicit FunctionDef(std::string_view n = "") : name(n) {}
    std::string node_type() const override { return "FunctionDef"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct AnonymousFunction : Node {
    std::vector<std::string_view> args;
    std::variant<Node*, std::vector<Node*>> body;
    AnonymousFunction(std::vector<std::string_view> a = {}, std::variant<Node*, std::vector<Node*>> b = std::vector<Node*>{}) : args(a), body(b) {}
    std::string node_type() const override { return "AnonymousFunction"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Call : Node {
    std::string_view name;
    Node* callee = nullptr;
    std::vector<Node*> args;
    std::vector<std::pair<std::string_view, Node*>> kwargs;
    std::vector<Node*> body;
    explicit Call(std::string_view n = "") : name(n) {}
    std::string node_type() const override { return "Call"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Return : Node {
    Node* value;
    explicit Return(Node* v = nullptr) : value(v) {}
    std::string node_type() const override { return "Return"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct ClassDef : Node {
    std::string_view name;
    std::vector<std::pair<std::string_view, Node*>> properties;
    std::vector<FunctionDef*> methods;
    std::optional<std::string_view> parent;
    explicit ClassDef(std::string_view n = "") : name(n) {}
    std::string node_type() const override { return "ClassDef"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Instantiation : Node {
    std::optional<std::string_view> var_name;
    std::string_view class_name;
    std::vector<Node*> args;
    std::vector<std::pair<std::string_view, Node*>> kwargs;
    std::string node_type() const override { return "Instantiation"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct MethodCall : Node {
    std::string_view instance_name;
    std::string_view method_name;
    std::vector<Node*> args;
    std::vector<std::pair<std::string_view, Node*>> kwargs;
    std::string node_type() const override { return "MethodCall"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct PropertyAccess : Node {
    std::string_view instance_name;
    std::string_view property_name;
    PropertyAccess(std::string_view i = "", std::string_view p = "") : instance_name(i), property_name(p) {}
    std::string node_type() const override { return "PropertyAccess"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Import : Node {
    std::string_view path;
    std::string node_type() const override { return "Import"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct ImportAs : Node {
    std::string_view path;
    std::string_view alias;
    std::string node_type() const override { return "ImportAs"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Try : Node {
    std::vector<Node*> try_body;
    std::string_view catch_var;
    std::vector<Node*> catch_body;
    explicit Try() = default;
    std::string node_type() const override { return "Try"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct TryAlways : Node {
    std::vector<Node*> try_body;
    std::string_view catch_var;
    std::vector<Node*> catch_body;
    std::vector<Node*> always_body;
    std::string node_type() const override { return "TryAlways"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Match : Node {
    Node* match_expr;
    std::vector<std::pair<Node*, std::vector<Node*>>> cases;
    std::vector<Node*> default_case;
    std::string node_type() const override { return "Match"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct ListComprehension : Node {
    Node* expr;
    std::string_view var_name;
    Node* iterable;
    Node* condition = nullptr;
    std::string node_type() const override { return "ListComprehension"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct ConstAssign : Node {
    std::string_view name;
    Node* value;
    ConstAssign(std::string_view n = "", Node* v = nullptr) : name(n), value(v) {}
    std::string node_type() const override { return "ConstAssign"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct IndexAccess : Node {
    Node* obj;
    Node* index;
    IndexAccess(Node* o = nullptr, Node* i = nullptr) : obj(o), index(i) {}
    std::string node_type() const override { return "IndexAccess"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct IndexAssign : Node {
    Node* obj;
    Node* index;
    Node* value;
    IndexAssign(Node* o = nullptr, Node* i = nullptr, Node* v = nullptr) : obj(o), index(i), value(v) {}
    std::string node_type() const override { return "IndexAssign"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Stop : Node {
    std::string node_type() const override { return "Stop"; }
    void accept(Visitor* v) override { v->visit(this); }
};
struct Skip : Node {
    std::string node_type() const override { return "Skip"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Throw : Node {
    Node* message;
    explicit Throw(Node* m = nullptr) : message(m) {}
    std::string node_type() const override { return "Throw"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct PythonImport : Node {
    std::string_view module_name;
    std::optional<std::string_view> alias;
    std::string node_type() const override { return "PythonImport"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct FromImport : Node {
    std::string_view module_name;
    std::vector<std::pair<std::string_view, std::optional<std::string_view>>> names;
    std::string node_type() const override { return "FromImport"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct For : Node {
    Node* count;
    std::vector<Node*> body;
    std::string node_type() const override { return "For"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Unless : Node {
    Node* condition;
    std::vector<Node*> body;
    std::string node_type() const override { return "Unless"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Repeat : Node {
    Node* count;
    std::vector<Node*> body;
    explicit Repeat(Node* c = nullptr) : count(c) {}
    std::string node_type() const override { return "Repeat"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Forever : Node {
    std::vector<Node*> body;
    explicit Forever() = default;
    std::string node_type() const override { return "Forever"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Until : Node {
    Node* condition;
    std::vector<Node*> body;
    std::string node_type() const override { return "Until"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Spawn : Node {
    Node* call;
    explicit Spawn(Node* c = nullptr) : call(c) {}
    std::string node_type() const override { return "Spawn"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct Parallel : Node {
    std::vector<Node*> body;
    explicit Parallel() = default;
    std::string node_type() const override { return "Parallel"; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct SliceNode : Node {
    Node* array = nullptr;
    Node* start = nullptr;
    Node* stop = nullptr;
    Node* step = nullptr;
    SliceNode(Node* arr = nullptr, Node* st = nullptr, Node* sp = nullptr, Node* sk = nullptr)
        : array(arr), start(st), stop(sp), step(sk) {}
    std::string node_type() const override { return "SliceNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct DbInsertNode : Node {
    Node* table = nullptr;
    Node* data = nullptr;
    DbInsertNode(Node* t = nullptr, Node* d = nullptr) : table(t), data(d) {}
    std::string node_type() const override { return "DbInsertNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct DbQueryNode : Node {
    Node* query = nullptr;
    std::vector<Node*> params;
    DbQueryNode(Node* q = nullptr, std::vector<Node*> p = {}) : query(q), params(p) {}
    std::string node_type() const override { return "DbQueryNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct DbFindNode : Node {
    Node* table = nullptr;
    Node* conditions = nullptr;
    bool find_all = true;
    DbFindNode(Node* t = nullptr, Node* c = nullptr, bool all = true) : table(t), conditions(c), find_all(all) {}
    std::string node_type() const override { return "DbFindNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct DbDeleteNode : Node {
    Node* table = nullptr;
    Node* conditions = nullptr;
    DbDeleteNode(Node* t = nullptr, Node* c = nullptr) : table(t), conditions(c) {}
    std::string node_type() const override { return "DbDeleteNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct WebListenNode : Node {
    Node* port = nullptr;
    explicit WebListenNode(Node* p = nullptr) : port(p) {}
    std::string node_type() const override { return "WebListenNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct WebRouteNode : Node {
    std::string method;
    Node* path = nullptr;
    Node* handler = nullptr;
    WebRouteNode(std::string m = "GET", Node* p = nullptr, Node* h = nullptr) : method(std::move(m)), path(p), handler(h) {}
    std::string node_type() const override { return "WebRouteNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct WebServeNode : Node {
    Node* route = nullptr;
    Node* dir = nullptr;
    WebServeNode(Node* r = nullptr, Node* d = nullptr) : route(r), dir(d) {}
    std::string node_type() const override { return "WebServeNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct NlpAddNode : Node {
    Node* item = nullptr;
    Node* container = nullptr;
    NlpAddNode(Node* i = nullptr, Node* c = nullptr) : item(i), container(c) {}
    std::string node_type() const override { return "NlpAddNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct NlpRemoveNode : Node {
    Node* item = nullptr;
    Node* container = nullptr;
    NlpRemoveNode(Node* i = nullptr, Node* c = nullptr) : item(i), container(c) {}
    std::string node_type() const override { return "NlpRemoveNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct NlpTimerNode : Node {
    Node* interval = nullptr;
    std::string unit = "seconds";
    Node* body = nullptr;
    bool is_every = true;
    NlpTimerNode(Node* iv = nullptr, std::string u = "seconds", Node* b = nullptr, bool every = true)
        : interval(iv), unit(std::move(u)), body(b), is_every(every) {}
    std::string node_type() const override { return "NlpTimerNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct FileWriteNode : Node {
    Node* path = nullptr;
    Node* data = nullptr;
    bool is_append = false;
    FileWriteNode(Node* p = nullptr, Node* d = nullptr, bool append = false) : path(p), data(d), is_append(append) {}
    std::string node_type() const override { return "FileWriteNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct FileReadNode : Node {
    Node* path = nullptr;
    explicit FileReadNode(Node* p = nullptr) : path(p) {}
    std::string node_type() const override { return "FileReadNode"; }
    bool pushes_value_as_statement() const override { return true; }
    void accept(Visitor* v) override { v->visit(this); }
};

struct NamespaceDecl : Node {
    std::string name;
    std::vector<Node*> body;
    NamespaceDecl(std::string n = "", std::vector<Node*> b = {}) : name(std::move(n)), body(std::move(b)) {}
    std::string node_type() const override { return "NamespaceDecl"; }
    void accept(Visitor* v) override { v->visit(this); }
};

}

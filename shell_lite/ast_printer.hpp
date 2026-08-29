#pragma once

#include "ast_nodes.hpp"
#include <iostream>
#include <vector>
#include <string>

namespace shell_lite {

class AstPrinter : public Visitor {
public:
    explicit AstPrinter(std::ostream &out = std::cout) : out_(out) {}

    void print(const std::vector<Node*> &nodes) {
        out_ << "[\n";
        for (size_t i = 0; i < nodes.size(); ++i) {
            indent_level_ = 1;
            print_indent();
            if (nodes[i]) {
                nodes[i]->accept(this);
            } else {
                out_ << "null";
            }
            if (i + 1 < nodes.size()) out_ << ",";
            out_ << "\n";
        }
        out_ << "]\n";
    }

    void visit(Number* node) override {
        out_ << "{\"type\": \"Number\", \"value\": " << node->value << "}";
    }

    void visit(String* node) override {
        out_ << "{\"type\": \"String\", \"value\": \"" << escape_string(std::string(node->value)) << "\"}";
    }

    void visit(VarAccess* node) override {
        out_ << "{\"type\": \"VarAccess\", \"name\": \"" << node->name << "\"}";
    }

    void visit(Assign* node) override {
        out_ << "{\"type\": \"Assign\", \"name\": \"" << node->name << "\", \"value\": ";
        if (node->value) node->value->accept(this);
        else out_ << "null";
        out_ << "}";
    }

    void visit(TypedAssign* node) override {
        out_ << "{\"type\": \"TypedAssign\", \"name\": \"" << node->name 
             << "\", \"type_hint\": \"" << node->type_hint << "\", \"value\": ";
        if (node->value) node->value->accept(this);
        else out_ << "null";
        out_ << "}";
    }

    void visit(PropertyAssign* node) override {
        out_ << "{\"type\": \"PropertyAssign\", \"instance\": \"" << node->instance_name 
             << "\", \"property\": \"" << node->property_name << "\"}";
    }

    void visit(UnaryOp* node) override {
        out_ << "{\"type\": \"UnaryOp\", \"op\": \"" << node->op << "\", \"operand\": ";
        if (node->right) node->right->accept(this);
        else out_ << "null";
        out_ << "}";
    }

    void visit(BinOp* node) override {
        out_ << "{\"type\": \"BinOp\", \"op\": \"" << node->op << "\", \"left\": ";
        if (node->left) node->left->accept(this);
        else out_ << "null";
        out_ << ", \"right\": ";
        if (node->right) node->right->accept(this);
        else out_ << "null";
        out_ << "}";
    }

    void visit(Print* node) override {
        out_ << "{\"type\": \"Print\", \"expr\": ";
        if (node->expression) node->expression->accept(this);
        else out_ << "null";
        out_ << "}";
    }

    void visit(If* node) override {
        out_ << "{\"type\": \"If\", \"condition\": ";
        if (node->condition) node->condition->accept(this);
        else out_ << "null";
        out_ << ", \"body\": [";
        for (size_t i = 0; i < node->body.size(); ++i) {
            if (node->body[i]) node->body[i]->accept(this);
            if (i + 1 < node->body.size()) out_ << ", ";
        }
        out_ << "], \"else_body\": [";
        for (size_t i = 0; i < node->else_body.size(); ++i) {
            if (node->else_body[i]) node->else_body[i]->accept(this);
            if (i + 1 < node->else_body.size()) out_ << ", ";
        }
        out_ << "]}";
    }

    void visit(While* node) override {
        out_ << "{\"type\": \"While\", \"condition\": ";
        if (node->condition) node->condition->accept(this);
        else out_ << "null";
        out_ << "}";
    }

    void visit(ForIn* node) override {
        out_ << "{\"type\": \"ForIn\", \"var\": \"" << node->var_name << "\"}";
    }

    void visit(ListVal* node) override {
        out_ << "{\"type\": \"List\", \"elements\": [";
        for (size_t i = 0; i < node->elements.size(); ++i) {
            if (node->elements[i]) node->elements[i]->accept(this);
            if (i + 1 < node->elements.size()) out_ << ", ";
        }
        out_ << "]}";
    }

    void visit(Dictionary* node) override {
        out_ << "{\"type\": \"Dictionary\", \"pairs_count\": " << node->pairs.size() << "}";
    }

    void visit(Boolean* node) override {
        out_ << "{\"type\": \"Boolean\", \"value\": " << (node->value ? "true" : "false") << "}";
    }

    void visit(FunctionDef* node) override {
        out_ << "{\"type\": \"FunctionDef\", \"name\": \"" << node->name << "\", \"args_count\": " << node->args.size() << "}";
    }

    void visit(AnonymousFunction* /*node*/) override {
        out_ << "{\"type\": \"AnonymousFunction\"}";
    }

    void visit(Call* node) override {
        out_ << "{\"type\": \"Call\", \"callee\": ";
        if (node->callee) node->callee->accept(this);
        else out_ << "null";
        out_ << ", \"args\": [";
        for (size_t i = 0; i < node->args.size(); ++i) {
            if (node->args[i]) node->args[i]->accept(this);
            if (i + 1 < node->args.size()) out_ << ", ";
        }
        out_ << "]}";
    }

    void visit(Return* node) override {
        out_ << "{\"type\": \"Return\", \"value\": ";
        if (node->value) node->value->accept(this);
        else out_ << "null";
        out_ << "}";
    }

    void visit(ClassDef* node) override {
        out_ << "{\"type\": \"ClassDef\", \"name\": \"" << node->name << "\"}";
    }

    void visit(Instantiation* node) override {
        out_ << "{\"type\": \"Instantiation\", \"class\": \"" << node->class_name << "\"}";
    }

    void visit(MethodCall* node) override {
        out_ << "{\"type\": \"MethodCall\", \"method\": \"" << node->method_name << "\"}";
    }

    void visit(PropertyAccess* node) override {
        out_ << "{\"type\": \"PropertyAccess\", \"property\": \"" << node->property_name << "\"}";
    }

    void visit(Import* node) override {
        out_ << "{\"type\": \"Import\", \"path\": \"" << node->path << "\"}";
    }

    void visit(ImportAs* node) override {
        out_ << "{\"type\": \"ImportAs\", \"path\": \"" << node->path << "\", \"alias\": \"" << node->alias << "\"}";
    }

    void visit(Try* /*node*/) override { out_ << "{\"type\": \"Try\"}"; }
    void visit(TryAlways* /*node*/) override { out_ << "{\"type\": \"TryAlways\"}"; }
    void visit(Match* /*node*/) override { out_ << "{\"type\": \"Match\"}"; }
    void visit(ListComprehension* /*node*/) override { out_ << "{\"type\": \"ListComprehension\"}"; }
    void visit(ConstAssign* node) override { out_ << "{\"type\": \"ConstAssign\", \"name\": \"" << node->name << "\"}"; }
    void visit(IndexAccess* /*node*/) override { out_ << "{\"type\": \"IndexAccess\"}"; }
    void visit(IndexAssign* /*node*/) override { out_ << "{\"type\": \"IndexAssign\"}"; }
    void visit(Stop* /*node*/) override { out_ << "{\"type\": \"Stop\"}"; }
    void visit(Skip* /*node*/) override { out_ << "{\"type\": \"Skip\"}"; }
    void visit(Throw* /*node*/) override { out_ << "{\"type\": \"Throw\"}"; }
    void visit(PythonImport* /*node*/) override { out_ << "{\"type\": \"PythonImport\"}"; }
    void visit(FromImport* /*node*/) override { out_ << "{\"type\": \"FromImport\"}"; }
    void visit(For* /*node*/) override { out_ << "{\"type\": \"For\"}"; }
    void visit(Unless* /*node*/) override { out_ << "{\"type\": \"Unless\"}"; }
    void visit(Repeat* /*node*/) override { out_ << "{\"type\": \"Repeat\"}"; }
    void visit(Forever* /*node*/) override { out_ << "{\"type\": \"Forever\"}"; }
    void visit(Until* /*node*/) override { out_ << "{\"type\": \"Until\"}"; }
    void visit(Spawn* /*node*/) override { out_ << "{\"type\": \"Spawn\"}"; }
    void visit(Parallel* /*node*/) override { out_ << "{\"type\": \"Parallel\"}"; }
    void visit(SliceNode* /*node*/) override { out_ << "{\"type\": \"SliceNode\"}"; }
    void visit(DbInsertNode* /*node*/) override { out_ << "{\"type\": \"DbInsertNode\"}"; }
    void visit(DbQueryNode* /*node*/) override { out_ << "{\"type\": \"DbQueryNode\"}"; }
    void visit(DbFindNode* /*node*/) override { out_ << "{\"type\": \"DbFindNode\"}"; }
    void visit(DbDeleteNode* /*node*/) override { out_ << "{\"type\": \"DbDeleteNode\"}"; }
    void visit(WebListenNode* /*node*/) override { out_ << "{\"type\": \"WebListenNode\"}"; }
    void visit(WebRouteNode* /*node*/) override { out_ << "{\"type\": \"WebRouteNode\"}"; }
    void visit(WebServeNode* /*node*/) override { out_ << "{\"type\": \"WebServeNode\"}"; }
    void visit(NlpAddNode* /*node*/) override { out_ << "{\"type\": \"NlpAddNode\"}"; }
    void visit(NlpRemoveNode* /*node*/) override { out_ << "{\"type\": \"NlpRemoveNode\"}"; }
    void visit(NlpTimerNode* /*node*/) override { out_ << "{\"type\": \"NlpTimerNode\"}"; }
    void visit(FileWriteNode* /*node*/) override { out_ << "{\"type\": \"FileWriteNode\"}"; }
    void visit(FileReadNode* /*node*/) override { out_ << "{\"type\": \"FileReadNode\"}"; }
    void visit(NamespaceDecl* /*node*/) override { out_ << "{\"type\": \"NamespaceDecl\"}"; }

private:
    std::ostream &out_;
    int indent_level_ = 0;

    void print_indent() {
        for (int i = 0; i < indent_level_; ++i) out_ << "  ";
    }

    std::string escape_string(const std::string &s) {
        std::string res;
        for (char c : s) {
            if (c == '"') res += "\\\"";
            else if (c == '\\') res += "\\\\";
            else if (c == '\n') res += "\\n";
            else if (c == '\r') res += "\\r";
            else if (c == '\t') res += "\\t";
            else res += c;
        }
        return res;
    }
};

} // namespace shell_lite

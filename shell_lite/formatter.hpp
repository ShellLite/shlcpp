#pragma once

#include "ast_nodes.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <sstream>

namespace shell_lite {

struct Comment {
  int line = 0;
  int col = 0;
  int end_col = 0;
  std::string text;
  bool is_block = false;
};

struct ColBuf : public std::stringbuf {
  int col = 0;
  void reset() {
    str("");
    col = 0;
  }

protected:
  int overflow(int c) override {
    if (c == '\n')
      col = 0;
    else if (c != '\r')
      col++;
    return std::stringbuf::overflow(c);
  }
  std::streamsize xsputn(const char *s, std::streamsize n) override {
    for (std::streamsize i = 0; i < n; i++) {
      if (s[i] == '\n')
        col = 0;
      else if (s[i] != '\r')
        col++;
    }
    return std::stringbuf::xsputn(s, n);
  }
};

struct NoiseMark {
  int line = 0;
  int col = 0;
  std::string word;
};

class CommentCollector {
public:
  // string aware scan dis so # in strings aint comments
  static void collect(std::string_view source, std::vector<Comment> &comments,
                      std::vector<NoiseMark> &noise);
};

struct FormatOptions {
  int indent_width = 4;
  int max_width = 100;
  bool check = false;
  bool diff = false;
  bool write = true;
  bool stdin_mode = false;
  int range_start = 0; // 1 based 0 = off
  int range_end = 0;
};

class Formatter : public Visitor {
public:
  explicit Formatter(const FormatOptions &opts = FormatOptions())
      : opts_(opts), out_(&buf_) {}

  // format the nodes source gotta outlive this for spans
  std::string format(const std::vector<Node *> &nodes, std::string_view source);

  void visit(Number *node) override;
  void visit(String *node) override;
  void visit(VarAccess *node) override;
  void visit(Assign *node) override;
  void visit(TypedAssign *node) override;
  void visit(PropertyAssign *node) override;
  void visit(UnaryOp *node) override;
  void visit(BinOp *node) override;
  void visit(Print *node) override;
  void visit(If *node) override;
  void visit(While *node) override;
  void visit(ForIn *node) override;
  void visit(ListVal *node) override;
  void visit(Dictionary *node) override;
  void visit(Boolean *node) override;
  void visit(FunctionDef *node) override;
  void visit(AnonymousFunction *node) override;
  void visit(Call *node) override;
  void visit(Return *node) override;
  void visit(ClassDef *node) override;
  void visit(Instantiation *node) override;
  void visit(MethodCall *node) override;
  void visit(PropertyAccess *node) override;
  void visit(Import *node) override;
  void visit(ImportAs *node) override;
  void visit(Try *node) override;
  void visit(TryAlways *node) override;
  void visit(Match *node) override;
  void visit(ListComprehension *node) override;
  void visit(ConstAssign *node) override;
  void visit(IndexAccess *node) override;
  void visit(IndexAssign *node) override;
  void visit(Stop *node) override;
  void visit(Pass *node) override;
  void visit(Skip *node) override;
  void visit(Throw *node) override;
  void visit(PythonImport *node) override;
  void visit(FromImport *node) override;
  void visit(For *node) override;
  void visit(Unless *node) override;
  void visit(Repeat *node) override;
  void visit(Forever *node) override;
  void visit(Until *node) override;
  void visit(Spawn *node) override;
  void visit(Parallel *node) override;
  void visit(SliceNode *node) override;
  void visit(DbInsertNode *node) override;
  void visit(DbQueryNode *node) override;
  void visit(DbFindNode *node) override;
  void visit(DbDeleteNode *node) override;
  void visit(WebListenNode *node) override;
  void visit(WebRouteNode *node) override;
  void visit(WebServeNode *node) override;
  void visit(NlpAddNode *node) override;
  void visit(NlpRemoveNode *node) override;
  void visit(NlpTimerNode *node) override;
  void visit(FileWriteNode *node) override;
  void visit(FileReadNode *node) override;
  void visit(NamespaceDecl *node) override;
  void visit(RegexLiteral *node) override;
  void visit(DelStmt *node) override;
  void visit(ParentInitCall *node) override;

private:
  FormatOptions opts_;
  std::string source_;
  ColBuf buf_;
  std::ostream out_;
  int indent_ = 0;
  std::vector<Comment> comments_;
  size_t comment_idx_ = 0;
  std::vector<NoiseMark> noise_;
  std::vector<size_t> line_offsets_;
  int last_line_ = 0;
  bool ends_with_nl_ = false;
  bool flat_mode_ = false;

  void emit_indent();
  void emit_stmt_list(const std::vector<Node *> &nodes);
  void emit_block(const std::vector<Node *> &body, int header_line = 0);
  void emit_expr(Node *node);
  void emit_expr_prec(Node *node, int parent_prec, bool right_side);
  void flush_leading_comments(int upto_line);
  void flush_trailing_comment(const Node *node, int end_line);
  void flush_header_comment(int header_line);
  void emit_verbatim(Node *node, int end_line);
  bool stmt_has_noise(const Node *node) const;
  static int stmt_end(Node *node);
  static int expr_end(Node *node);
  int bracket_end(int from_line, int bound_line) const;
  std::string render_flat(Node *node);
  void emit_paren_args(const std::vector<std::string> &parts);
  void emit_bracketed(const std::vector<std::string> &parts, const char *open,
                      const char *close);
  std::string verbatim_lines(int line, int end_line) const;
  bool in_range(const Node *node, int end_line) const;
  std::string slice_source(int line, int col, int end_line, int end_col) const;
  static int expr_prec(const Node *node);
  static int binop_prec(std::string_view op);
};

std::string escape_string_literal(std::string_view value);
std::string format_number(double value);

void apply_editorconfig(const std::string &filepath, FormatOptions &opts);

}

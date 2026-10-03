#include "formatter.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <variant>

namespace shell_lite {

namespace fs = std::filesystem;

namespace {

bool is_word_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool is_noise_word(std::string_view w) {
  return w == "the" || w == "let" || w == "please";
}

}

void CommentCollector::collect(std::string_view source,
                               std::vector<Comment> &comments,
                               std::vector<NoiseMark> &noise) {
  int line = 1, col = 1;
  size_t i = 0, n = source.size();
  auto adv = [&](size_t k = 1) {
    for (size_t j = 0; j < k; j++) {
      if (i < n && source[i] == '\n') {
        line++;
        col = 1;
      } else {
        col++;
      }
      i++;
    }
  };
  while (i < n) {
    char c = source[i];
    if (c == '\n') {
      adv();
      continue;
    }
    if (c == '"' || c == '\'') {
      char q = c;
      adv();
      while (i < n) {
        char d = source[i];
        if (d == '\\' && i + 1 < n) {
          adv(2);
          continue;
        }
        if (d == q) {
          adv();
          break;
        }
        if (d == '\n') {
          adv();
          break;
        }
        adv();
      }
      continue;
    }
    if (c == '#' ) {
      int sc = col, sl = line;
      size_t start = i;
      while (i < n && source[i] != '\n')
        adv();
      comments.push_back(
          {sl, sc, col, std::string(source.substr(start, i - start)), false});
      continue;
    }
    if (c == '/') {
      bool can_be_regex = true;
      if (i > 0) {
        size_t back = i - 1;
        while (back > 0 && (source[back] == ' ' || source[back] == '\t' ||
                            source[back] == '\n' || source[back] == '\r'))
          back--;
        char prev = source[back];
        if (std::isalnum(static_cast<unsigned char>(prev)) || prev == ')' ||
            prev == ']' || prev == '}' || prev == '"' || prev == '\'')
          can_be_regex = false;
      }
      if (can_be_regex && i + 1 < n && source[i + 1] != '*' &&
          source[i + 1] != '/' && source[i + 1] != ' ' &&
          source[i + 1] != '=') {
        adv();
        while (i < n && source[i] != '/') {
          if (source[i] == '\\' && i + 1 < n)
            adv(2);
          else
            adv();
        }
        if (i < n)
          adv();
        while (i < n && std::isalpha(static_cast<unsigned char>(source[i])))
          adv();
        continue;
      }
    }
    if (c == '/' && i + 1 < n && source[i + 1] == '*') {
      int sc = col, sl = line;
      size_t start = i;
      adv(2);
      while (i < n && !(source[i] == '*' && i + 1 < n && source[i + 1] == '/'))
        adv();
      if (i < n)
        adv(2);
      comments.push_back(
          {sl, sc, col, std::string(source.substr(start, i - start)), true});
      continue;
    }
    if (is_word_char(c)) {
      size_t start = i;
      int sc = col, sl = line;
      while (i < n && is_word_char(source[i]))
        adv();
      std::string_view w = source.substr(start, i - start);
      if (is_noise_word(w))
        noise.push_back({sl, sc, std::string(w)});
      continue;
    }
    adv();
  }
}

std::string escape_string_literal(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (unsigned char c : value) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\a':
      out += "\\a";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\v':
      out += "\\v";
      break;
    default:
      if (c < 0x20 || c == 0x7f) {
        char buf[5];
        std::snprintf(buf, sizeof(buf), "\\x%02x", c);
        out += buf;
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  out.push_back('"');
  return out;
}

std::string format_number(double value) {
  if (std::isnan(value))
    return "0/0";
  if (std::isinf(value))
    return value > 0 ? "1e999" : "-1e999";
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.15g", value);
  if (std::strtod(buf, nullptr) != value)
    std::snprintf(buf, sizeof(buf), "%.17g", value);
  return buf;
}

std::string Formatter::format(const std::vector<Node *> &nodes,
                              std::string_view source) {
  source_ = std::string(source);
  comments_.clear();
  noise_.clear();
  comment_idx_ = 0;
  indent_ = 0;
  buf_.reset();
  line_offsets_.clear();
  line_offsets_.push_back(0);
  for (size_t i = 0; i < source_.size(); i++)
    if (source_[i] == '\n')
      line_offsets_.push_back(i + 1);
  CommentCollector::collect(source_, comments_, noise_);
  last_line_ = 0;
  emit_stmt_list(nodes);
  flush_leading_comments(1 << 30);
  return buf_.str();
}

std::string Formatter::render_flat(Node *node) {
  Formatter sub(opts_);
  sub.flat_mode_ = true;
  sub.emit_expr(node);
  return sub.buf_.str();
}

void Formatter::emit_paren_args(const std::vector<std::string> &parts) {
  emit_bracketed(parts, "(", ")");
}

void Formatter::emit_bracketed(const std::vector<std::string> &parts,
                               const char *open, const char *close) {
  size_t total = 2;
  bool multiline = false;
  for (size_t i = 0; i < parts.size(); i++) {
    if (parts[i].find('\n') != std::string::npos)
      multiline = true;
    total += parts[i].size() + (i + 1 < parts.size() ? 2 : 0);
  }
  if (!flat_mode_ && !multiline && (int)(buf_.col + total) > opts_.max_width &&
      !parts.empty()) {
    out_ << open << "\n";
    indent_++;
    for (auto &p : parts) {
      emit_indent();
      out_ << p << ",\n";
    }
    indent_--;
    emit_indent();
    out_ << close;
    return;
  }
  out_ << open;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i)
      out_ << ", ";
    out_ << parts[i];
  }
  out_ << close;
}

std::string Formatter::verbatim_lines(int line, int end_line) const {
  if (line < 1)
    line = 1;
  if ((size_t)line > line_offsets_.size())
    return "";
  if (end_line < line)
    end_line = line;
  size_t start = line_offsets_[(size_t)(line - 1)];
  size_t end = end_line < (int)line_offsets_.size()
                   ? line_offsets_[(size_t)end_line]
                   : source_.size();
  if (start >= source_.size() || end > source_.size() || end <= start)
    return "";
  return source_.substr(start, end - start);
}

bool Formatter::in_range(const Node *node, int end_line) const {
  if (opts_.range_start <= 0)
    return true;
  int s = node->line;
  int e = end_line >= s ? end_line : s;
  return s >= opts_.range_start && e <= opts_.range_end;
}

void Formatter::emit_indent() {
  for (int i = 0; i < indent_ * opts_.indent_width; i++)
    out_.put(' ');
}

void Formatter::flush_leading_comments(int upto_line) {
  while (comment_idx_ < comments_.size() &&
         comments_[comment_idx_].line < upto_line) {
    const Comment &c = comments_[comment_idx_++];
    emit_indent();
    out_ << c.text << "\n";
    last_line_ = c.line;
  }
}

void Formatter::flush_trailing_comment(const Node *node, int end_line) {
  if (comment_idx_ >= comments_.size())
    return;
  const Comment &c = comments_[comment_idx_];
  // one line /* */ hugs the code like # does tall ones go above
  bool single_line = c.text.find('\n') == std::string::npos;
  if (single_line && c.line == end_line && c.col > node->end_col) {
    out_ << "  " << c.text;
    comment_idx_++;
    ends_with_nl_ = false;
  }
}

void Formatter::flush_header_comment(int header_line) {
  if (header_line <= 0 || comment_idx_ >= comments_.size())
    return;
  const Comment &c = comments_[comment_idx_];
  if (!c.is_block && c.line == header_line) {
    out_ << "  " << c.text;
    comment_idx_++;
  }
}

// parser leaves end_line 0 on some nodes so will walk it ourselves
int Formatter::stmt_end(Node *node) {
  if (!node)
    return 0;
  int e = node->end_line >= node->line ? node->end_line : node->line;
  auto block_end = [&](const std::vector<Node *> &b) {
    for (Node *c : b) {
      int ce = stmt_end(c);
      if (ce > e)
        e = ce;
    }
  };
  if (auto *x = dynamic_cast<If *>(node)) {
    block_end(x->body);
    block_end(x->else_body);
  } else if (auto *x = dynamic_cast<While *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<ForIn *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<For *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<Repeat *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<Forever *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<Until *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<Unless *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<TryAlways *>(node)) {
    block_end(x->try_body);
    block_end(x->catch_body);
    block_end(x->always_body);
  } else if (auto *x = dynamic_cast<Try *>(node)) {
    block_end(x->try_body);
    block_end(x->catch_body);
  } else if (auto *x = dynamic_cast<FunctionDef *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<ClassDef *>(node)) {
    for (FunctionDef *m : x->methods) {
      int me = stmt_end(m);
      if (me > e)
        e = me;
    }
  } else if (auto *x = dynamic_cast<Parallel *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<NamespaceDecl *>(node)) {
    block_end(x->body);
  } else if (auto *x = dynamic_cast<Match *>(node)) {
    for (auto &c : x->cases)
      block_end(c.second);
    block_end(x->default_case);
  } else if (auto *x = dynamic_cast<AnonymousFunction *>(node)) {
    if (std::holds_alternative<std::vector<Node *>>(x->body))
      block_end(std::get<std::vector<Node *>>(x->body));
    else if (Node *s = std::get<Node *>(x->body)) {
      int se = expr_end(s);
      if (se > e)
        e = se;
    }
  } else if (auto *x = dynamic_cast<Assign *>(node)) {
    int ve = expr_end(x->value);
    if (ve > e)
      e = ve;
  } else if (auto *x = dynamic_cast<PropertyAssign *>(node)) {
    int ve = expr_end(x->value);
    if (ve > e)
      e = ve;
  } else if (auto *x = dynamic_cast<IndexAssign *>(node)) {
    int ve = expr_end(x->value);
    if (ve > e)
      e = ve;
  } else if (auto *x = dynamic_cast<ConstAssign *>(node)) {
    int ve = expr_end(x->value);
    if (ve > e)
      e = ve;
  } else if (auto *x = dynamic_cast<Return *>(node)) {
    int ve = expr_end(x->value);
    if (ve > e)
      e = ve;
  }
  return e;
}

// last source line of an expression so multiline rhs values count right
int Formatter::expr_end(Node *node) {
  if (!node)
    return 0;
  int e = node->end_line >= node->line ? node->end_line : node->line;
  auto sub = [&](Node *c) {
    int ce = expr_end(c);
    if (ce > e)
      e = ce;
  };
  if (auto *x = dynamic_cast<BinOp *>(node)) {
    sub(x->left);
    sub(x->right);
  } else if (auto *x = dynamic_cast<UnaryOp *>(node)) {
    sub(x->right);
  } else if (auto *x = dynamic_cast<ListVal *>(node)) {
    for (Node *c : x->elements)
      sub(c);
  } else if (auto *x = dynamic_cast<Dictionary *>(node)) {
    for (auto &kv : x->pairs) {
      sub(kv.first);
      sub(kv.second);
    }
  } else if (auto *x = dynamic_cast<Call *>(node)) {
    sub(x->callee);
    for (Node *a : x->args)
      sub(a);
    for (auto &kw : x->kwargs)
      sub(kw.second);
    for (Node *b : x->body) {
      int be = stmt_end(b);
      if (be > e)
        e = be;
    }
  } else if (auto *x = dynamic_cast<AnonymousFunction *>(node)) {
    int se = stmt_end(x);
    if (se > e)
      e = se;
  } else if (auto *x = dynamic_cast<IndexAccess *>(node)) {
    sub(x->obj);
    sub(x->index);
  } else if (auto *x = dynamic_cast<PropertyAccess *>(node)) {
    sub(x->base);
  } else if (auto *x = dynamic_cast<ListComprehension *>(node)) {
    sub(x->expr);
    sub(x->iterable);
    sub(x->condition);
  } else if (auto *x = dynamic_cast<SliceNode *>(node)) {
    sub(x->array);
    sub(x->start);
    sub(x->stop);
    sub(x->step);
  }
  return e;
}

// last line of a statement by matching brackets in the source
// ast nodes dont cover closers sitting on their own line
int Formatter::bracket_end(int from_line, int bound_line) const {
  if (from_line < 1 || (size_t)from_line > line_offsets_.size())
    return from_line;
  int last = (int)line_offsets_.size() + 1;
  if (bound_line > from_line && bound_line < last)
    last = bound_line;
  size_t i = line_offsets_[(size_t)(from_line - 1)];
  size_t n = source_.size();
  size_t end = (size_t)last <= line_offsets_.size()
                   ? line_offsets_[(size_t)(last - 1)]
                   : n;
  int depth = 0;
  int line = from_line;
  int candidate = 0;
  bool has_code = false;
  bool stop = false;
  auto line_indent = [&](int l) {
    if (l < 1 || (size_t)l > line_offsets_.size())
      return 0;
    size_t s = line_offsets_[(size_t)(l - 1)];
    size_t e = (size_t)l < line_offsets_.size() ? line_offsets_[(size_t)l] : n;
    int w = 0;
    while (s < e && (source_[s] == ' ' || source_[s] == '\t')) {
      w++;
      s++;
    }
    return w;
  };
  int base_indent = line_indent(from_line);
  auto commit = [&]() {
    if (depth == 0 && has_code)
      candidate = line;
    has_code = false;
    // a depth zero line followed by same or lower indent ends the statement
    // continuations are deeper indented or sit inside brackets
    if (depth == 0 && line + 1 < last && line_indent(line + 1) <= base_indent)
      stop = true;
  };
  while (i < end && !stop) {
    char c = source_[i];
    if (c == '\n') {
      commit();
      line++;
      i++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      i++;
      continue;
    }
    if (c == '"' || c == '\'') {
      has_code = true;
      char q = c;
      i++;
      while (i < end) {
        char d = source_[i];
        if (d == '\\' && i + 1 < end) {
          i += 2;
          continue;
        }
        if (d == q) {
          i++;
          break;
        }
        if (d == '\n')
          break;
        i++;
      }
      continue;
    }
    if (c == '#') {
      while (i < end && source_[i] != '\n')
        i++;
      continue;
    }
    if (c == '/' && i + 1 < end && source_[i + 1] == '*') {
      i += 2;
      while (i + 1 < end && !(source_[i] == '*' && source_[i + 1] == '/')) {
        if (source_[i] == '\n') {
          commit();
          line++;
          if (stop)
            break;
        }
        i++;
      }
      if (i + 1 < end)
        i += 2;
      continue;
    }
    has_code = true;
    if (c == '(' || c == '[' || c == '{') {
      depth++;
    } else if ((c == ')' || c == ']' || c == '}') && depth > 0) {
      depth--;
    }
    i++;
  }
  commit();
  return candidate >= from_line ? candidate : from_line;
}

bool Formatter::stmt_has_noise(const Node *node) const {
  int e = stmt_end(const_cast<Node *>(node));
  for (const auto &m : noise_) {
    if (m.line >= node->line && m.line <= e)
      return true;
  }
  return false;
}

std::string Formatter::slice_source(int line, int col, int end_line,
                                    int end_col) const {
  if (line < 1 || end_line < 1 ||
      (size_t)line > line_offsets_.size() ||
      (size_t)end_line > line_offsets_.size())
    return "";
  size_t start = line_offsets_[line - 1] + (col > 0 ? (size_t)(col - 1) : 0);
  size_t end = line_offsets_[end_line - 1] + (end_col > 0 ? (size_t)(end_col - 1) : 0);
  if (start > source_.size() || end > source_.size() || end < start)
    return "";
  return source_.substr(start, end - start);
}

void Formatter::emit_verbatim(Node *node, int end_line) {
  // noise words in here so just keep the source like it was
  std::string text = verbatim_lines(node->line, end_line);
  if (text.empty()) {
    node->accept(this);
    return;
  }
  size_t pos = 0;
  bool first = true;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string ln =
        (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
    if (first) {
      emit_indent();
      size_t s = ln.find_first_not_of(" \t");
      out_ << (s == std::string::npos ? "" : ln.substr(s));
      first = false;
    } else {
      out_ << "\n";
      emit_indent();
      size_t s = ln.find_first_not_of(" \t");
      std::string orig = verbatim_lines(node->line, node->line);
      size_t base = orig.find_first_not_of(" \t");
      if (base == std::string::npos)
        base = 0;
      size_t cur = (s == std::string::npos) ? ln.size() : s;
      for (size_t k = 0; k < (cur > base ? cur - base : 0); k++)
        out_.put(' ');
      out_ << (s == std::string::npos ? "" : ln.substr(s));
    }
    if (nl == std::string::npos)
      break;
    pos = nl + 1;
  }
}

void Formatter::emit_stmt_list(const std::vector<Node *> &nodes) {
  bool first = true;
  for (size_t idx = 0; idx < nodes.size(); idx++) {
    Node *n = nodes[idx];
    if (!n)
      continue;
    bool had_line = n->line > 0;
    if (!had_line) {
      // some stmts got no line info uh so fake one so comment logic dont freak out
      int syn = last_line_ + 1;
      for (size_t k = comment_idx_; k < comments_.size(); k++) {
        if (comments_[k].line == syn)
          syn++;
        else if (comments_[k].line > syn)
          break;
      }
      n->line = syn;
      if (n->end_line < syn)
        n->end_line = syn;
    }
    int end_line = stmt_end(n);
    if (had_line) {
      int bound = (int)line_offsets_.size() + 1;
      if (idx + 1 < nodes.size() && nodes[idx + 1] &&
          nodes[idx + 1]->line > n->line)
        bound = nodes[idx + 1]->line;
      int be = bracket_end(n->line, bound);
      if (be > end_line)
        end_line = be;
    }
    // dis so comments count as content no blank line in the gaps
    int next_line = n->line;
    for (size_t k = comment_idx_;
         k < comments_.size() && comments_[k].line < n->line; k++) {
      if (comments_[k].line < next_line)
        next_line = comments_[k].line;
    }
    if (!first && next_line > last_line_ + 1)
      out_ << "\n";
    flush_leading_comments(n->line);
    ends_with_nl_ = false;
    if (!in_range(n, end_line)) {
      // outta range soooo leave the bytes alone
      std::string raw = verbatim_lines(n->line, end_line);
      out_ << raw;
      ends_with_nl_ = !raw.empty() && raw.back() == '\n';
      while (comment_idx_ < comments_.size() &&
             comments_[comment_idx_].line <= end_line)
        comment_idx_++;
    } else if (stmt_has_noise(n)) {
      emit_verbatim(n, end_line);
      while (comment_idx_ < comments_.size() &&
             comments_[comment_idx_].line <= end_line)
        comment_idx_++;
    } else {
      emit_indent();
      n->accept(this);
    }
    flush_trailing_comment(n, end_line);
    if (ends_with_nl_) {
      ends_with_nl_ = false;
    } else {
      out_ << "\n";
    }
    last_line_ = end_line > last_line_ ? end_line : last_line_;
    first = false;
  }
}

void Formatter::emit_block(const std::vector<Node *> &body, int header_line) {
  flush_header_comment(header_line);
  out_ << "\n";
  indent_++;
  if (body.empty()) {
    // empty block soooo anchor on the header line
    if (header_line > 0)
      last_line_ = header_line;
    // suck in interior comments before the pass lmao
    while (comment_idx_ < comments_.size()) {
      const Comment &c = comments_[comment_idx_];
      if (c.is_block || c.line != last_line_ + 1)
        break;
      if (c.col <= indent_ * opts_.indent_width)
        break;
      emit_indent();
      out_ << c.text << "\n";
      last_line_ = c.line;
      comment_idx_++;
    }
    emit_indent();
    out_ << "pass\n";
    last_line_++;
  } else {
    emit_stmt_list(body);
    // dis so trailing comments at block indent stay in the block
    while (comment_idx_ < comments_.size()) {
      const Comment &c = comments_[comment_idx_];
      if (c.is_block || c.line != last_line_ + 1)
        break;
      if (c.col <= indent_ * opts_.indent_width)
        break;
      // dis so comments which are with the code stay with their line
      std::string prefix = slice_source(c.line, 1, c.line, c.col);
      bool standalone = true;
      for (char ch : prefix)
        if (ch != ' ' && ch != '\t') {
          standalone = false;
          break;
        }
      if (!standalone)
        break;
      emit_indent();
      out_ << c.text << "\n";
      last_line_ = c.line;
      comment_idx_++;
    }
  }
  indent_--;
  ends_with_nl_ = true;
}
int Formatter::binop_prec(std::string_view op) {
  if (op == "or")
    return 10;
  if (op == "and")
    return 20;
  if (op == "|")
    return 22;
  if (op == "^")
    return 24;
  if (op == "&")
    return 26;
  if (op == "==" || op == "!=")
    return 30;
  if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "in" ||
      op == "not in" || op == "is")
    return 40;
  if (op == "<<" || op == ">>")
    return 45;
  if (op == "+" || op == "-")
    return 50;
  if (op == "*" || op == "/" || op == "%" || op == "//")
    return 60;
  if (op == "**")
    return 70;
  return 0;
}

int Formatter::expr_prec(const Node *node) {
  if (const BinOp *b = dynamic_cast<const BinOp *>(node))
    return binop_prec(b->op);
  if (dynamic_cast<const UnaryOp *>(node))
    return 90;
  return 100;
}

void Formatter::emit_expr(Node *node) {
  if (node)
    node->accept(this);
}

void Formatter::emit_expr_prec(Node *node, int parent_prec, bool right_side) {
  if (!node)
    return;
  int p = expr_prec(node);
  bool need = false;
  if (const BinOp *b = dynamic_cast<const BinOp *>(node)) {
    bool right_assoc = (b->op == "**");
    if (right_side)
      need = right_assoc ? (p < parent_prec) : (p <= parent_prec);
    else
      need = right_assoc ? (p <= parent_prec) : (p < parent_prec);
  } else {
    need = p < parent_prec;
  }
  if (need)
    out_ << "(";
  node->accept(this);
  if (need)
    out_ << ")";
}

void Formatter::visit(Number *node) {
  // keep the raw lexeme so 0xFF aint rewritten as 255
  if (!node->lexeme.empty())
    out_ << node->lexeme;
  else
    out_ << format_number(node->value);
}

void Formatter::visit(String *node) { out_ << escape_string_literal(node->value); }

void Formatter::visit(Boolean *node) { out_ << (node->value ? "true" : "false"); }

void Formatter::visit(VarAccess *node) { out_ << node->name; }

void Formatter::visit(RegexLiteral *node) {
  out_ << "/" << node->pattern << "/" << node->flags;
}

void Formatter::visit(BinOp *node) {
  int p = binop_prec(node->op);
  emit_expr_prec(node->left, p, false);
  out_ << " " << node->op << " ";
  emit_expr_prec(node->right, p, true);
}

void Formatter::visit(UnaryOp *node) {
  out_ << node->op;
  if (node->op == "not")
    out_ << " ";
  Node *o = node->right;
  int p = expr_prec(o);
  if (p < 90) {
    out_ << "(";
    emit_expr(o);
    out_ << ")";
  } else {
    emit_expr(o);
  }
}

void Formatter::visit(Call *node) {
  if (node->callee)
    emit_expr(node->callee);
  else
    out_ << node->name;
  std::vector<std::string> parts;
  for (Node *a : node->args)
    parts.push_back(render_flat(a));
  for (auto &kv : node->kwargs)
    parts.push_back(std::string(kv.first) + " = " + render_flat(kv.second));
  emit_paren_args(parts);
}

void Formatter::visit(MethodCall *node) {
  out_ << node->instance_name << "." << node->method_name;
  std::vector<std::string> parts;
  for (Node *a : node->args)
    parts.push_back(render_flat(a));
  emit_paren_args(parts);
}

void Formatter::visit(PropertyAccess *node) {
  if (node->base)
    emit_expr(node->base);
  else
    out_ << node->instance_name;
  out_ << "." << node->property_name;
}

void Formatter::visit(IndexAccess *node) {
  emit_expr(node->obj);
  out_ << "[";
  emit_expr(node->index);
  out_ << "]";
}

void Formatter::visit(ListVal *node) {
  std::vector<std::string> parts;
  for (Node *e : node->elements)
    parts.push_back(render_flat(e));
  emit_bracketed(parts, "[", "]");
}

void Formatter::visit(Dictionary *node) {
  std::vector<std::string> parts;
  for (auto &kv : node->pairs)
    parts.push_back(render_flat(kv.first) + ": " + render_flat(kv.second));
  emit_bracketed(parts, "{", "}");
}

void Formatter::visit(ListComprehension *node) {
  out_ << "[";
  emit_expr(node->expr);
  out_ << " for " << node->var_name << " in ";
  emit_expr(node->iterable);
  if (node->condition) {
    out_ << " if ";
    emit_expr(node->condition);
  }
  out_ << "]";
}

void Formatter::visit(SliceNode *node) {
  emit_expr(node->array);
  out_ << "[";
  if (node->start)
    emit_expr(node->start);
  out_ << ":";
  if (node->stop)
    emit_expr(node->stop);
  if (node->step) {
    out_ << ":";
    emit_expr(node->step);
  }
  out_ << "]";
}

void Formatter::visit(Instantiation *node) {
  out_ << node->class_name << "(";
  bool first = true;
  for (Node *a : node->args) {
    if (!first)
      out_ << ", ";
    emit_expr(a);
    first = false;
  }
  for (auto &kv : node->kwargs) {
    if (!first)
      out_ << ", ";
    out_ << kv.first << " = ";
    emit_expr(kv.second);
    first = false;
  }
  out_ << ")";
}

void Formatter::visit(AnonymousFunction *node) {
  out_ << "lambda";
  bool first = true;
  for (auto a : node->args) {
    out_ << (first ? " " : ", ");
    out_ << a;
    first = false;
  }
  if (std::holds_alternative<Node *>(node->body)) {
    out_ << " => ";
    if (Node *single = std::get<Node *>(node->body))
      emit_expr(single);
  } else {
    out_ << " do";
    emit_block(std::get<std::vector<Node *>>(node->body), node->line);
  }
}

void Formatter::visit(Assign *node) {
  out_ << node->name << " = ";
  emit_expr(node->value);
}

void Formatter::visit(TypedAssign *node) {
  out_ << node->name << " as " << node->type_hint << " = ";
  emit_expr(node->value);
}

void Formatter::visit(ConstAssign *node) {
  out_ << "const " << node->name << " = ";
  emit_expr(node->value);
}

void Formatter::visit(PropertyAssign *node) {
  out_ << node->instance_name << "." << node->property_name << " = ";
  emit_expr(node->value);
}

void Formatter::visit(IndexAssign *node) {
  emit_expr(node->obj);
  out_ << "[";
  emit_expr(node->index);
  out_ << "] = ";
  emit_expr(node->value);
}

void Formatter::visit(Print *node) {
  out_ << (node->to_stderr ? "esay" : "say");
  if (node->style && !node->style->empty())
    out_ << " " << *node->style;
  if (node->color && !node->color->empty())
    out_ << " " << *node->color;
  out_ << " ";
  emit_expr(node->expression);
}

void Formatter::visit(Return *node) {
  out_ << "give";
  if (node->value) {
    out_ << " ";
    emit_expr(node->value);
  }
}

void Formatter::visit(Stop *node) { out_ << "stop"; }

void Formatter::visit(Skip *node) { out_ << "skip"; }

void Formatter::visit(Pass *node) { out_ << "pass"; }

void Formatter::visit(Throw *node) {
  out_ << "throw ";
  emit_expr(node->message);
}

void Formatter::visit(DelStmt *node) {
  out_ << "del ";
  emit_expr(node->obj);
  if (node->key) {
    out_ << "[";
    emit_expr(node->key);
    out_ << "]";
  }
}

void Formatter::visit(ParentInitCall *node) {
  out_ << "super(";
  bool first = true;
  for (Node *a : node->args) {
    if (!first)
      out_ << ", ";
    emit_expr(a);
    first = false;
  }
  out_ << ")";
}

void Formatter::visit(Spawn *node) {
  out_ << "spawn ";
  emit_expr(node->call);
}

void Formatter::visit(Import *node) {
  out_ << "use \"" << node->path << "\"";
}

void Formatter::visit(ImportAs *node) {
  out_ << "use \"" << node->path << "\" as " << node->alias;
}

void Formatter::visit(PythonImport *node) {
  out_ << "use python " << node->module_name;
  if (node->alias)
    out_ << " as " << *node->alias;
}

void Formatter::visit(FromImport *node) {
  out_ << "from " << node->module_name << " import ";
  bool first = true;
  for (auto &nm : node->names) {
    if (!first)
      out_ << ", ";
    out_ << nm.first;
    if (nm.second)
      out_ << " as " << *nm.second;
    first = false;
  }
}

void Formatter::visit(If *node) {
  out_ << "if ";
  emit_expr(node->condition);
  emit_block(node->body, node->line);
  const If *elif_node = nullptr;
  if (node->else_body.size() == 1)
    elif_node = dynamic_cast<const If *>(node->else_body[0]);
  if (elif_node) {
    emit_indent();
    out_ << "elif ";
    emit_expr(elif_node->condition);
    emit_block(elif_node->body, elif_node->line);
    const If *cur = elif_node;
    while (true) {
      const If *next = nullptr;
      if (cur->else_body.size() == 1)
        next = dynamic_cast<const If *>(cur->else_body[0]);
      if (!next)
        break;
      emit_indent();
      out_ << "elif ";
      emit_expr(next->condition);
      emit_block(next->body, next->line);
      cur = next;
    }
    if (!cur->else_body.empty()) {
      emit_indent();
      out_ << "else";
      emit_block(cur->else_body, 0);
    }
    return;
  }
  if (!node->else_body.empty()) {
    emit_indent();
    out_ << "else";
    emit_block(node->else_body, 0);
  }
}

void Formatter::visit(While *node) {
  out_ << "while ";
  emit_expr(node->condition);
  emit_block(node->body, node->line);
}

void Formatter::visit(Unless *node) {
  out_ << "unless ";
  emit_expr(node->condition);
  emit_block(node->body, node->line);
}

void Formatter::visit(ForIn *node) {
  out_ << "for " << node->var_name << " in ";
  emit_expr(node->iterable);
  emit_block(node->body, node->line);
}

void Formatter::visit(For *node) {
  out_ << "for ";
  emit_expr(node->count);
  emit_block(node->body, node->line);
}

void Formatter::visit(Repeat *node) {
  out_ << "repeat ";
  emit_expr(node->count);
  out_ << " times";
  emit_block(node->body, node->line);
}

void Formatter::visit(Forever *node) {
  out_ << "forever";
  emit_block(node->body, node->line);
}

void Formatter::visit(Until *node) {
  out_ << "until ";
  emit_expr(node->condition);
  emit_block(node->body, node->line);
}

void Formatter::visit(Try *node) {
  out_ << "try";
  emit_block(node->try_body, node->line);
  if (!node->catch_body.empty()) {
    emit_indent();
    out_ << "catch";
    if (!node->catch_var.empty())
      out_ << " " << node->catch_var;
    emit_block(node->catch_body, 0);
  }
}

void Formatter::visit(TryAlways *node) {
  out_ << "try";
  emit_block(node->try_body, node->line);
  if (!node->catch_body.empty()) {
    emit_indent();
    out_ << "catch";
    if (!node->catch_var.empty())
      out_ << " " << node->catch_var;
    emit_block(node->catch_body, 0);
  }
  if (!node->always_body.empty()) {
    emit_indent();
    out_ << "always";
    emit_block(node->always_body, 0);
  }
}

void Formatter::visit(Parallel *node) {
  out_ << "parallel";
  emit_block(node->body, node->line);
}

void Formatter::visit(NamespaceDecl *node) {
  out_ << "namespace " << node->name;
  emit_block(node->body, node->line);
}

void Formatter::visit(FunctionDef *node) {
  out_ << "to " << node->name << "(";
  bool first = true;
  for (auto &a : node->args) {
    if (!first)
      out_ << ", ";
    out_ << std::get<0>(a);
    if (std::get<2>(a))
      out_ << " as " << *std::get<2>(a);
    if (std::get<1>(a)) {
      out_ << " = ";
      emit_expr(std::get<1>(a));
    }
    first = false;
  }
  out_ << ")";
  emit_block(node->body, node->line);
}

void Formatter::visit(ClassDef *node) {
  out_ << "class " << node->name;
  if (node->parent)
    out_ << " extends " << *node->parent;
  flush_header_comment(node->line);
  out_ << "\n";
  indent_++;
  bool first = true;
  for (auto &p : node->properties) {
    emit_indent();
    out_ << "has " << p.first;
    if (p.second) {
      out_ << " = ";
      emit_expr(p.second);
    }
    out_ << "\n";
    first = false;
  }
  for (FunctionDef *f : node->methods) {
    if (!f)
      continue;
    // parser fakes an init with no line uh so skip that
    if (f->name == "init" && f->line == 0)
      continue;
    if (!first)
      out_ << "\n";
    emit_indent();
    visit(f);
    first = false;
  }
  if (first) {
    if (node->line > 0)
      last_line_ = node->line;
    while (comment_idx_ < comments_.size()) {
      const Comment &c = comments_[comment_idx_];
      if (c.is_block || c.line != last_line_ + 1)
        break;
      if (c.col <= indent_ * opts_.indent_width)
        break;
      emit_indent();
      out_ << c.text << "\n";
      last_line_ = c.line;
      comment_idx_++;
    }
    emit_indent();
    out_ << "pass\n";
    last_line_++;
  }
  indent_--;
  ends_with_nl_ = true;
}

void Formatter::visit(NlpAddNode *node) {
  out_ << "add ";
  emit_expr(node->item);
  out_ << " to ";
  emit_expr(node->container);
}

void Formatter::visit(NlpRemoveNode *node) {
  out_ << "remove ";
  emit_expr(node->item);
  out_ << " from ";
  emit_expr(node->container);
}

void Formatter::visit(NlpTimerNode *node) {
  out_ << (node->is_every ? "every" : "after") << " ";
  emit_expr(node->interval);
  out_ << " " << node->unit;
  if (node->body) {
    if (AnonymousFunction *af = dynamic_cast<AnonymousFunction *>(node->body)) {
      if (std::holds_alternative<std::vector<Node *>>(af->body)) {
        emit_block(std::get<std::vector<Node *>>(af->body), node->line);
      } else {
        Node *single = std::get<Node *>(af->body);
        std::vector<Node *> one;
        if (single)
          one.push_back(single);
        emit_block(one, node->line);
      }
    } else {
      std::vector<Node *> single{node->body};
      emit_block(single, node->line);
    }
  } else {
    emit_block({}, node->line);
  }
}

void Formatter::visit(FileWriteNode *node) {
  out_ << (node->is_append ? "append" : "write") << " ";
  emit_expr(node->data);
  out_ << " to ";
  emit_expr(node->path);
}

void Formatter::visit(FileReadNode *node) {
  out_ << "read ";
  emit_expr(node->path);
}

void Formatter::visit(DbInsertNode *node) {
  out_ << "db insert ";
  emit_expr(node->data);
  out_ << " into ";
  emit_expr(node->table);
}

void Formatter::visit(DbQueryNode *node) {
  out_ << "db query ";
  emit_expr(node->query);
}

void Formatter::visit(DbFindNode *node) {
  out_ << "db find ";
  if (node->conditions)
    emit_expr(node->conditions);
  else
    out_ << "all";
  out_ << " from ";
  emit_expr(node->table);
}

void Formatter::visit(DbDeleteNode *node) {
  out_ << "db delete from ";
  emit_expr(node->table);
  if (node->conditions) {
    out_ << " where ";
    emit_expr(node->conditions);
  }
}

void Formatter::visit(WebListenNode *node) {
  out_ << "listen ";
  emit_expr(node->port);
}

void Formatter::visit(WebRouteNode *node) {
  out_ << "route " << node->method << " ";
  emit_expr(node->path);
  out_ << " ";
  emit_expr(node->handler);
}

void Formatter::visit(WebServeNode *node) {
  out_ << "serve ";
  emit_expr(node->dir);
  out_ << " at ";
  emit_expr(node->route);
}

void Formatter::visit(Match *node) {
  out_ << "when ";
  emit_expr(node->match_expr);
  out_ << "\n";
  indent_++;
  for (auto &c : node->cases) {
    emit_indent();
    out_ << "is ";
    emit_expr(c.first);
    emit_block(c.second, c.first ? c.first->line : 0);
  }
  if (!node->default_case.empty()) {
    emit_indent();
    out_ << "otherwise";
    emit_block(node->default_case, 0);
  }
  indent_--;
}

namespace {

bool glob_match(std::string_view pat, std::string_view s) {
  size_t p = 0, i = 0, star = std::string_view::npos, mark = 0;
  while (i < s.size()) {
    if (p < pat.size() && (pat[p] == '?' || pat[p] == s[i])) {
      p++;
      i++;
    } else if (p < pat.size() && pat[p] == '*') {
      star = p++;
      mark = i;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      i = ++mark;
    } else {
      return false;
    }
  }
  while (p < pat.size() && pat[p] == '*')
    p++;
  return p == pat.size();
}

std::string trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    b--;
  return std::string(s.substr(a, b - a));
}

}

void apply_editorconfig(const std::string &filepath, FormatOptions &opts) {
  fs::path p = fs::absolute(fs::path(filepath));
  fs::path dir = p.parent_path();
  std::string filename = p.filename().string();
  bool have_indent = false;
  bool have_max = false;
  for (int depth = 0; depth < 32 && !dir.empty(); depth++) {
    fs::path cfg = dir / ".editorconfig";
    std::error_code ec;
    bool is_root = false;
    if (fs::exists(cfg, ec)) {
      std::string rel = filename;
      fs::path relp = fs::relative(p, dir, ec);
      if (!ec) {
        rel = relp.string();
        for (auto &c : rel)
          if (c == '\\')
            c = '/';
      }
      std::ifstream in(cfg);
      std::string line, section;
      bool matched = false;
      bool preamble = true;
      int indent_size = 0, max_len = 0;
      while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';')
          continue;
        if (t.front() == '[' && t.back() == ']') {
          preamble = false;
          section = trim(t.substr(1, t.size() - 2));
          matched =
              glob_match(section, rel) || glob_match(section, filename);
          continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos)
          eq = t.find(':');
        if (eq == std::string::npos)
          continue;
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));
        for (auto &c : key)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (preamble && key == "root") {
          for (auto &c : val)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          if (trim(val) == "true")
            is_root = true;
          continue;
        }
        if (!matched)
          continue;
        if (key == "indent_size" && val != "tab") {
          try {
            indent_size = std::stoi(val);
          } catch (...) {
          }
        } else if (key == "max_line_length" && val != "off") {
          try {
            max_len = std::stoi(val);
          } catch (...) {
          }
        }
      }
      if (!have_indent && indent_size > 0) {
        opts.indent_width = indent_size;
        have_indent = true;
      }
      if (!have_max && max_len > 0) {
        opts.max_width = max_len;
        have_max = true;
      }
      if (is_root)
        break;
    }
    if (dir == dir.root_path())
      break;
    dir = dir.parent_path();
  }
}

} // namespace shell_lite

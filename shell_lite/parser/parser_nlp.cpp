#include "parser/parser_internal.hpp"

namespace shell_lite {


Node *Parser::bind_nlp_add(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  size_t to_idx = 0;
  for (size_t k = 1; k < tokens.size(); ++k) {
    if (tokens[k].type == TokenType::TOK_TO || tokens[k].type == TokenType::TOK_INTO) {
      to_idx = k;
      break;
    }
  }
  if (to_idx > 1 && to_idx + 1 < tokens.size()) {
    std::vector<Token> item_toks(tokens.begin() + 1, tokens.begin() + to_idx);
    std::vector<Token> cont_toks(tokens.begin() + to_idx + 1, tokens.end());
    Node *item_node = parse_expr_recursive(item_toks);
    Node *cont_node = parse_expr_recursive(cont_toks);
    NlpAddNode *node = arena_.emplace<NlpAddNode>(item_node, cont_node);
    set_node_loc(node, index);
    return node;
  }
  return bind_expression_statement(index);
}

Node *Parser::bind_nlp_remove(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  size_t from_idx = 0;
  for (size_t k = 1; k < tokens.size(); ++k) {
    if (tokens[k].type == TokenType::TOK_FROM) {
      from_idx = k;
      break;
    }
  }
  if (from_idx > 1 && from_idx + 1 < tokens.size()) {
    std::vector<Token> item_toks(tokens.begin() + 1, tokens.begin() + from_idx);
    std::vector<Token> cont_toks(tokens.begin() + from_idx + 1, tokens.end());
    Node *item_node = parse_expr_recursive(item_toks);
    Node *cont_node = parse_expr_recursive(cont_toks);
    NlpRemoveNode *node = arena_.emplace<NlpRemoveNode>(item_node, cont_node);
    set_node_loc(node, index);
    return node;
  }
  return bind_expression_statement(index);
}

Node *Parser::bind_nlp_timer(int index, bool is_every) {
  const auto &tokens = flat_nodes_[index].tokens;
  std::string unit = "seconds";
  std::vector<Token> interval_toks;
  for (size_t k = 1; k < tokens.size(); ++k) {
    if (tokens[k].type == TokenType::TOK_MINUTE) {
      unit = "minutes";
      continue;
    }
    if (tokens[k].type == TokenType::TOK_SECOND) {
      unit = "seconds";
      continue;
    }
    if (tokens[k].type == TokenType::TOK_DO || tokens[k].type == TokenType::TOK_COLON) {
      continue;
    }
    interval_toks.push_back(tokens[k]);
  }
  Node *interval_node = interval_toks.empty() ? arena_.emplace<Number>(1.0) : parse_expr_recursive(interval_toks);
  AnonymousFunction *body_fn = arena_.emplace<AnonymousFunction>();
  body_fn->body = bind_statement_list(flat_nodes_[index].child_indices);
  NlpTimerNode *node = arena_.emplace<NlpTimerNode>(interval_node, unit, body_fn, is_every);
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_file_write(int index, bool is_append) {
  const auto &tokens = flat_nodes_[index].tokens;
  size_t to_idx = 0;
  for (size_t k = 1; k < tokens.size(); ++k) {
    if (tokens[k].type == TokenType::TOK_TO || tokens[k].type == TokenType::TOK_INTO) {
      to_idx = k;
      break;
    }
  }
  if (to_idx > 1 && to_idx + 1 < tokens.size()) {
    std::vector<Token> data_toks(tokens.begin() + 1, tokens.begin() + to_idx);
    std::vector<Token> path_toks(tokens.begin() + to_idx + 1, tokens.end());
    Node *data_node = parse_expr_recursive(data_toks);
    Node *path_node = parse_expr_recursive(path_toks);
    FileWriteNode *node = arena_.emplace<FileWriteNode>(path_node, data_node, is_append);
    set_node_loc(node, index);
    return node;
  }
  return bind_expression_statement(index);
}


Node *Parser::bind_print(int index) {
  auto tokens = flat_nodes_[index].tokens;
  SubParser parser(tokens, arena_, *this);
  parser.advance();
  std::optional<std::string_view> style;
  std::optional<std::string_view> color;

  auto is_color = [](std::string_view s) {
    return s == "red" || s == "green" || s == "blue" || s == "yellow" ||
           s == "cyan" || s == "magenta" || s == "white" || s == "black";
  };
  auto is_style = [](std::string_view s) {
    return s == "bold" || s == "italic" || s == "underline";
  };

  while (!parser.is_at_end()) {
    if (parser.check(TokenType::TOK_IN)) {
      parser.advance();
      continue;
    }
    if (parser.check(TokenType::TOK_ID)) {
      std::string_view val = parser.peek().value;
      if (is_style(val)) {
        style = val;
        parser.advance();
        continue;
      }
      if (is_color(val)) {
        color = val;
        parser.advance();
        continue;
      }
    }
    break;
  }

  Node *expr = parser.parse_expression();
  if (!expr)
    throw SyntaxError(
        "Syntax error: Missing expression for 'print'/'say' at line " +
        std::to_string(flat_nodes_[index].line));
  Print *node = arena_.emplace<Print>();
  node->expression = expr;
  node->style = style;
  node->color = color;
  set_node_loc(node, index);
  return node;
}


Node *Parser::bind_natural_list(int index) {
  return parse_expr_recursive(flat_nodes_[index].tokens,
                              flat_nodes_[index].child_indices);
}

Node *Parser::bind_natural_set(int index) {
  Node *list = parse_expr_recursive(flat_nodes_[index].tokens,
                                    flat_nodes_[index].child_indices);
  Call *call = arena_.emplace<Call>("set");
  call->args = {list};
  return call;
}


} // namespace shell_lite

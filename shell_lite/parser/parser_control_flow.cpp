#include "parser/parser_internal.hpp"

namespace shell_lite {


Node *Parser::bind_if(int index) {
  auto tokens = flat_nodes_[index].tokens;
  SubParser parser(tokens, arena_, *this);
  parser.advance();
  Node *cond = parser.parse_expression();
  if (!cond)
    throw SyntaxError(
        "Syntax error: Missing condition for 'if' at line " +
        std::to_string(flat_nodes_[index].line));
  auto body = bind_statement_list(flat_nodes_[index].child_indices);
  If *node = arena_.emplace<If>();
  node->condition = cond;
  node->body = body;
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_unless(int index) {
  auto tokens = flat_nodes_[index].tokens;
  SubParser parser(tokens, arena_, *this);
  parser.advance();
  Node *cond = parser.parse_expression();
  auto body = bind_statement_list(flat_nodes_[index].child_indices);
  If *node = arena_.emplace<If>();
  node->condition = arena_.emplace<UnaryOp>("not", cond);
  node->body = body;
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_until(int index) {
  auto tokens = flat_nodes_[index].tokens;
  SubParser parser(tokens, arena_, *this);
  parser.advance();
  Node *cond = parser.parse_expression();
  auto body = bind_statement_list(flat_nodes_[index].child_indices);
  While *node = arena_.emplace<While>();
  node->condition = arena_.emplace<UnaryOp>("not", cond);
  node->body = body;
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_while(int index) {
  auto tokens = flat_nodes_[index].tokens;
  SubParser parser(tokens, arena_, *this);
  parser.advance();
  Node *cond = parser.parse_expression();
  if (!cond)
    throw SyntaxError(
        "Syntax error: Missing condition for 'while' at line " +
        std::to_string(flat_nodes_[index].line));
  auto body = bind_statement_list(flat_nodes_[index].child_indices);
  While *node = arena_.emplace<While>();
  node->condition = cond;
  node->body = body;
  set_node_loc(node, index);
  return node;
}


Node *Parser::bind_repeat(int index) {
  auto tokens = flat_nodes_[index].tokens;
  std::vector<Token> expr_tokens;
  for (size_t k = 1; k < tokens.size(); ++k) {
    if (tokens[k].type == TokenType::TOK_TIMES || tokens[k].value == "times" ||
        tokens[k].type == TokenType::TOK_COLON)
      continue;
    expr_tokens.push_back(tokens[k]);
  }
  Node *count = parse_expr_recursive(expr_tokens);
  auto body = bind_statement_list(flat_nodes_[index].child_indices);
  Repeat *node = arena_.emplace<Repeat>();
  node->count = count;
  node->body = body;
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_forever(int index) {
  auto body = bind_statement_list(flat_nodes_[index].child_indices);
  Forever *node = arena_.emplace<Forever>();
  node->body = body;
  set_node_loc(node, index);
  return node;
}


Node *Parser::bind_for(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  std::string_view var = "i";
  std::vector<Token> iter_tokens;
  std::optional<size_t> in_idx;
  for (size_t k = 0; k < tokens.size(); ++k) {
    if (tokens[k].type == TokenType::TOK_IN) {
      in_idx = k;
      break;
    }
  }

  if (in_idx.has_value()) {
    if (in_idx.value() >= 3 && tokens[1].type == TokenType::TOK_EACH) {
      var = tokens[2].value;
    } else if (in_idx.value() >= 2) {
      var = tokens[1].value;
    }
    if (in_idx.value() + 1 < tokens.size()) {
      iter_tokens = std::vector<Token>(tokens.begin() + in_idx.value() + 1, tokens.end());
    }
  }

  if (iter_tokens.empty()) {
    std::vector<Token> count_tokens;
    for (size_t k = 1; k < tokens.size(); ++k) {
      if (tokens[k].type == TokenType::TOK_TIMES)
        continue;
      count_tokens.push_back(tokens[k]);
    }
    if (!count_tokens.empty()) {
      For *fnode = arena_.emplace<For>();
      fnode->count = parse_expr_recursive(count_tokens);
      fnode->body = bind_statement_list(flat_nodes_[index].child_indices);
      set_node_loc(fnode, index);
      return fnode;
    }
    throw SyntaxError(
        "Syntax error: Missing iterable in 'for' loop at line " +
        std::to_string(flat_nodes_[index].line));
  }

  ForIn *node = arena_.emplace<ForIn>();
  node->var_name = var;
  node->iterable = parse_expr_recursive(iter_tokens);
  node->body = bind_statement_list(flat_nodes_[index].child_indices);
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_try(int index) {
  Try *node = arena_.emplace<Try>();
  node->try_body = bind_statement_list(flat_nodes_[index].child_indices);
  set_node_loc(node, index);
  return node;
}


Node *Parser::bind_throw(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  Throw *node = arena_.emplace<Throw>();
  if (tokens.size() > 1) {
    node->message = parse_expr_recursive(
        std::vector<Token>(tokens.begin() + 1, tokens.end()));
  } else {
    node->message = arena_.emplace<String>("Error");
  }
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_assertion(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  size_t to_idx = static_cast<size_t>(-1);
  bool is_not = false;
  for (size_t k = 0; k < tokens.size(); k++) {
    if (to_idx == static_cast<size_t>(-1) &&
        (tokens[k].type == TokenType::TOK_IS ||
         tokens[k].type == TokenType::TOK_TO ||
         tokens[k].type == TokenType::TOK_BE))
      to_idx = k;
    if (tokens[k].type == TokenType::TOK_NOT)
      is_not = true;
  }

  Node *condition;
  if (to_idx == static_cast<size_t>(-1)) {
    condition = parse_expr_recursive(extract_expr_tokens(tokens, 1));
  } else {
    Node *left = parse_expr_recursive(
        std::vector<Token>(tokens.begin() + 1, tokens.begin() + to_idx));
    std::vector<Token> right_tokens;
    for (size_t k = to_idx + 1; k < tokens.size(); k++)
      if (tokens[k].type != TokenType::TOK_NOT &&
          tokens[k].type != TokenType::TOK_BE)
        right_tokens.push_back(tokens[k]);
    Node *right = parse_expr_recursive(right_tokens);
    condition = arena_.emplace<BinOp>(left, is_not ? "!=" : "==", right);
  }

  Call *call = arena_.emplace<Call>("assert");
  call->args.push_back(condition);
  set_node_loc(call, index);
  return call;
}


Node *Parser::bind_test_block(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  Call *call = arena_.emplace<Call>("test_block");
  call->args.push_back(arena_.emplace<String>(
      (tokens.size() > 1) ? tokens[1].value : "unnamed test"));
  AnonymousFunction *block = arena_.emplace<AnonymousFunction>();
  block->body = bind_statement_list(flat_nodes_[index].child_indices);
  call->args.push_back(block);
  set_node_loc(call, index);
  return call;
}


Node *Parser::bind_match(int index) {

  const auto &tokens = flat_nodes_[index].tokens;

  Match *node = arena_.emplace<Match>();
  auto expr_tokens = extract_expr_tokens(tokens, 1);
  node->match_expr = parse_expr_recursive(expr_tokens);
  for (int child_idx : flat_nodes_[index].child_indices) {
    Token head = get_effective_head(child_idx);
    if (head.type == TokenType::TOK_IS) {
      node->cases.push_back(
          {parse_expr_recursive(
               extract_expr_tokens(flat_nodes_[child_idx].tokens, 1)),
           bind_statement_list(flat_nodes_[child_idx].child_indices)});
    } else if (head.type == TokenType::TOK_OTHERWISE)
      node->default_case =
          bind_statement_list(flat_nodes_[child_idx].child_indices);
  }
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_when_clause(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  // Detect routing pattern: when someone visits/submits
  bool is_routing = false;
  for (const auto &t : tokens) {
    if (t.type == TokenType::TOK_VISITS || t.type == TokenType::TOK_SUBMITS ||
        t.type == TokenType::TOK_SOMEONE) {
      is_routing = true;
      break;
    }
  }
  if (is_routing) {
    std::string method = "get";
    std::string path = "/";
    for (const auto &t : tokens) {
      if (t.type == TokenType::TOK_SUBMITS)
        method = "post";
      if (t.type == TokenType::TOK_STRING)
        path = t.value;
    }
    Call *call = arena_.emplace<Call>(method == "get" ? "when_someone_visits"
                                                      : "when_someone_submits");
    call->args.push_back(arena_.emplace<String>(path));
    AnonymousFunction *block = arena_.emplace<AnonymousFunction>();
    block->body = bind_statement_list(flat_nodes_[index].child_indices);
    call->args.push_back(block);
    set_node_loc(call, index);
    return call;
  }
  // Fallback: treat as match statement
  return bind_match(index);
}

Node *Parser::bind_exit(int index) {
  Call *n = arena_.emplace<Call>("os_exit");
  n->args.push_back(
      parse_expr_recursive(extract_expr_tokens(flat_nodes_[index].tokens, 1)));
  set_node_loc(n, index);
  return n;
}
Node *Parser::bind_stop(int index) {
  Stop *n = arena_.emplace<Stop>();
  set_node_loc(n, index);
  return n;
}
Node *Parser::bind_skip(int index) {
  Skip *n = arena_.emplace<Skip>();
  set_node_loc(n, index);
  return n;
}

} // namespace shell_lite

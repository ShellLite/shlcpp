#include "parser/parser_internal.hpp"

namespace shell_lite {


Node *Parser::bind_return(int index) {
  auto tokens = flat_nodes_[index].tokens;
  SubParser parser(tokens, arena_, *this);
  parser.advance();
  Return *node = arena_.emplace<Return>();
  node->value = parser.parse_expression();
  if (!node->value && !parser.is_at_end())
    throw SyntaxError(
        "Syntax error: Invalid expression for return at line " +
        std::to_string(flat_nodes_[index].line));
  set_node_loc(node, index);
  return node;
}


Node *Parser::bind_const(int index) {
  auto tokens = flat_nodes_[index].tokens;
  SubParser parser(tokens, arena_, *this);
  parser.consume(TokenType::TOK_CONST, "Expected 'const'");
  std::string_view name =
      parser.consume(TokenType::TOK_ID, "Expected constant name").value;
  parser.consume(TokenType::TOK_ASSIGN, "Expected '=' after constant name");
  Node *value = parser.parse_expression();
  ConstAssign *node = arena_.emplace<ConstAssign>();
  node->name = name;
  node->value = value;
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_func(int index) {
  auto tokens = flat_nodes_[index].tokens;
  SubParser parser(tokens, arena_, *this);
  if (parser.check(TokenType::TOK_FUNCTION) ||
      parser.check(TokenType::TOK_TO)) {
    parser.advance();
  }

  if (parser.is_at_end())
    return nullptr;
  Token name_token = parser.advance();
  if (name_token.value.empty())
    throw SyntaxError("Expected function name", {"", 0, 0});
  std::string_view name = name_token.value;
  FunctionDef *node = arena_.emplace<FunctionDef>();
  node->name = name;

  // Optional introductory preposition: with, take, of
  parser.match({TokenType::TOK_OF, TokenType::TOK_WITH, TokenType::TOK_TAKE});

  while (!parser.is_at_end()) {
    if (parser.match(TokenType::TOK_LPAREN) || parser.match(TokenType::TOK_RPAREN)) {
      continue;
    }
    if (is_identifier_like(parser.peek().type)) {
      std::string_view arg_name = parser.advance().value;
      Node *default_val = nullptr;
      std::optional<std::string_view> type_hint;

      if (parser.match(TokenType::TOK_AS)) {
        if (is_identifier_like(parser.peek().type)) {
          type_hint = parser.advance().value;
        }
      }
      if (parser.match({TokenType::TOK_ASSIGN, TokenType::TOK_IS})) {
        default_val = parser.parse_expression();
      }
      node->args.push_back({arg_name, default_val, type_hint});
      parser.match({TokenType::TOK_COMMA, TokenType::TOK_AND});
    } else {
      break;
    }
  }

  node->body = bind_statement_list(flat_nodes_[index].child_indices);
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_structure(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  std::string_view name = (tokens.size() > 1) ? tokens[1].value : "Thing";
  ClassDef *node = arena_.emplace<ClassDef>();
  node->name = name;

  for (size_t k = 0; k < tokens.size(); k++) {
    if (tokens[k].type == TokenType::TOK_EXTENDS && k + 1 < tokens.size()) {
      node->parent = tokens[k + 1].value;
    }
  }

  bool has_explicit_init = false;
  for (int child_idx : flat_nodes_[index].child_indices) {
    Token head = get_effective_head(child_idx);
    if (head.type == TokenType::TOK_HAS) {
      const auto &c_tokens = flat_nodes_[child_idx].tokens;
      size_t assign_pos = static_cast<size_t>(-1);
      for (size_t k = 1; k < c_tokens.size(); k++) {
        if (c_tokens[k].type == TokenType::TOK_ASSIGN ||
            c_tokens[k].type == TokenType::TOK_IS ||
            c_tokens[k].type == TokenType::TOK_BE) {
          assign_pos = k;
          break;
        }
      }
      if (assign_pos != static_cast<size_t>(-1)) {
        std::string_view prop_name = c_tokens[1].value;
        Node *def_val = parse_expr_recursive(
            std::vector<Token>(c_tokens.begin() + assign_pos + 1,
                               c_tokens.end()),
            flat_nodes_[child_idx].child_indices);
        node->properties.push_back({prop_name, def_val});
      } else {
        for (size_t k = 1; k < c_tokens.size(); k++) {
          if (c_tokens[k].type == TokenType::TOK_ID) {
            node->properties.push_back({c_tokens[k].value, nullptr});
          }
        }
      }
    } else if (head.type == TokenType::TOK_TO ||
               head.type == TokenType::TOK_FUNCTION) {
      FunctionDef *fn = static_cast<FunctionDef *>(bind_func(child_idx));
      if (fn->name == "init")
        has_explicit_init = true;
      node->methods.push_back(fn);
    }
  }

  if (!has_explicit_init) {
    FunctionDef *init_fn = arena_.emplace<FunctionDef>();
    init_fn->name = "init";
    for (auto &prop : node->properties) {
      init_fn->args.push_back({prop.first, prop.second, std::nullopt});
      PropertyAssign *pa = arena_.emplace<PropertyAssign>();
      pa->instance_name = "self";
      pa->property_name = prop.first;
      pa->value = arena_.emplace<VarAccess>(prop.first);
      init_fn->body.push_back(pa);
    }
    node->methods.push_back(init_fn);
  }
  set_node_loc(node, index);
  return node;
}


Node *Parser::bind_complex_assignment(int index, size_t assign_idx) {
  const auto &tokens = flat_nodes_[index].tokens;
  Node *lhs = parse_expr_recursive(
      std::vector<Token>(tokens.begin(), tokens.begin() + assign_idx));
  Node *rhs = parse_expr_recursive(
      std::vector<Token>(tokens.begin() + assign_idx + 1, tokens.end()),
      flat_nodes_[index].child_indices);

  TokenType op = tokens[assign_idx].type;
  if (op != TokenType::TOK_ASSIGN && op != TokenType::TOK_IS &&
      op != TokenType::TOK_BE) {
    std::string_view op_str = get_compound_op_str(op);
    rhs = arena_.emplace<BinOp>(lhs, op_str, rhs);
  }

  if (IndexAccess *idx = dynamic_cast<IndexAccess *>(lhs)) {
    IndexAssign *ia = arena_.emplace<IndexAssign>(idx->obj, idx->index, rhs);
    set_node_loc(ia, index);
    return ia;
  }
  if (PropertyAccess *prop = dynamic_cast<PropertyAccess *>(lhs)) {
    PropertyAssign *pa = arena_.emplace<PropertyAssign>();
    pa->instance_name = prop->instance_name;
    pa->property_name = prop->property_name;
    pa->value = rhs;
    set_node_loc(pa, index);
    return pa;
  }
  if (VarAccess *v = dynamic_cast<VarAccess *>(lhs)) {
    Assign *a = arena_.emplace<Assign>(v->name, rhs);
    set_node_loc(a, index);
    return a;
  }
  throw SyntaxError(
      "Syntax error: Invalid assignment target at line " +
      std::to_string(flat_nodes_[index].line));
}


Node *Parser::bind_define(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  // define page X [using arg1, arg2]
  if (tokens.size() >= 3 && tokens[1].type == TokenType::TOK_PAGE) {
    FunctionDef *node = arena_.emplace<FunctionDef>();
    node->name = tokens[2].value;
    // Check for 'using' parameter list
    for (size_t k = 3; k < tokens.size(); k++) {
      if (tokens[k].type == TokenType::TOK_USING) {
        for (size_t j = k + 1; j < tokens.size(); j++) {
          if (tokens[j].type == TokenType::TOK_ID) {
            node->args.push_back({tokens[j].value, nullptr, std::nullopt});
          }
        }
        break;
      }
    }
    node->body = bind_statement_list(flat_nodes_[index].child_indices);
    set_node_loc(node, index);
    return node;
  }
  // Generic define: define X name → Call("define_X", [name, block])
  if (tokens.size() >= 3 && tokens[1].type == TokenType::TOK_ID) {
    std::string func_name = "define_" + std::string(tokens[1].value);
    Call *call = arena_.emplace<Call>(arena_.emplace_string(func_name));
    call->args.push_back(arena_.emplace<String>(tokens[2].value));
    AnonymousFunction *block = arena_.emplace<AnonymousFunction>();
    block->body = bind_statement_list(flat_nodes_[index].child_indices);
    call->args.push_back(block);
    set_node_loc(call, index);
    return call;
  }
  // Fallback: treat define as function definition
  return bind_func(index);
}

} // namespace shell_lite

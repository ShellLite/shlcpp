#include "parser/parser_internal.hpp"

namespace shell_lite {

Parser::Parser(std::string source) : source_code_(std::move(source)) {}

std::vector<Node *> Parser::parse() {
  TopographyResult topo;
  if (parallel_enabled_) {
    topo = phase1_topography_scan_parallel(source_code_, num_threads_);
  } else {
    topo = phase1_topography_scan(source_code_);
  }
  return parse_with_topography(std::move(topo));
}

std::vector<Node *> Parser::parse_with_topography(TopographyResult topo) {
  try {
    flat_nodes_ = std::move(topo.nodes);
    diagnostics_ = std::move(topo.diagnostics);

    if (flat_nodes_.empty()) {
      if (!diagnostics_.empty()) {
        throw diagnostics_.front();
      }
      return {};
    }

    Lexer lexer("");
    for (size_t i = 0; i < flat_nodes_.size(); ++i) {
      flat_nodes_[i].tokens =
          lexer.tokenize_line_only(flat_nodes_[i].raw_text, flat_nodes_[i].line);

      bool complex = false;
      int depth = 0;
      for (const auto &token : flat_nodes_[i].tokens) {
        TokenType type = token.type;
        if (type == TokenType::TOK_LPAREN || type == TokenType::TOK_LBRACKET ||
            type == TokenType::TOK_LBRACE) {
          depth++;
          if (depth > 1)
            complex = true;
        } else if (type == TokenType::TOK_RPAREN ||
                   type == TokenType::TOK_RBRACKET ||
                   type == TokenType::TOK_RBRACE) {
          depth = std::max(0, depth - 1);
        } else if (type == TokenType::TOK_ARROW || type == TokenType::TOK_TAKE ||
                   type == TokenType::TOK_FROM) {
          complex = true;
          break;
        }
      }
      if (depth > 0)
        complex = true;
      flat_nodes_[i].is_complex = complex;
    }

    phase2_topology_linking(flat_nodes_);

    root_node_indices_.clear();
    for (size_t i = 0; i < flat_nodes_.size(); ++i) {
      if (flat_nodes_[i].parent_index == -1) {
        root_node_indices_.push_back(static_cast<int>(i));
      }
    }

    return bind_statement_list(root_node_indices_);
  } catch (shlcppError &err) {
    if (err.location.source_line.empty()) {
      int line_num = err.location.line > 0 ? err.location.line : 1;
      err.location.source_line = extract_source_line(source_code_, line_num);
      err.location.line = line_num;
    }
    throw;
  }
}

std::vector<Node *>
Parser::bind_statement_list(const std::vector<int> &indices) {
  std::vector<Node *> ast;
  size_t i = 0;
  while (i < indices.size()) {
    int idx = indices[i];
    if (flat_nodes_[idx].tokens.empty()) {
      i++;
      continue;
    }
    Token head = get_effective_head(idx);

    if (head.type == TokenType::TOK_IF) {
      If *if_node = static_cast<If *>(bind_if(idx));
      size_t j = i + 1;
      If *last_chain = if_node;
      while (j < indices.size()) {
        Token next_head = get_effective_head(indices[j]);
        if (next_head.type == TokenType::TOK_ELIF) {
          If *elif_node = static_cast<If *>(bind_if(indices[j]));
          last_chain->else_body.push_back(elif_node);
          last_chain = elif_node;
          j++;
        } else if (next_head.type == TokenType::TOK_ELSE) {
          last_chain->else_body =
              bind_statement_list(flat_nodes_[indices[j]].child_indices);
          j++;
          break;
        } else {
          break;
        }
      }
      ast.push_back(if_node);
      i = j;
      continue;
    }

    if (head.type == TokenType::TOK_TRY) {
      Try *try_node = arena_.emplace<Try>();
      try_node->try_body = bind_statement_list(flat_nodes_[idx].child_indices);
      try_node->catch_var = "e";

      size_t j = i + 1;
      bool has_always = false;
      std::vector<Node *> always_body;

      while (j < indices.size()) {
        Token next_head = get_effective_head(indices[j]);
        if (next_head.type == TokenType::TOK_CATCH) {
          const auto &c_tokens = flat_nodes_[indices[j]].tokens;
          std::string_view c_var = "e";
          for (size_t k = 1; k < c_tokens.size(); ++k) {
            if (c_tokens[k].type == TokenType::TOK_ID) {
              c_var = c_tokens[k].value;
              break;
            }
          }
          try_node->catch_var = c_var;
          try_node->catch_body =
              bind_statement_list(flat_nodes_[indices[j]].child_indices);
          j++;
        } else if (next_head.type == TokenType::TOK_ALWAYS) {
          always_body =
              bind_statement_list(flat_nodes_[indices[j]].child_indices);
          has_always = true;
          j++;
        } else {
          break;
        }
      }

      if (has_always) {
        TryAlways *ta = arena_.emplace<TryAlways>();
        ta->try_body = try_node->try_body;
        ta->catch_var = try_node->catch_var;
        ta->catch_body = try_node->catch_body;
        ta->always_body = always_body;
        ast.push_back(ta);
      } else {
        ast.push_back(try_node);
      }
      i = j;
      continue;
    }

    Node *node = bind_node(idx);
    if (node)
      ast.push_back(node);
    i++;
  }
  return ast;
}

Node *Parser::bind_node(int index) {
  Token head = get_effective_head(index);

  if (head.type == TokenType::TOK_IF || head.type == TokenType::TOK_UNLESS ||
      head.type == TokenType::TOK_UNTIL || head.type == TokenType::TOK_WHILE ||
      head.type == TokenType::TOK_FOR || head.type == TokenType::TOK_REPEAT ||
      head.type == TokenType::TOK_FOREVER ||
      head.type == TokenType::TOK_PRINT || head.type == TokenType::TOK_SAY ||
      head.type == TokenType::TOK_RETURN || head.type == TokenType::TOK_TO ||
      head.type == TokenType::TOK_FUNCTION ||
      head.type == TokenType::TOK_STRUCTURE ||
      head.type == TokenType::TOK_CONST || head.type == TokenType::TOK_TRY ||
      head.type == TokenType::TOK_MATCHES || head.type == TokenType::TOK_WHEN ||
      head.type == TokenType::TOK_DEFINE || head.type == TokenType::TOK_SPAWN ||
      head.type == TokenType::TOK_AWAIT || head.type == TokenType::TOK_EXIT ||
      head.type == TokenType::TOK_STOP || head.type == TokenType::TOK_SKIP ||
      head.type == TokenType::TOK_PARALLEL ||
      head.type == TokenType::TOK_LOCK || head.type == TokenType::TOK_SEND ||
      head.type == TokenType::TOK_RECEIVE ||
      head.type == TokenType::TOK_EXPECT ||
      head.type == TokenType::TOK_ENSURE || head.type == TokenType::TOK_TEST ||
      head.type == TokenType::TOK_IMPORT || head.type == TokenType::TOK_USE ||
      head.type == TokenType::TOK_CHECK || head.type == TokenType::TOK_HAS ||
      head.type == TokenType::TOK_FROM ||
      head.type == TokenType::TOK_DB || head.type == TokenType::TOK_SERVE ||
      head.type == TokenType::TOK_LISTEN || head.type == TokenType::TOK_ADD ||
      head.type == TokenType::TOK_REMOVE || head.type == TokenType::TOK_EVERY ||
      head.type == TokenType::TOK_AFTER || head.type == TokenType::TOK_WRITE ||
      head.type == TokenType::TOK_APPEND ||
      head.type == TokenType::TOK_NAMESPACE ||
      head.type == TokenType::TOK_ERROR) {
    return bind_head_dispatcher(index, head.type);
  }

  const auto &tokens = flat_nodes_[index].tokens;

  size_t assign_idx = static_cast<size_t>(-1);
  for (size_t k = 0; k < tokens.size(); k++) {
    if (tokens[k].type == TokenType::TOK_ASSIGN ||
        tokens[k].type == TokenType::TOK_PLUSEQ ||
        tokens[k].type == TokenType::TOK_MINUSEQ ||
        tokens[k].type == TokenType::TOK_MULEQ ||
        tokens[k].type == TokenType::TOK_DIVEQ ||
        tokens[k].type == TokenType::TOK_MODEQ ||
        tokens[k].type == TokenType::TOK_IS ||
        tokens[k].type == TokenType::TOK_BE) {
      assign_idx = k;
      break;
    }
  }

  if (assign_idx != static_cast<size_t>(-1)) {
    bool has_accessor = false;
    for (size_t k = 0; k < assign_idx; k++) {
      if (tokens[k].type == TokenType::TOK_LBRACKET ||
          tokens[k].type == TokenType::TOK_DOT) {
        has_accessor = true;
        break;
      }
    }

    if (tokens[assign_idx].type == TokenType::TOK_IS || tokens[assign_idx].type == TokenType::TOK_BE) {
      size_t id_count = 0;
      std::string_view name;
      for (size_t k = 0; k < assign_idx; k++)
        if (tokens[k].type == TokenType::TOK_ID) {
          id_count++;
          name = tokens[k].value;
        }
      if (!has_accessor && id_count == 1) {
        Node *val = parse_expr_recursive(
            std::vector<Token>(tokens.begin() + assign_idx + 1, tokens.end()),
            flat_nodes_[index].child_indices);
        Assign *a = arena_.emplace<Assign>(name, val);
        set_node_loc(a, index);
        return a;
      }
    } else {
      size_t id_count = 0;
      std::string_view name;
      for (size_t k = 0; k < assign_idx; k++)
        if (tokens[k].type == TokenType::TOK_ID) {
          id_count++;
          name = tokens[k].value;
        }
      if (!has_accessor && id_count == 1) {
        Node *val = parse_expr_recursive(
            std::vector<Token>(tokens.begin() + assign_idx + 1, tokens.end()),
            flat_nodes_[index].child_indices);

        TokenType op = tokens[assign_idx].type;
        if (op != TokenType::TOK_ASSIGN) {
          std::string_view op_str = get_compound_op_str(op);
          val =
              arena_.emplace<BinOp>(arena_.emplace<VarAccess>(name), op_str, val);
        }
        Assign *a = arena_.emplace<Assign>(name, val);
        set_node_loc(a, index);
        return a;
      }
      return bind_complex_assignment(index, assign_idx);
    }
  }

  return bind_head_dispatcher(index, head.type);
}

Node *Parser::bind_head_dispatcher(int index, TokenType type) {
  Token head = get_effective_head(index);
  switch (head.type) {
  case TokenType::TOK_IF:
    return bind_if(index);
  case TokenType::TOK_UNLESS:
    return bind_unless(index);
  case TokenType::TOK_UNTIL:
    return bind_until(index);
  case TokenType::TOK_WHILE:
    return bind_while(index);
  case TokenType::TOK_FOR:
    return bind_for(index);
  case TokenType::TOK_REPEAT:
    return bind_repeat(index);
  case TokenType::TOK_FOREVER:
    return bind_forever(index);
  case TokenType::TOK_PRINT:
  case TokenType::TOK_SAY:
    return bind_print(index);
  case TokenType::TOK_RETURN:
    return bind_return(index);
  case TokenType::TOK_TO:
  case TokenType::TOK_FUNCTION:
    return bind_func(index);
  case TokenType::TOK_STRUCTURE:
    return bind_structure(index);
  case TokenType::TOK_CONST:
    return bind_const(index);
  case TokenType::TOK_TRY:
    return bind_try(index);
  case TokenType::TOK_MATCHES:
    return bind_match(index);
  case TokenType::TOK_WHEN:
    return bind_when_clause(index);
  case TokenType::TOK_DEFINE:
    return bind_define(index);
  case TokenType::TOK_SPAWN:
    return bind_spawn(index);
  case TokenType::TOK_AWAIT:
    return bind_await(index);
  case TokenType::TOK_EXIT:
    return bind_exit(index);
  case TokenType::TOK_STOP:
    return bind_stop(index);
  case TokenType::TOK_SKIP:
    return bind_skip(index);
  case TokenType::TOK_PARALLEL:
    return bind_parallel(index);
  case TokenType::TOK_LOCK:
    return bind_lock(index);
  case TokenType::TOK_SEND:
    return bind_send(index);
  case TokenType::TOK_RECEIVE:
    return bind_receive(index);
  case TokenType::TOK_CHECK:
  case TokenType::TOK_EXPECT:
  case TokenType::TOK_ENSURE:
    return bind_assertion(index);
  case TokenType::TOK_TEST:
    return bind_test_block(index);
  case TokenType::TOK_IMPORT:
  case TokenType::TOK_USE:
    return bind_import_enhanced(index);
  case TokenType::TOK_FROM:
    return bind_from_import(index);
  case TokenType::TOK_HAS:
    throw SyntaxError("Syntax error: 'has' property definition is only valid inside structure bodies at line " +
                             std::to_string(flat_nodes_[index].line));
  case TokenType::TOK_DB:
    return bind_db(index);
  case TokenType::TOK_SERVE:
    return bind_serve(index);
  case TokenType::TOK_LISTEN:
    return bind_listen(index);
  case TokenType::TOK_ADD:
    return bind_nlp_add(index);
  case TokenType::TOK_REMOVE:
    return bind_nlp_remove(index);
  case TokenType::TOK_EVERY:
    return bind_nlp_timer(index, true);
  case TokenType::TOK_AFTER:
    return bind_nlp_timer(index, false);
  case TokenType::TOK_WRITE:
    return bind_file_write(index, false);
  case TokenType::TOK_APPEND:
    return bind_file_write(index, true);
  case TokenType::TOK_NAMESPACE:
    return bind_namespace(index);
  case TokenType::TOK_ERROR:
    return bind_throw(index);

  default:
    return bind_expression_statement(index);
  }
}

void Parser::set_node_loc(Node *node, const Token &start, const Token &end) {
  node->line = start.line;
  node->col = start.col;
  node->end_line = end.line;
  node->end_col = end.col + static_cast<int>(end.value.size());
}
void Parser::set_node_loc(Node *node, int index) {
  node->line = flat_nodes_[index].line;
}

Node *Parser::bind_expression_statement(int index) {
  Node *expr = parse_expr_recursive(flat_nodes_[index].tokens,
                                    flat_nodes_[index].is_complex);
  if (!expr && !flat_nodes_[index].tokens.empty())
    throw SyntaxError(
        "Syntax error: Invalid expression statement at line " +
        std::to_string(flat_nodes_[index].line));

  if (expr && !flat_nodes_[index].child_indices.empty()) {
    if (Call *call = dynamic_cast<Call *>(expr)) {
      AnonymousFunction *block = arena_.emplace<AnonymousFunction>();
      block->body = bind_statement_list(flat_nodes_[index].child_indices);
      call->args.push_back(block);
    }
  }
  return expr;
}

Token Parser::get_effective_head(int index) {
  if (!flat_nodes_[index].tokens.empty())
    return flat_nodes_[index].tokens[0];
  return Token(TokenType::TOK_ILLEGAL, "", flat_nodes_[index].line, 1);
}

std::vector<Token> Parser::extract_expr_tokens(const std::vector<Token> &tokens,
                                               int start) {
  if (start >= (int)tokens.size())
    return {};
  size_t end = tokens.size();
  if (!tokens.empty() && tokens.back().type == TokenType::TOK_COLON)
    end--;
  return std::vector<Token>(tokens.begin() + start, tokens.begin() + end);
}

int Parser::get_precedence(TokenType type) {
  switch (type) {
  case TokenType::TOK_ASSIGN:
  case TokenType::TOK_IS:
  case TokenType::TOK_BE:
    return 5;
  case TokenType::TOK_OR:
    return 10;
  case TokenType::TOK_AND:
    return 20;
  case TokenType::TOK_BIT_OR:
    return 22;
  case TokenType::TOK_BIT_XOR:
    return 24;
  case TokenType::TOK_BIT_AND:
    return 26;
  case TokenType::TOK_EQ:
  case TokenType::TOK_NEQ:
    return 30;
  case TokenType::TOK_OP_LT:
  case TokenType::TOK_LE:
  case TokenType::TOK_OP_GT:
  case TokenType::TOK_GE:
    return 40;
  case TokenType::TOK_LSHIFT:
  case TokenType::TOK_RSHIFT:
    return 45;
  case TokenType::TOK_PLUS_OP:
  case TokenType::TOK_MINUS_OP:
    return 50;
  case TokenType::TOK_MUL_OP:
  case TokenType::TOK_DIV_OP:
  case TokenType::TOK_MOD_OP:
    return 60;
  case TokenType::TOK_POW:
    return 70;
  default:
    return 0;
  }
}

std::string_view Parser::op_to_str(TokenType type) {
  switch (type) {
  case TokenType::TOK_PLUS_OP:
    return "+";
  case TokenType::TOK_MINUS_OP:
    return "-";
  case TokenType::TOK_MUL_OP:
    return "*";
  case TokenType::TOK_DIV_OP:
    return "/";
  case TokenType::TOK_MOD_OP:
    return "%";
  case TokenType::TOK_POW:
    return "**";
  case TokenType::TOK_EQ:
    return "==";
  case TokenType::TOK_NEQ:
    return "!=";
  case TokenType::TOK_OP_LT:
    return "<";
  case TokenType::TOK_LE:
    return "<=";
  case TokenType::TOK_OP_GT:
    return ">";
  case TokenType::TOK_GE:
    return ">=";
  case TokenType::TOK_BIT_AND:
    return "&";
  case TokenType::TOK_BIT_OR:
    return "|";
  case TokenType::TOK_BIT_XOR:
    return "^";
  case TokenType::TOK_BIT_NOT:
    return "~";
  case TokenType::TOK_LSHIFT:
    return "<<";
  case TokenType::TOK_RSHIFT:
    return ">>";
  case TokenType::TOK_AND:
    return "and";
  case TokenType::TOK_OR:
    return "or";
  default:
    return "?";
  }
}

} // namespace shell_lite


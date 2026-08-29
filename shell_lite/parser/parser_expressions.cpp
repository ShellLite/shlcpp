#include "parser/parser_internal.hpp"

namespace shell_lite {


Parser::SubParser::SubParser(const std::vector<Token> &tokens, Arena &arena,
                             Parser &parent, bool is_complex,
                             const std::vector<int> &child_indices)
    : tokens_(tokens), arena_(arena), parent_(parent), is_complex_(is_complex),
      child_indices_(child_indices) {}

Node *Parser::SubParser::parse_expression() {
  return parse_assignment();
}

static std::string unescape_string_literal(std::string_view sv) {
  std::string res;
  res.reserve(sv.size());
  for (size_t i = 0; i < sv.size(); ++i) {
    if (sv[i] == '\\' && i + 1 < sv.size()) {
      char next = sv[++i];
      switch (next) {
      case 'n':
        res += '\n';
        break;
      case 't':
        res += '\t';
        break;
      case 'r':
        res += '\r';
        break;
      case 'a':
        res += '\a';
        break;
      case 'b':
        res += '\b';
        break;
      case 'f':
        res += '\f';
        break;
      case 'v':
        res += '\v';
        break;
      case '\\':
        res += '\\';
        break;
      case '"':
        res += '"';
        break;
      case '\'':
        res += '\'';
        break;
      case '0':
        res += '\0';
        break;
      case 'x': {
        if (i + 2 < sv.size() && isxdigit(sv[i + 1]) && isxdigit(sv[i + 2])) {
          auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
          };
          char byte = static_cast<char>((hex_val(sv[i + 1]) << 4) | hex_val(sv[i + 2]));
          res += byte;
          i += 2;
        } else {
          res += 'x';
        }
        break;
      }
      default:
        res += next;
        break;
      }
    } else {
      res += sv[i];
    }
  }
  return res;
}

Node *Parser::SubParser::parse_opg() {
  std::vector<Node *> output_stack;
  std::vector<Token> operator_stack;

  auto apply_operator = [&]() {
    if (operator_stack.empty())
      return false;
    Token op = operator_stack.back();
    operator_stack.pop_back();

    if (op.type == TokenType::TOK_NOT) {
      if (output_stack.empty())
        return false;
      Node *right = output_stack.back();
      output_stack.pop_back();
      output_stack.push_back(arena_.emplace<UnaryOp>("not", right));
      return true;
    }

    if (output_stack.size() < 2)
      return false;
    Node *right = output_stack.back();
    output_stack.pop_back();
    Node *left = output_stack.back();
    output_stack.pop_back();

    std::string_view op_str = parent_.op_to_str(op.type);
    output_stack.push_back(arena_.emplace<BinOp>(left, op_str, right));
    return true;
  };

  bool expect_operand = true;

  for (size_t i = 0; i < tokens_.size(); ++i) {
    const Token &token = tokens_[i];
    if (token.type == TokenType::TOK_NOISE)
      continue;

    if (expect_operand) {
      if (token.type == TokenType::TOK_NUMBER) {
        double val = 0;
        std::from_chars(token.value.data(),
                        token.value.data() + token.value.size(), val);
        output_stack.push_back(arena_.emplace<Number>(val));
        expect_operand = false;
      } else if (token.type == TokenType::TOK_STRING) {
        output_stack.push_back(arena_.emplace<String>(
            arena_.emplace_string(unescape_string_literal(token.value))));
        expect_operand = false;
      } else if (token.type == TokenType::TOK_YES ||
                 token.type == TokenType::TOK_NO) {
        output_stack.push_back(
            arena_.emplace<Boolean>(token.type == TokenType::TOK_YES));
        expect_operand = false;
      } else if (token.type == TokenType::TOK_ID) {
        output_stack.push_back(arena_.emplace<VarAccess>(token.value));
        expect_operand = false;
      } else if (token.type == TokenType::TOK_NOT) {
        operator_stack.push_back(token);
      } else {
        return nullptr;
      }
    } else {
      if (token.type == TokenType::TOK_DOT) {
        if (output_stack.empty() || i + 1 >= tokens_.size() ||
            tokens_[i + 1].type != TokenType::TOK_ID)
          return nullptr;
        Node *obj = output_stack.back();
        output_stack.pop_back();
        std::string_view prop_name = tokens_[++i].value;
        if (VarAccess *v = dynamic_cast<VarAccess *>(obj)) {
          output_stack.push_back(
              arena_.emplace<PropertyAccess>(v->name, prop_name));
        } else {
          return nullptr;
        }
        continue;
      }

      int prec = parent_.get_precedence(token.type);
      if (prec == 0) {
        return nullptr;
      }

      while (!operator_stack.empty() &&
             parent_.get_precedence(operator_stack.back().type) >= prec) {
        if (!apply_operator())
          return nullptr;
      }
      operator_stack.push_back(token);
      expect_operand = true;
    }
  }

  while (!operator_stack.empty()) {
    if (!apply_operator())
      return nullptr;
  }

  if (output_stack.size() == 1 && !expect_operand) {
    return output_stack[0];
  }
  return nullptr;
}

Node *Parser::SubParser::parse_assignment() {
  Node *node = parse_logical_or();
  if (match({TokenType::TOK_ASSIGN, TokenType::TOK_IS, TokenType::TOK_BE,
             TokenType::TOK_PLUSEQ, TokenType::TOK_MINUSEQ})) {
    Token op = previous();
    Node *value = parse_assignment();
    if (!value)
      throw SyntaxError(
          "Syntax error: Missing right operand for assignment at line " +
          std::to_string(op.line));
    if (VarAccess *v = dynamic_cast<VarAccess *>(node))
      return arena_.emplace<Assign>(v->name, value);
    if (IndexAccess *idx = dynamic_cast<IndexAccess *>(node))
      return arena_.emplace<IndexAssign>(idx->obj, idx->index, value);
    if (PropertyAccess *prop = dynamic_cast<PropertyAccess *>(node)) {
      PropertyAssign *pa = arena_.emplace<PropertyAssign>();
      pa->instance_name = prop->instance_name;
      pa->property_name = prop->property_name;
      pa->value = value;
      return pa;
    }
  }
  return node;
}

Node *Parser::SubParser::parse_logical_or() {
  Node *node = parse_logical_and();
  while (match(TokenType::TOK_OR)) {
    Token op = previous();
    Node *right = parse_logical_and();
    if (!right)
      throw SyntaxError(
          "Syntax error: Missing right operand for 'or' at line " +
          std::to_string(op.line));
    node = arena_.emplace<BinOp>(node, "or", right);
  }
  return node;
}

Node *Parser::SubParser::parse_logical_and() {
  Node *node = parse_bitwise_or();
  while (match(TokenType::TOK_AND)) {
    Token op = previous();
    Node *right = parse_bitwise_or();
    if (!right)
      throw SyntaxError(
          "Syntax error: Missing right operand for 'and' at line " +
          std::to_string(op.line));
    node = arena_.emplace<BinOp>(node, "and", right);
  }
  return node;
}

Node *Parser::SubParser::parse_bitwise_or() {
  Node *node = parse_bitwise_xor();
  while (match(TokenType::TOK_BIT_OR)) {
    Token op = previous();
    Node *right = parse_bitwise_xor();
    if (!right)
      throw SyntaxError(
          "Syntax error: Missing right operand for '|' at line " +
          std::to_string(op.line));
    node = arena_.emplace<BinOp>(node, "|", right);
  }
  return node;
}

Node *Parser::SubParser::parse_bitwise_xor() {
  Node *node = parse_bitwise_and();
  while (match(TokenType::TOK_BIT_XOR)) {
    Token op = previous();
    Node *right = parse_bitwise_and();
    if (!right)
      throw SyntaxError(
          "Syntax error: Missing right operand for '^' at line " +
          std::to_string(op.line));
    node = arena_.emplace<BinOp>(node, "^", right);
  }
  return node;
}

Node *Parser::SubParser::parse_bitwise_and() {
  Node *node = parse_equality();
  while (match(TokenType::TOK_BIT_AND)) {
    Token op = previous();
    Node *right = parse_equality();
    if (!right)
      throw SyntaxError(
          "Syntax error: Missing right operand for '&' at line " +
          std::to_string(op.line));
    node = arena_.emplace<BinOp>(node, "&", right);
  }
  return node;
}

Node *Parser::SubParser::parse_equality() {
  Node *node = parse_comparison();
  while (match({TokenType::TOK_EQ, TokenType::TOK_NEQ})) {
    Token op = previous();
    Node *right = parse_comparison();
    if (!right)
      throw SyntaxError("Syntax error: Missing right operand for '" +
                               std::string(op.value) + "' at line " +
                               std::to_string(op.line),
                        SourceLocation{"", op.line, op.col});
    node = arena_.emplace<BinOp>(
        node, op.type == TokenType::TOK_EQ ? "==" : "!=", right);
  }
  return node;
}

Node *Parser::SubParser::parse_comparison() {
  Node *node = parse_shift();
  while (match({TokenType::TOK_OP_LT, TokenType::TOK_LE, TokenType::TOK_OP_GT,
                TokenType::TOK_GE, TokenType::TOK_IN, TokenType::TOK_NOTIN,
                TokenType::TOK_IS, TokenType::TOK_NOT})) {
    Token op = previous();
    if (op.type == TokenType::TOK_NOT) {
      if (match(TokenType::TOK_IN)) {
        Node *right = parse_shift();
        if (!right)
          throw SyntaxError("Syntax error: Missing right operand for 'not in' at line " +
                                   std::to_string(op.line),
                            SourceLocation{"", op.line, op.col});
        node = arena_.emplace<BinOp>(node, "not in", right);
      } else {
        pos_--;
        break;
      }
    } else if (op.type == TokenType::TOK_IS) {
      if (match({TokenType::TOK_EVEN, TokenType::TOK_ODD, TokenType::TOK_PRIME,
                 TokenType::TOK_POSITIVE, TokenType::TOK_NEGATIVE})) {
        std::string traits[] = {"even", "odd", "prime", "positive", "negative"};
        int trait_idx = (int)previous().type - (int)TokenType::TOK_EVEN;
        Call *call = arena_.emplace<Call>(traits[trait_idx]);
        call->args = {node};
        node = call;
        continue;
      }

      std::string_view op_str = "==";
      if (match(TokenType::TOK_AT)) {
        consume(TokenType::TOK_LEAST, "Expected 'least' after 'is at'");
        op_str = ">=";
      } else if (match(TokenType::TOK_EXACTLY)) {
        op_str = "==";
      } else if (match(TokenType::TOK_LESS)) {
        consume(TokenType::TOK_THAN, "Expected 'than' after 'is less'");
        op_str = "<";
      } else if (match(TokenType::TOK_MORE)) {
        consume(TokenType::TOK_THAN, "Expected 'than' after 'is more'");
        op_str = ">";
      } else if (match(TokenType::TOK_NOT)) {
        if (match(TokenType::TOK_IN)) {
          op_str = "not in";
        } else {
          op_str = "!=";
        }
      } else if (match(TokenType::TOK_IN)) {
        op_str = "in";
      }

      Node *right = parse_shift();
      if (!right)
        throw SyntaxError("Syntax error: Missing right operand for 'is' expression at line " +
                                 std::to_string(op.line),
                          SourceLocation{"", op.line, op.col});
      node = arena_.emplace<BinOp>(node, op_str, right);
    } else {
      std::string_view op_str = (op.type == TokenType::TOK_OP_LT)   ? "<"
                                : (op.type == TokenType::TOK_LE)    ? "<="
                                : (op.type == TokenType::TOK_OP_GT) ? ">"
                                : (op.type == TokenType::TOK_GE)    ? ">="
                                : (op.type == TokenType::TOK_IN)    ? "in"
                                                                    : "not in";
      Node *right = parse_shift();
      if (!right)
        throw SyntaxError("Syntax error: Missing right operand for '" +
                                 std::string(op.value) + "' at line " +
                                 std::to_string(op.line),
                          SourceLocation{"", op.line, op.col});
      node = arena_.emplace<BinOp>(node, op_str, right);
    }
  }
  return node;
}

Node *Parser::SubParser::parse_shift() {
  Node *node = parse_additive();
  while (match({TokenType::TOK_LSHIFT, TokenType::TOK_RSHIFT})) {
    Token op = previous();
    Node *right = parse_additive();
    if (!right)
      throw SyntaxError(
          "Syntax error: Missing right operand for shift at line " +
          std::to_string(op.line),
          SourceLocation{"", op.line, op.col});
    node = arena_.emplace<BinOp>(
        node, op.type == TokenType::TOK_LSHIFT ? "<<" : ">>", right);
  }
  return node;
}

Node *Parser::SubParser::parse_additive() {
  Node *node = parse_multiplicative();
  while (match({TokenType::TOK_PLUS_OP, TokenType::TOK_MINUS_OP})) {
    Token op = previous();
    Node *right = parse_multiplicative();
    if (!right)
      throw SyntaxError("Syntax error: Missing right operand for '" +
                               std::string(op.value) + "' at line " +
                               std::to_string(op.line),
                        SourceLocation{"", op.line, op.col});
    node = arena_.emplace<BinOp>(
        node, op.type == TokenType::TOK_PLUS_OP ? "+" : "-", right);
  }
  return node;
}

Node *Parser::SubParser::parse_multiplicative() {
  Node *node = parse_power();
  while (match(
      {TokenType::TOK_MUL_OP, TokenType::TOK_DIV_OP, TokenType::TOK_MOD_OP})) {
    Token op = previous();
    std::string_view op_str = (op.type == TokenType::TOK_MUL_OP)   ? "*"
                              : (op.type == TokenType::TOK_DIV_OP) ? "/"
                              : "%";
    Node *right = parse_power();
    if (!right)
      throw SyntaxError("Syntax error: Missing right operand for '" +
                               std::string(op.value) + "' at line " +
                               std::to_string(op.line),
                        SourceLocation{"", op.line, op.col});
    node = arena_.emplace<BinOp>(node, op_str, right);
  }
  return node;
}

Node *Parser::SubParser::parse_power() {
  Node *node = parse_unary();
  while (match(TokenType::TOK_POW)) {
    Token op = previous();
    Node *right = parse_power();
    if (!right)
      throw SyntaxError(
          "Syntax error: Missing right operand for '**' at line " +
          std::to_string(op.line),
          SourceLocation{"", op.line, op.col});
    node = arena_.emplace<BinOp>(node, "**", right);
  }
  return node;
}

Node *Parser::SubParser::parse_unary() {
  if (match({TokenType::TOK_NOT, TokenType::TOK_MINUS_OP,
             TokenType::TOK_BIT_NOT})) {
    Token op = previous();
    Node *operand = parse_unary();
    if (!operand)
      throw SyntaxError("Syntax error: Missing operand for '" +
                               std::string(op.value) + "' at line " +
                               std::to_string(op.line),
                        SourceLocation{"", op.line, op.col});
    std::string_view op_name =
        op.type == TokenType::TOK_NOT
            ? "not"
            : (op.type == TokenType::TOK_BIT_NOT ? "~" : "-");
    return arena_.emplace<UnaryOp>(op_name, operand);
  }
  return parse_call_and_access();
}

Node *Parser::SubParser::parse_call_and_access() {
  Node *node = parse_primary();
  while (true) {
    if (match(TokenType::TOK_LPAREN)) {
      if (PropertyAccess *pa = dynamic_cast<PropertyAccess *>(node)) {
        MethodCall *mc = arena_.emplace<MethodCall>();
        mc->instance_name = pa->instance_name;
        mc->method_name = pa->property_name;
        if (!check(TokenType::TOK_RPAREN)) {
          do {
            mc->args.push_back(parse_expression());
          } while (match(TokenType::TOK_COMMA));
        }
        consume(TokenType::TOK_RPAREN, "Expected ')'");
        node = mc;
      } else {
        Call *call = arena_.emplace<Call>();
        if (VarAccess *v = dynamic_cast<VarAccess *>(node)) {
          call->name = v->name;
        } else {
          call->name = "anonymous_call";
          call->callee = node;
        }
        if (!check(TokenType::TOK_RPAREN)) {
          do {
            if (check(TokenType::TOK_ID) && pos_ + 1 < tokens_.size() &&
                tokens_[pos_ + 1].type == TokenType::TOK_ASSIGN) {
              std::string_view key = consume(TokenType::TOK_ID, "").value;
              consume(TokenType::TOK_ASSIGN, "");
              call->kwargs.push_back({key, parse_expression()});
            } else
              call->args.push_back(parse_expression());
          } while (match(TokenType::TOK_COMMA));
        }
        consume(TokenType::TOK_RPAREN, "Expected ')'");
        node = call;
      }
    } else if (match(TokenType::TOK_DOT)) {
      if (is_at_end())
        throw SyntaxError("Expected property name", {"", 0, 0});
      Token name = tokens_[pos_++];
      if (VarAccess *v = dynamic_cast<VarAccess *>(node))
        node = arena_.emplace<PropertyAccess>(v->name, name.value);
      else
        node = arena_.emplace<PropertyAccess>("", name.value);
    } else if (match(TokenType::TOK_COLON_COLON)) {
      if (is_at_end())
        throw SyntaxError("Expected identifier after '::'", {"", 0, 0});
      Token member = advance();
      if (VarAccess *v = dynamic_cast<VarAccess *>(node)) {
        std::string full_name = std::string(v->name) + "::" + std::string(member.value);
        node = arena_.emplace<VarAccess>(arena_.emplace_string(full_name));
      } else {
        node = arena_.emplace<VarAccess>(member.value);
      }
    } else if (match(TokenType::TOK_LBRACKET)) {
      IndexAccess *idx = arena_.emplace<IndexAccess>();
      idx->obj = node;
      bool is_slice = false;
      size_t saved = pos_;
      int depth = 0;
      while (!is_at_end() &&
             (peek().type != TokenType::TOK_RBRACKET || depth > 0)) {
        if (peek().type == TokenType::TOK_LBRACKET)
          depth++;
        if (peek().type == TokenType::TOK_RBRACKET)
          depth--;
        if ((peek().type == TokenType::TOK_COLON ||
             peek().type == TokenType::TOK_COLON_COLON) && depth == 0) {
          is_slice = true;
          break;
        }
        pos_++;
      }
      pos_ = saved;
      if (is_slice) {
        Node *start = nullptr;
        Node *stop = nullptr;
        Node *step = nullptr;
        if (match(TokenType::TOK_COLON_COLON)) {
          if (!check(TokenType::TOK_RBRACKET))
            step = parse_expression();
        } else {
          if (!check(TokenType::TOK_COLON) && !check(TokenType::TOK_COLON_COLON))
            start = parse_expression();
          if (match(TokenType::TOK_COLON_COLON)) {
            if (!check(TokenType::TOK_RBRACKET))
              step = parse_expression();
          } else {
            consume(TokenType::TOK_COLON, "Expected ':'");
            if (!check(TokenType::TOK_COLON) && !check(TokenType::TOK_RBRACKET))
              stop = parse_expression();
            if (match(TokenType::TOK_COLON))
              step = parse_expression();
          }
        }

        consume(TokenType::TOK_RBRACKET, "Expected ']'");
        node = arena_.emplace<SliceNode>(node, start, stop, step);
      } else {

        size_t to_check = pos_;
        int d = 0;
        bool has_to = false;
        while (to_check < tokens_.size() &&
               tokens_[to_check].type != TokenType::TOK_RBRACKET) {
          if (tokens_[to_check].type == TokenType::TOK_LBRACKET)
            d++;
          if (tokens_[to_check].type == TokenType::TOK_RBRACKET)
            d--;
          if (tokens_[to_check].type == TokenType::TOK_TO && d == 0) {
            has_to = true;
            break;
          }
          to_check++;
        }
        if (has_to) {
          Node *start = parse_expression();
          consume(TokenType::TOK_TO, "Expected 'to'");
          Node *end = parse_expression();
          consume(TokenType::TOK_RBRACKET, "Expected ']'");
          node = arena_.emplace<SliceNode>(node, start, end, nullptr);
        } else {
          idx->index = parse_expression();
          consume(TokenType::TOK_RBRACKET, "Expected ']'");
          node = idx;
        }
      }
    } else
      break;
  }
  return node;
}

Node *Parser::SubParser::parse_primary() {
  if (match(TokenType::TOK_NUMBER)) {
    double val = 0;
    try {
      std::string s(previous().value);
      val = std::stod(s);
    } catch (...) {
      val = 0;
    }
    return arena_.emplace<Number>(val);
  }
  if (match(TokenType::TOK_STRING))
    return arena_.emplace<String>(
        arena_.emplace_string(unescape_string_literal(previous().value)));
  if (match({TokenType::TOK_YES, TokenType::TOK_NO}))
    return arena_.emplace<Boolean>(previous().type == TokenType::TOK_YES);
  if (match(TokenType::TOK_NULL))
    return arena_.emplace<VarAccess>("null");

  if (match({TokenType::TOK_TAKE, TokenType::TOK_LAMBDA}))
    return parse_lambda();
  if (match({TokenType::TOK_MAX, TokenType::TOK_MIN, TokenType::TOK_CLAMPED,
             TokenType::TOK_LERP}))
    return parse_math_intrinsic(previous().type);

  if (match({TokenType::TOK_ASK, TokenType::TOK_INPUT})) {
    Call *call = arena_.emplace<Call>("__shl_ask__");
    if (match(TokenType::TOK_LPAREN)) {
      if (!check(TokenType::TOK_RPAREN)) {
        call->args.push_back(parse_expression());
      }
      consume(TokenType::TOK_RPAREN, "Expected ')'");
    } else if (!is_at_end() && peek().type != TokenType::TOK_NEWLINE &&
               peek().type != TokenType::TOK_EOF_TOK &&
               peek().type != TokenType::TOK_COMMA &&
               peek().type != TokenType::TOK_RPAREN &&
               peek().type != TokenType::TOK_RBRACKET &&
               peek().type != TokenType::TOK_RBRACE) {
      call->args.push_back(parse_equality());
    }
    return call;
  }

  if (match(TokenType::TOK_RANGE)) {
    Call *call = arena_.emplace<Call>("range");
    if (match(TokenType::TOK_LPAREN)) {
      if (!check(TokenType::TOK_RPAREN)) {
        do {
          call->args.push_back(parse_expression());
        } while (match(TokenType::TOK_COMMA));
      }
      consume(TokenType::TOK_RPAREN, "Expected ')'");
    } else {
      call->args.push_back(parse_expression());
      if (!is_at_end() && peek().type != TokenType::TOK_NEWLINE &&
          peek().type != TokenType::TOK_EOF_TOK &&
          peek().type != TokenType::TOK_COMMA &&
          peek().type != TokenType::TOK_RPAREN &&
          peek().type != TokenType::TOK_RBRACKET &&
          peek().type != TokenType::TOK_RBRACE &&
          peek().type != TokenType::TOK_COLON) {
        call->args.push_back(parse_expression());
      }
    }
    return call;
  }

  if (match(TokenType::TOK_SPAWN)) {
    Node *callee = parse_expression();
    return arena_.emplace<Spawn>(callee);
  }
  if (match(TokenType::TOK_AWAIT)) {
    Call *call = arena_.emplace<Call>("task_await");
    call->args.push_back(parse_expression());
    return call;
  }

  if (match(TokenType::TOK_MAKE)) {
    if (!is_at_end() && (peek().type == TokenType::TOK_ID ||
                         peek().type == TokenType::TOK_STRUCTURE)) {
      Token cls_token = advance();
      std::string_view cls_name = cls_token.value;
      Call *call = arena_.emplace<Call>(cls_name);
      if (match(TokenType::TOK_LPAREN)) {
        if (!check(TokenType::TOK_RPAREN)) {
          do {
            call->args.push_back(parse_expression());
          } while (match(TokenType::TOK_COMMA));
        }
        consume(TokenType::TOK_RPAREN, "Expected ')'");
      } else if (!is_at_end() && peek().type != TokenType::TOK_NEWLINE &&
                 peek().type != TokenType::TOK_EOF_TOK &&
                 peek().type != TokenType::TOK_COMMA &&
                 peek().type != TokenType::TOK_RBRACKET &&
                 peek().type != TokenType::TOK_RBRACE) {
        while (!is_at_end() && peek().type != TokenType::TOK_NEWLINE &&
               peek().type != TokenType::TOK_EOF_TOK &&
               peek().type != TokenType::TOK_RBRACKET &&
               peek().type != TokenType::TOK_RBRACE) {
          call->args.push_back(parse_equality());
          match(TokenType::TOK_COMMA);
        }
      }
      return call;
    }
  }

  if (match(TokenType::TOK_DB)) {
    if (match(TokenType::TOK_QUERY)) {
      Node *query = parse_expression();
      return arena_.emplace<DbQueryNode>(query);
    } else if (match(TokenType::TOK_FIND)) {
      Node *conds = parse_expression();
      consume(TokenType::TOK_FROM, "Expected 'from'");
      Node *tbl = (check(TokenType::TOK_ID))
                      ? arena_.emplace<String>(arena_.emplace_string(std::string(advance().value)))
                      : parse_expression();
      return arena_.emplace<DbFindNode>(tbl, conds, true);
    } else if (match(TokenType::TOK_INSERT)) {
      Node *data = parse_expression();
      consume(TokenType::TOK_INTO, "Expected 'into'");
      Node *tbl = (check(TokenType::TOK_ID))
                      ? arena_.emplace<String>(arena_.emplace_string(std::string(advance().value)))
                      : parse_expression();
      return arena_.emplace<DbInsertNode>(tbl, data);
    } else if (match(TokenType::TOK_DELETE)) {
      consume(TokenType::TOK_FROM, "Expected 'from'");
      Node *tbl = (check(TokenType::TOK_ID))
                      ? arena_.emplace<String>(arena_.emplace_string(std::string(advance().value)))
                      : parse_expression();
      Node *conds = nullptr;
      if (match(TokenType::TOK_WHERE)) {
        conds = parse_expression();
      }
      return arena_.emplace<DbDeleteNode>(tbl, conds);
    }
  }

  if (match(TokenType::TOK_FIND)) {
    Node *conds = parse_expression();
    consume(TokenType::TOK_FROM, "Expected 'from'");
    Node *tbl = (check(TokenType::TOK_ID))
                    ? arena_.emplace<String>(arena_.emplace_string(std::string(advance().value)))
                    : parse_expression();
    return arena_.emplace<DbFindNode>(tbl, conds, true);
  }
  if (match(TokenType::TOK_INSERT)) {
    Node *data = parse_expression();
    consume(TokenType::TOK_INTO, "Expected 'into'");
    Node *tbl = (check(TokenType::TOK_ID))
                    ? arena_.emplace<String>(arena_.emplace_string(std::string(advance().value)))
                    : parse_expression();
    return arena_.emplace<DbInsertNode>(tbl, data);
  }
  if (match(TokenType::TOK_DELETE)) {
    consume(TokenType::TOK_FROM, "Expected 'from'");
    Node *tbl = (check(TokenType::TOK_ID))
                    ? arena_.emplace<String>(arena_.emplace_string(std::string(advance().value)))
                    : parse_expression();
    Node *conds = nullptr;
    if (match(TokenType::TOK_WHERE)) {
      conds = parse_expression();
    }
    return arena_.emplace<DbDeleteNode>(tbl, conds);
  }

  if (match({TokenType::TOK_ID,         TokenType::TOK_ERROR,
             TokenType::TOK_EXPECT,     TokenType::TOK_ENSURE,
             TokenType::TOK_TEST,       TokenType::TOK_LOCK,
             TokenType::TOK_SEND,       TokenType::TOK_RECEIVE,
             TokenType::TOK_GATHER,     TokenType::TOK_ADD,
             TokenType::TOK_DB,         TokenType::TOK_MODEL,
             TokenType::TOK_CREATE,
             TokenType::TOK_UPDATE,     TokenType::TOK_WHERE,
             TokenType::TOK_INTO,       TokenType::TOK_FROM,
             TokenType::TOK_COLUMN,     TokenType::TOK_ROW,
             TokenType::TOK_LAYOUT,     TokenType::TOK_WIDGET,
             TokenType::TOK_SERVE,      TokenType::TOK_START,
             TokenType::TOK_LISTEN,     TokenType::TOK_WRITE,
             TokenType::TOK_READ,       TokenType::TOK_APPEND,
             TokenType::TOK_MIDDLEWARE, TokenType::TOK_CONVERT,
             TokenType::TOK_INCREMENT,  TokenType::TOK_DECREMENT,
             TokenType::TOK_ON,         TokenType::TOK_REMOVE,
             TokenType::TOK_EVERY,      TokenType::TOK_AFTER,
             TokenType::TOK_EXECUTE,    TokenType::TOK_BY,              TokenType::TOK_AT,         TokenType::TOK_PORT,
              TokenType::TOK_FILE,       TokenType::TOK_DO,
              TokenType::TOK_CONTAINS,   TokenType::TOK_EMPTY})) {
    std::string_view name = previous().value;
    if (name == "make" || name == "new") {
      if (!is_at_end() && (peek().type == TokenType::TOK_ID ||
                           peek().type == TokenType::TOK_STRUCTURE)) {
        Token cls_token = advance();
        std::string_view cls_name = cls_token.value;
        Call *call = arena_.emplace<Call>(cls_name);
        if (match(TokenType::TOK_LPAREN)) {
          if (!check(TokenType::TOK_RPAREN)) {
            do {
              call->args.push_back(parse_expression());
            } while (match(TokenType::TOK_COMMA));
          }
          consume(TokenType::TOK_RPAREN, "Expected ')'");
        } else if (!is_at_end() && peek().type != TokenType::TOK_NEWLINE &&
                   peek().type != TokenType::TOK_EOF_TOK &&
                   peek().type != TokenType::TOK_COMMA &&
                   peek().type != TokenType::TOK_RBRACKET &&
                   peek().type != TokenType::TOK_RBRACE) {
          while (!is_at_end() && peek().type != TokenType::TOK_NEWLINE &&
                 peek().type != TokenType::TOK_EOF_TOK &&
                 peek().type != TokenType::TOK_RBRACKET &&
                 peek().type != TokenType::TOK_RBRACE) {
            call->args.push_back(parse_equality());
            match(TokenType::TOK_COMMA);
          }
        }
        return call;
      }
    }
    if (name == "a") {
      Node *nat = parse_natural_list_dict();
      if (nat)
        return nat;
    }
    if (match({TokenType::TOK_ALL, TokenType::TOK_ANY})) {
      TokenType coll_type = previous().type;
      if (!is_at_end() && (peek().value == "items" || peek().value == "item")) {
        advance();
      }
      consume(TokenType::TOK_IN, "Expected 'in'");
      Node *list = parse_expression();
      Call *call =
          arena_.emplace<Call>(coll_type == TokenType::TOK_ALL ? "all" : "any");
      call->args = {list};
      return call;
    }
    if (match({TokenType::TOK_FROM, TokenType::TOK_IN}))
      return parse_comprehension(name);

    bool is_builtin =
        (name == "str" || name == "int" || name == "float" || name == "bool" ||
         name == "len" || name == "typeof" || name == "convert" ||
         name == "increment" || name == "decrement");

    if (is_builtin && !is_at_end() && peek().type != TokenType::TOK_LPAREN &&
        peek().type != TokenType::TOK_LBRACKET &&
        peek().type != TokenType::TOK_DOT &&
        (peek().type == TokenType::TOK_ID ||
         peek().type == TokenType::TOK_NUMBER ||
         peek().type == TokenType::TOK_STRING)) {
      return parse_unparenthesized_call(name);
    }
    return arena_.emplace<VarAccess>(name);
  }
  if (match(TokenType::TOK_SUM)) {
    consume(TokenType::TOK_OF, "Expected 'of'");
    Call *call = arena_.emplace<Call>("sum");
    call->args = {parse_expression()};
    return call;
  }

  if (match(TokenType::TOK_LPAREN)) {
    Node *node = parse_expression();
    consume(TokenType::TOK_RPAREN, "Expected ')'");
    return node;
  }

  if (match(TokenType::TOK_LBRACKET)) {
    while (match(TokenType::TOK_NEWLINE)) {
    }
    if (check(TokenType::TOK_RBRACKET)) {
      consume(TokenType::TOK_RBRACKET, "Expected ']'");
      return arena_.emplace<ListVal>();
    }
    Node *first_expr = parse_expression();
    if (match(TokenType::TOK_FOR)) {
      std::string_view var_name =
          consume(TokenType::TOK_ID, "Expected variable name in comprehension")
              .value;
      consume(TokenType::TOK_IN,
              "Expected 'in' after variable name in comprehension");
      Node *iter = parse_expression();
      Node *cond = nullptr;
      if (match({TokenType::TOK_WHEN, TokenType::TOK_IF}))
        cond = parse_expression();
      while (match(TokenType::TOK_NEWLINE)) {
      }
      consume(TokenType::TOK_RBRACKET,
              "Expected ']' at end of list comprehension");
      ListComprehension *lc = arena_.emplace<ListComprehension>();
      lc->expr = first_expr;
      lc->var_name = var_name;
      lc->iterable = iter;
      lc->condition = cond;
      return lc;
    }
    ListVal *list = arena_.emplace<ListVal>();
    list->elements.push_back(first_expr);
    while (!check(TokenType::TOK_RBRACKET) && !is_at_end()) {
      if (match(TokenType::TOK_COMMA) || match(TokenType::TOK_NEWLINE)) {
        while (match(TokenType::TOK_NEWLINE)) {
        }
        if (check(TokenType::TOK_RBRACKET))
          break;
        list->elements.push_back(parse_expression());
      } else {
        break;
      }
    }
    while (match(TokenType::TOK_NEWLINE)) {
    }
    consume(TokenType::TOK_RBRACKET, "Expected ']'");
    return list;
  }

  if (match(TokenType::TOK_LBRACE)) {
    Dictionary *dict = arena_.emplace<Dictionary>();
    while (match(TokenType::TOK_NEWLINE)) {
    }
    if (!check(TokenType::TOK_RBRACE)) {
      do {
        while (match(TokenType::TOK_NEWLINE)) {
        }
        if (check(TokenType::TOK_RBRACE))
          break;
        Node *k = parse_expression();
        consume(TokenType::TOK_COLON, "Expected ':'");
        Node *v = parse_expression();
        dict->pairs.push_back({k, v});
        while (match(TokenType::TOK_NEWLINE)) {
        }
      } while (match(TokenType::TOK_COMMA));
    }
    while (match(TokenType::TOK_NEWLINE)) {
    }
    consume(TokenType::TOK_RBRACE, "Expected '}'");
    return dict;
  }

  return nullptr;
}

Node *Parser::SubParser::parse_lambda() {
  AnonymousFunction *lam = arena_.emplace<AnonymousFunction>();
  if (!check(TokenType::TOK_DO) && !check(TokenType::TOK_ARROW)) {
    do {
      if (is_identifier_like(peek().type)) {
        lam->args.push_back(advance().value);
      } else if (match(TokenType::TOK_COMMA)) {

      } else
        break;
    } while (peek().type != TokenType::TOK_DO &&
             peek().type != TokenType::TOK_ARROW && !is_at_end());
  }
  if (match(TokenType::TOK_ARROW)) {
    lam->body = parse_expression();
    return lam;
  }
  consume(TokenType::TOK_DO, "Expected 'do' or '=>'");
  if (!child_indices_.empty())
    lam->body = parent_.bind_statement_list(child_indices_);
  else
    lam->body = parse_expression();
  return lam;
}

Node *Parser::SubParser::parse_math_intrinsic(TokenType type) {
  if (type == TokenType::TOK_MAX || type == TokenType::TOK_MIN) {
    match(TokenType::TOK_OF);
    Node *left = parse_equality();
    Call *call = arena_.emplace<Call>(type == TokenType::TOK_MAX ? "math_max"
                                                                 : "math_min");
    call->args.push_back(left);
    if (match(TokenType::TOK_AND))
      call->args.push_back(parse_equality());
    return call;
  }
  if (type == TokenType::TOK_CLAMPED) {
    Node *v = parse_equality();
    consume(TokenType::TOK_BETWEEN, "Expected 'between'");
    Node *mn = parse_equality();
    consume(TokenType::TOK_AND, "Expected 'and'");
    Node *mx = parse_equality();
    Call *call = arena_.emplace<Call>("math_clamp");
    call->args = {v, mn, mx};
    return call;
  }
  if (type == TokenType::TOK_LERP) {
    match(TokenType::TOK_FROM);
    Node *a = parse_equality();
    consume(TokenType::TOK_TO, "Expected 'to'");
    Node *b = parse_equality();
    consume(TokenType::TOK_BY, "Expected 'by'");
    Node *t = parse_equality();
    Call *call = arena_.emplace<Call>("math_lerp");
    call->args = {a, b, t};
    return call;
  }
  return nullptr;
}

Node *Parser::SubParser::parse_natural_list_dict() {
  if (match(TokenType::TOK_UNIQUE)) {
    if (match(TokenType::TOK_SET)) {
      ListVal *list = arena_.emplace<ListVal>();
      if (match(TokenType::TOK_OF)) {
        do {
          list->elements.push_back(parse_expression());
        } while (match(TokenType::TOK_COMMA));
      }
      Call *call = arena_.emplace<Call>("set");
      call->args = {list};
      return call;
    }
  }
  if (match(TokenType::TOK_LIST)) {
    ListVal *list = arena_.emplace<ListVal>();
    if (match(TokenType::TOK_OF)) {
      do {
        list->elements.push_back(parse_expression());
      } while (match(TokenType::TOK_COMMA));
    }
    return list;
  }
  if (match(TokenType::TOK_SET)) {
    ListVal *list = arena_.emplace<ListVal>();
    if (match(TokenType::TOK_OF)) {
      do {
        list->elements.push_back(parse_expression());
      } while (match(TokenType::TOK_COMMA));
    }
    Call *call = arena_.emplace<Call>("set");
    call->args = {list};
    return call;
  }
  if (match(TokenType::TOK_ID) && previous().value == "dictionary")
    return arena_.emplace<Dictionary>();
  return nullptr;
}

Node *Parser::SubParser::parse_comprehension(std::string_view var_name) {
  ListComprehension *lc = arena_.emplace<ListComprehension>();
  lc->var_name = var_name;
  lc->expr = arena_.emplace<VarAccess>(var_name);
  Node *start = parse_expression();
  consume(TokenType::TOK_TO, "Expected 'to'");
  Node *end = parse_expression();
  Call *r = arena_.emplace<Call>("range");
  r->args = {start, end};
  lc->iterable = r;
  if (match({TokenType::TOK_WHEN, TokenType::TOK_IF}))
    lc->condition = parse_expression();
  return lc;
}

Node *Parser::SubParser::parse_unparenthesized_call(std::string_view name) {
  Call *call = arena_.emplace<Call>(name);
  while (!is_at_end()) {
    if (check(TokenType::TOK_NEWLINE) || check(TokenType::TOK_DEDENT) ||
        check(TokenType::TOK_EOF_TOK)) {
      break;
    }
    if (check(TokenType::TOK_TO) || check(TokenType::TOK_INTO) ||
        check(TokenType::TOK_FROM) || check(TokenType::TOK_BY) ||
        check(TokenType::TOK_AT) || check(TokenType::TOK_ON) ||
        check(TokenType::TOK_WITH)) {
      advance();
      if (is_at_end() || check(TokenType::TOK_NEWLINE) ||
          check(TokenType::TOK_DEDENT))
        break;
    }
    bool is_kwarg_candidate =
        check(TokenType::TOK_ID) || check(TokenType::TOK_INSERT) ||
        check(TokenType::TOK_INTO) || check(TokenType::TOK_FROM) ||
        check(TokenType::TOK_WHERE) || check(TokenType::TOK_TO) ||
        check(TokenType::TOK_BY) || check(TokenType::TOK_FILE) ||
        check(TokenType::TOK_DO) || check(TokenType::TOK_AT) ||
        check(TokenType::TOK_PORT);

    if (is_kwarg_candidate && pos_ + 1 < tokens_.size() &&
        tokens_[pos_ + 1].type == TokenType::TOK_ASSIGN) {
      std::string kw = std::string(advance().value);
      advance(); // skip '='
      call->kwargs.push_back({kw, parse_call_and_access()});
    } else {
      call->args.push_back(parse_call_and_access());
    }

    // legacy NLP check for `only letters`
    if (!is_at_end() && previous().value == "only" &&
        check(TokenType::TOK_ID) && peek().value == "letters") {
      call->kwargs.push_back({"only", arena_.emplace<String>("letters")});
      advance();
    }
  }
  return call;
}

const Token &Parser::SubParser::peek() const { return tokens_[pos_]; }
const Token &Parser::SubParser::previous() const { return tokens_[pos_ - 1]; }
bool Parser::SubParser::is_at_end() const {
  return pos_ >= tokens_.size() || peek().type == TokenType::TOK_ILLEGAL;
}
bool Parser::SubParser::check(TokenType type) const {
  if (is_at_end())
    return false;
  return peek().type == type;
}
bool Parser::SubParser::match(TokenType type) {
  if (check(type)) {
    pos_++;
    return true;
  }
  return false;
}
bool Parser::SubParser::match(const std::vector<TokenType> &types) {
  for (auto t : types)
    if (match(t))
      return true;
  return false;
}
const Token &Parser::SubParser::consume(TokenType type,
                                        const std::string &msg) {
  if (check(type))
    return tokens_[pos_++];
  throw SyntaxError(msg, {"", 0, 0});
}
const Token &Parser::SubParser::advance() {
  if (!is_at_end())
    pos_++;
  return previous();
}

Node *Parser::parse_expr_recursive(const std::vector<Token> &tokens,
                                   bool is_complex,
                                   const std::vector<int> &child_indices) {
  if (tokens.empty())
    return nullptr;
  SubParser p(tokens, arena_, *this, is_complex, child_indices);
  return p.parse_expression();
}

Node *Parser::parse_expr_recursive(const std::vector<Token> &tokens,
                                   const std::vector<int> &child_indices) {
  return parse_expr_recursive(tokens, true, child_indices);
}


} // namespace shell_lite

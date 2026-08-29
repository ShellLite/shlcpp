#pragma once

#include "parser.hpp"
#include "error/error_context.hpp"
#include <algorithm>
#include <charconv>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace shell_lite {

static inline bool is_identifier_like(TokenType t) {
  return t == TokenType::TOK_ID || t == TokenType::TOK_INTO || t == TokenType::TOK_FROM ||
         t == TokenType::TOK_WHERE || t == TokenType::TOK_TO || t == TokenType::TOK_BY ||
         t == TokenType::TOK_ON || t == TokenType::TOK_AT || t == TokenType::TOK_WITH ||
         t == TokenType::TOK_DO || t == TokenType::TOK_SERVER || t == TokenType::TOK_PORT ||
         t == TokenType::TOK_FILE || t == TokenType::TOK_OF || t == TokenType::TOK_IN ||
         t == TokenType::TOK_AS || t == TokenType::TOK_REMOVE || t == TokenType::TOK_ADD;
}

static inline std::string_view get_compound_op_str(TokenType op) {
  return (op == TokenType::TOK_PLUSEQ)    ? "+"
         : (op == TokenType::TOK_MINUSEQ) ? "-"
         : (op == TokenType::TOK_MULEQ)   ? "*"
         : (op == TokenType::TOK_DIVEQ)   ? "/"
                                          : "%";
}

} // namespace shell_lite

#include "parser/parser_internal.hpp"

namespace shell_lite {


Node *Parser::bind_db(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  if (tokens.size() < 2) return bind_expression_statement(index);

  TokenType sub = tokens[1].type;
  if (sub == TokenType::TOK_INSERT) {
    size_t into_idx = 0;
    for (size_t k = 2; k < tokens.size(); ++k) {
      if (tokens[k].type == TokenType::TOK_INTO) {
        into_idx = k;
        break;
      }
    }
    if (into_idx > 2 && into_idx + 1 < tokens.size()) {
      std::vector<Token> data_toks(tokens.begin() + 2, tokens.begin() + into_idx);
      std::vector<Token> tbl_toks(tokens.begin() + into_idx + 1, tokens.end());
      Node *data_node = parse_expr_recursive(data_toks);
      Node *tbl_node = (tbl_toks.size() == 1 && tbl_toks[0].type == TokenType::TOK_ID)
                           ? arena_.emplace<String>(arena_.emplace_string(std::string(tbl_toks[0].value)))
                           : parse_expr_recursive(tbl_toks);
      DbInsertNode *node = arena_.emplace<DbInsertNode>(tbl_node, data_node);
      set_node_loc(node, index);
      return node;
    }
  } else if (sub == TokenType::TOK_QUERY) {
    if (tokens.size() > 2) {
      std::vector<Token> q_toks(tokens.begin() + 2, tokens.end());
      Node *query_node = parse_expr_recursive(q_toks);
      DbQueryNode *node = arena_.emplace<DbQueryNode>(query_node);
      set_node_loc(node, index);
      return node;
    }
  } else if (sub == TokenType::TOK_FIND) {
    size_t from_idx = 0;
    for (size_t k = 2; k < tokens.size(); ++k) {
      if (tokens[k].type == TokenType::TOK_FROM || tokens[k].type == TokenType::TOK_IN) {
        from_idx = k;
        break;
      }
    }
    if (from_idx > 2 && from_idx + 1 < tokens.size()) {
      std::vector<Token> cond_toks(tokens.begin() + 2, tokens.begin() + from_idx);
      std::vector<Token> tbl_toks(tokens.begin() + from_idx + 1, tokens.end());
      Node *cond_node = parse_expr_recursive(cond_toks);
      Node *tbl_node = (tbl_toks.size() == 1 && tbl_toks[0].type == TokenType::TOK_ID)
                           ? arena_.emplace<String>(arena_.emplace_string(std::string(tbl_toks[0].value)))
                           : parse_expr_recursive(tbl_toks);
      DbFindNode *node = arena_.emplace<DbFindNode>(tbl_node, cond_node, true);
      set_node_loc(node, index);
      return node;
    }
  } else if (sub == TokenType::TOK_DELETE) {
    size_t from_idx = 0;
    size_t where_idx = 0;
    for (size_t k = 2; k < tokens.size(); ++k) {
      if (tokens[k].type == TokenType::TOK_FROM) from_idx = k;
      if (tokens[k].type == TokenType::TOK_WHERE) where_idx = k;
    }
    Node *tbl_node = nullptr;
    Node *cond_node = nullptr;
    if (from_idx > 0) {
      size_t tbl_end = (where_idx > from_idx) ? where_idx : tokens.size();
      std::vector<Token> tbl_toks(tokens.begin() + from_idx + 1, tokens.begin() + tbl_end);
      tbl_node = (tbl_toks.size() == 1 && tbl_toks[0].type == TokenType::TOK_ID)
                     ? arena_.emplace<String>(arena_.emplace_string(std::string(tbl_toks[0].value)))
                     : parse_expr_recursive(tbl_toks);
      if (where_idx > 0 && where_idx + 1 < tokens.size()) {
        std::vector<Token> cond_toks(tokens.begin() + where_idx + 1, tokens.end());
        cond_node = parse_expr_recursive(cond_toks);
      }
      DbDeleteNode *node = arena_.emplace<DbDeleteNode>(tbl_node, cond_node);
      set_node_loc(node, index);
      return node;
    }
  }
  return bind_expression_statement(index);
}

Node *Parser::bind_serve(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  size_t at_idx = 0;
  size_t start_idx = 1;
  while (start_idx < tokens.size() && (tokens[start_idx].type == TokenType::TOK_FILES || tokens[start_idx].type == TokenType::TOK_FROM)) {
    start_idx++;
  }
  for (size_t k = start_idx; k < tokens.size(); ++k) {
    if (tokens[k].type == TokenType::TOK_AT || tokens[k].type == TokenType::TOK_TO) {
      at_idx = k;
      break;
    }
  }
  if (at_idx > start_idx && at_idx + 1 < tokens.size()) {
    std::vector<Token> dir_toks(tokens.begin() + start_idx, tokens.begin() + at_idx);
    std::vector<Token> route_toks(tokens.begin() + at_idx + 1, tokens.end());
    Node *dir_node = parse_expr_recursive(dir_toks);
    Node *route_node = parse_expr_recursive(route_toks);
    WebServeNode *node = arena_.emplace<WebServeNode>(route_node, dir_node);
    set_node_loc(node, index);
    return node;
  }
  return bind_expression_statement(index);
}

Node *Parser::bind_listen(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  size_t start_idx = 1;
  while (start_idx < tokens.size() && (tokens[start_idx].type == TokenType::TOK_ON || tokens[start_idx].type == TokenType::TOK_PORT)) {
    start_idx++;
  }
  if (start_idx < tokens.size()) {
    std::vector<Token> port_toks(tokens.begin() + start_idx, tokens.end());
    Node *port_node = parse_expr_recursive(port_toks);
    WebListenNode *node = arena_.emplace<WebListenNode>(port_node);
    set_node_loc(node, index);
    return node;
  }
  WebListenNode *node = arena_.emplace<WebListenNode>(arena_.emplace<Number>(8080.0));
  set_node_loc(node, index);
  return node;
}

} // namespace shell_lite

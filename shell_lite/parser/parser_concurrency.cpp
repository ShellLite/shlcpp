#include "parser/parser_internal.hpp"

namespace shell_lite {


Node *Parser::bind_parallel(int index) {
  Parallel *node = arena_.emplace<Parallel>();
  node->body = bind_statement_list(flat_nodes_[index].child_indices);
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_lock(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  std::string_view name = (tokens.size() > 1) ? tokens[1].value : "default";
  Call *call = arena_.emplace<Call>("lock_block");
  call->args.push_back(arena_.emplace<String>(std::string(name)));
  AnonymousFunction *block = arena_.emplace<AnonymousFunction>();
  block->body = bind_statement_list(flat_nodes_[index].child_indices);
  call->args.push_back(block);
  set_node_loc(call, index);
  return call;
}

Node *Parser::bind_send(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  Call *call = arena_.emplace<Call>("channel_send");
  size_t to_idx = 0;
  for (size_t k = 0; k < tokens.size(); ++k)
    if (tokens[k].type == TokenType::TOK_TO) {
      to_idx = k;
      break;
    }
  if (to_idx <= 1 || to_idx >= tokens.size() - 1) {
    throw SyntaxError("Syntax error: Invalid 'send' statement syntax at line " +
                             std::to_string(flat_nodes_[index].line) +
                             " (expected: send <value> to <channel>)");
  }
  call->args.push_back(parse_expr_recursive(std::vector<Token>(
      tokens.begin() + to_idx + 1, tokens.end()))); // channel
  call->args.push_back(parse_expr_recursive(std::vector<Token>(
      tokens.begin() + 1, tokens.begin() + to_idx))); // value

  set_node_loc(call, index);
  return call;
}

Node *Parser::bind_receive(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  Call *call = arena_.emplace<Call>("channel_receive");
  size_t from_idx = 0;
  for (size_t k = 0; k < tokens.size(); ++k)
    if (tokens[k].type == TokenType::TOK_FROM) {
      from_idx = k;
      break;
    }
  if (from_idx == 0 || from_idx >= tokens.size() - 1) {
    throw SyntaxError(
        "Syntax error: Missing 'from' in 'receive' at line " +
        std::to_string(flat_nodes_[index].line));
  }
  call->args.push_back(parse_expr_recursive(
      std::vector<Token>(tokens.begin() + from_idx + 1, tokens.end())));

  set_node_loc(call, index);
  return call;
}


Node *Parser::bind_spawn(int index) {
  Spawn *n = arena_.emplace<Spawn>(
      parse_expr_recursive(extract_expr_tokens(flat_nodes_[index].tokens, 1)));
  set_node_loc(n, index);
  return n;
}
Node *Parser::bind_await(int index) {
  Call *n = arena_.emplace<Call>("task_await");
  n->args.push_back(
      parse_expr_recursive(extract_expr_tokens(flat_nodes_[index].tokens, 1)));
  set_node_loc(n, index);
  return n;
}

} // namespace shell_lite

#pragma once
#include "arena.hpp"
#include "ast_nodes.hpp"
#include "gbp_core.hpp"
#include "lexer.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace shell_lite {

class Parser {
public:
  explicit Parser(std::string source);
  std::vector<Node *> parse();
  std::vector<Node *> parse_with_topography(TopographyResult topo);

  void set_parallel(bool enable, size_t num_threads = 0) {
    parallel_enabled_ = enable;
    num_threads_ = num_threads;
  }
  bool is_parallel() const { return parallel_enabled_; }

  const std::vector<SyntaxError>& diagnostics() const { return diagnostics_; }
  bool has_diagnostics() const { return !diagnostics_.empty(); }

private:
  std::string source_code_;
  bool parallel_enabled_ = false;
  size_t num_threads_ = 0;
  Arena arena_;
  std::vector<GeoNode> flat_nodes_;
  std::vector<int> root_node_indices_;
  std::vector<SyntaxError> diagnostics_;

  std::vector<Node *> bind_statement_list(const std::vector<int> &indices);
  Node *bind_node(int index);

  class SubParser {
  public:
    SubParser(const std::vector<Token> &tokens, Arena &arena, Parser &parent,
              bool is_complex = false,
              const std::vector<int> &child_indices = {});
    Node *parse_expression();
    Node *parse_opg();
    Node *parse_statement();

    const Token &peek() const;
    const Token &previous() const;
    bool match(TokenType type);
    bool match(const std::vector<TokenType> &types);
    bool check(TokenType type) const;
    const Token &consume(TokenType type, const std::string &msg);
    const Token &advance();
    bool is_at_end() const;

  private:
    const std::vector<Token> &tokens_;
    const std::vector<int> &child_indices_;
    Arena &arena_;
    Parser &parent_;
    size_t pos_ = 0;
    bool is_complex_ = false;

    Node *parse_assignment();
    Node *parse_logical_or();
    Node *parse_logical_and();
    Node *parse_bitwise_or();
    Node *parse_bitwise_xor();
    Node *parse_bitwise_and();
    Node *parse_equality();
    Node *parse_comparison();
    Node *parse_shift();
    Node *parse_additive();
    Node *parse_multiplicative();
    Node *parse_power();
    Node *parse_unary();
    Node *parse_call_and_access();
    Node *parse_primary();

    Node *parse_natural_list_dict();
    Node *parse_comprehension(std::string_view var_name);
    Node *parse_unparenthesized_call(std::string_view name);
    Node *parse_math_intrinsic(TokenType type);
    Node *parse_lambda();
  };

  Node *parse_expr_recursive(const std::vector<Token> &tokens,
                             bool is_complex = true,
                             const std::vector<int> &child_indices = {});
  Node *parse_expr_recursive(const std::vector<Token> &tokens,
                             const std::vector<int> &child_indices);
  Node *bind_expression_statement(int index);
  Node *bind_call_or_expr(const std::vector<Token> &tokens);

  Node *bind_if(int index);
  Node *bind_unless(int index);
  Node *bind_until(int index);
  Node *bind_while(int index);
  Node *bind_for(int index);
  Node *bind_repeat(int index);
  Node *bind_forever(int index);
  Node *bind_print(int index);
  Node *bind_const(int index);
  Node *bind_return(int index);
  Node *bind_func(int index);
  Node *bind_structure(int index);
  Node *bind_complex_assignment(int index, size_t assign_idx);
  Node *bind_import_enhanced(int index);
  Node *bind_from_import(int index);

  Node *bind_head_dispatcher(int index, TokenType type);

  Node *bind_try(int index);
  Node *bind_match(int index);
  Node *bind_when_clause(int index);
  Node *bind_define(int index);

  Node *bind_spawn(int index);
  Node *bind_await(int index);
  Node *bind_exit(int index);
  Node *bind_stop(int index);
  Node *bind_skip(int index);
  Node *bind_parallel(int index);
  Node *bind_lock(int index);
  Node *bind_send(int index);
  Node *bind_receive(int index);
  Node *bind_throw(int index);
  Node *bind_assertion(int index);
  Node *bind_test_block(int index);
  Node *bind_natural_list(int index);
  Node *bind_natural_set(int index);
  Node *bind_db(int index);
  Node *bind_serve(int index);
  Node *bind_listen(int index);
  Node *bind_nlp_add(int index);
  Node *bind_nlp_remove(int index);
  Node *bind_nlp_timer(int index, bool is_every);
  Node *bind_file_write(int index, bool is_append);
  Node *bind_namespace(int index);
  std::vector<Token> extract_expr_tokens(const std::vector<Token> &tokens,
                                         int start = 0);
  Token get_effective_head(int index);
  void set_node_loc(Node *node, int index);
  void set_node_loc(Node *node, const Token &start, const Token &end);

  int get_precedence(TokenType type);
  std::string_view op_to_str(TokenType type);
};

} // namespace shell_lite

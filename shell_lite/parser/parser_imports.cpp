#include "parser/parser_internal.hpp"

namespace shell_lite {


Node *Parser::bind_namespace(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  std::string_view name = (tokens.size() > 1) ? tokens[1].value : "UnnamedNamespace";
  NamespaceDecl *node = arena_.emplace<NamespaceDecl>();
  node->name = std::string(name);
  node->body = bind_statement_list(flat_nodes_[index].child_indices);
  set_node_loc(node, index);
  return node;
}


Node *Parser::bind_import_enhanced(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  size_t start_k = 1;
  bool is_python = false;
  if (tokens.size() > 1 && tokens[1].value == "python") {
    is_python = true;
    start_k = 2;
  }
  std::string_view mod_name =
      (tokens.size() > start_k) ? tokens[start_k].value : "";
  std::string_view alias = "";
  for (size_t k = start_k + 1; k < tokens.size(); k++) {
    if (tokens[k].type == TokenType::TOK_AS && k + 1 < tokens.size()) {
      alias = tokens[k + 1].value;
      break;
    }
  }
  if (!is_python) {
    if (!alias.empty()) {
      ImportAs *imp = arena_.emplace<ImportAs>();
      imp->path = mod_name;
      imp->alias = alias;
      set_node_loc(imp, index);
      return imp;
    } else {
      Import *imp = arena_.emplace<Import>();
      imp->path = mod_name;
      set_node_loc(imp, index);
      return imp;
    }
  }
  PythonImport *node = arena_.emplace<PythonImport>();
  node->module_name = mod_name;
  if (!alias.empty())
    node->alias = alias;
  set_node_loc(node, index);
  return node;
}

Node *Parser::bind_from_import(int index) {
  const auto &tokens = flat_nodes_[index].tokens;
  FromImport *node = arena_.emplace<FromImport>();
  if (tokens.size() > 1)
    node->module_name = tokens[1].value;
  size_t imp_idx = 0;
  for (size_t k = 0; k < tokens.size(); k++)
    if (tokens[k].type == TokenType::TOK_IMPORT ||
        tokens[k].type == TokenType::TOK_USE)
      imp_idx = k;
  if (imp_idx > 0) {
    for (size_t k = imp_idx + 1; k < tokens.size(); k++) {
      if (tokens[k].type == TokenType::TOK_ID) {
        std::string_view name = tokens[k].value;
        std::optional<std::string_view> alias;
        if (k + 2 < tokens.size() && tokens[k + 1].type == TokenType::TOK_AS) {
          alias = tokens[k + 2].value;
          k += 2;
        }
        node->names.push_back({name, alias});
      }
    }
  }
  set_node_loc(node, index);
  return node;
}

} // namespace shell_lite

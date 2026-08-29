#pragma once
#include "ast_nodes.hpp"
#include "chunk.hpp"
#include <vector>
#include <string>

namespace shell_lite {

struct Local {
    std::string name;
    int depth;
};

struct ObjFunction;

class VM;

class Compiler {
    VM* vm_;
public:
    Compiler(VM* vm);
    struct ObjFunction* compile(const std::string& name, const std::vector<Node*>& nodes);
};

}

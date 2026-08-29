#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <stack>
#include "lexer.hpp"
#include "error/error_context.hpp"

namespace shell_lite {

struct GeoNode {
    int line;
    int indent_level;
    int parent_index = -1;
    std::string_view raw_text;
    std::vector<Token> tokens;
    std::vector<int> child_indices;
    bool is_complex = false;
};

struct TopographyResult {
    std::vector<GeoNode> nodes;
    std::vector<SyntaxError> diagnostics;
};

class GPDAStackManager {
private:
    std::vector<int> stack;
    std::vector<GeoNode>* nodes_ref;

public:
    explicit GPDAStackManager(std::vector<GeoNode>* nodes);
    ~GPDAStackManager();

    GPDAStackManager(const GPDAStackManager&) = delete;
    GPDAStackManager& operator=(const GPDAStackManager&) = delete;
    
    GPDAStackManager(GPDAStackManager&& other) noexcept;
    GPDAStackManager& operator=(GPDAStackManager&& other) noexcept;

    void process_node(int node_index);
    void clear();
};

TopographyResult phase1_topography_scan(std::string_view source);
TopographyResult phase1_topography_scan_parallel(std::string_view source, size_t num_threads = 0);
void phase2_topology_linking(std::vector<GeoNode>& nodes);
bool is_anchor_line(std::string_view source, size_t line_start);

}

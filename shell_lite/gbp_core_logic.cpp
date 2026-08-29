#include "gbp_core.hpp"
#include "error/error_context.hpp"
#include <unordered_set>
#include <algorithm>
#include <thread>
#include <future>

namespace shell_lite {

bool is_anchor_line(std::string_view source, size_t line_start) {
    if (line_start >= source.size()) return false;
    char first_c = source[line_start];
    if (first_c == ' ' || first_c == '\t' || first_c == '\r' || first_c == '\n' || first_c == '#') {
        return false;
    }
    size_t word_end = line_start;
    while (word_end < source.size() && (std::isalnum(static_cast<unsigned char>(source[word_end])) || source[word_end] == '_')) {
        word_end++;
    }
    if (word_end == line_start) return false;
    std::string_view word = source.substr(line_start, word_end - line_start);
    if (!is_hard_anchor_keyword(word)) return false;

    if (word_end < source.size()) {
        char next_c = source[word_end];
        if (!std::isspace(static_cast<unsigned char>(next_c)) && next_c != '(' && next_c != ':' && next_c != '\0') {
            return false;
        }
    }
    return true;
}

TopographyResult phase1_topography_scan(std::string_view source) {
    TopographyResult result;
    result.nodes.reserve(source.size() / 40);

    size_t pos = 0;
    size_t node_start = 0;
    int line_count = 1;
    int start_line = 1;

    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    char in_quote = 0;
    bool escaped = false;
    bool in_comment = false;
    bool in_block_comment = false;

    while (pos <= source.size()) {
        char c = (pos < source.size()) ? source[pos] : '\n';

        if (in_block_comment) {
            if (c == '*' && pos + 1 < source.size() && source[pos + 1] == '/') {
                in_block_comment = false;
                pos += 2;
                continue;
            }
        } else if (in_comment) {
            if (c == '\n') {
                in_comment = false;
            }
        } else if (in_quote) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == in_quote) in_quote = 0;
        } else {
            if (c == '/' && pos + 1 < source.size() && source[pos + 1] == '*') {
                in_block_comment = true;
                pos += 2;
                continue;
            } else if (c == '#') {
                in_comment = true;
            } else if (c == '\\' && ((pos + 1 < source.size() && source[pos + 1] == '\n') || (pos + 2 < source.size() && source[pos + 1] == '\r' && source[pos + 2] == '\n'))) {
                if (source[pos + 1] == '\r') pos += 3;
                else pos += 2;
                line_count++;
                continue;
            } else if (c == '"' || c == '\'') {
                in_quote = c;
            } else if (c == '(') {
                paren_depth++;
            } else if (c == ')') {
                paren_depth = std::max(0, paren_depth - 1);
            } else if (c == '[') {
                bracket_depth++;
            } else if (c == ']') {
                bracket_depth = std::max(0, bracket_depth - 1);
            } else if (c == '{') {
                brace_depth++;
            } else if (c == '}') {
                brace_depth = std::max(0, brace_depth - 1);
            }
        }

        if (c == '\n') {
            bool in_anomaly = (paren_depth > 0 || bracket_depth > 0 || brace_depth > 0 || in_quote != 0);

            if (in_anomaly) {
                size_t next_line_start = pos + 1;
                if (is_anchor_line(source, next_line_start)) {
                    if (in_quote != 0) {
                        std::string sl = extract_source_line(source, start_line);
                        result.diagnostics.push_back(SyntaxError("Unclosed string literal (missing closing " + std::string(1, in_quote) + ")",
                                                                 SourceLocation{"", start_line, 1, sl, "add closing " + std::string(1, in_quote)}));
                    } else if (paren_depth > 0) {
                        std::string sl = extract_source_line(source, start_line);
                        result.diagnostics.push_back(SyntaxError("Unclosed parenthesis '('",
                                                                 SourceLocation{"", start_line, 1, sl, "add closing ')'"}));
                    } else if (bracket_depth > 0) {
                        std::string sl = extract_source_line(source, start_line);
                        result.diagnostics.push_back(SyntaxError("Unclosed bracket '['",
                                                                 SourceLocation{"", start_line, 1, sl, "add closing ']'"}));
                    } else if (brace_depth > 0) {
                        std::string sl = extract_source_line(source, start_line);
                        result.diagnostics.push_back(SyntaxError("Unclosed brace '{'",
                                                                 SourceLocation{"", start_line, 1, sl, "add closing '}'"}));
                    }

                    in_quote = 0;
                    paren_depth = 0;
                    bracket_depth = 0;
                    brace_depth = 0;
                    escaped = false;
                    in_comment = false;
                    in_block_comment = false;

                    node_start = pos + 1;
                    start_line = line_count + 1;
                    line_count++;
                    pos++;
                    continue;
                }
            } else if (!in_block_comment) {
                std::string_view raw_block = source.substr(node_start, pos - node_start);

                size_t first_non_space = raw_block.find_first_not_of(" \t\r\n");
                if (first_non_space != std::string_view::npos && raw_block[first_non_space] != '#') {
                    int indent = 0;
                    for (size_t i = 0; i < first_non_space; ++i) {
                        if (raw_block[i] == '\t') indent += 4;
                        else if (raw_block[i] == ' ') indent += 1;
                        else if (raw_block[i] == '\n' || raw_block[i] == '\r') break;
                    }
                    result.nodes.push_back({start_line, indent, -1, raw_block, {}, {}, false});
                }

                node_start = pos + 1;
                start_line = line_count + 1;
            }
            line_count++;
        }
        pos++;
    }

    if (in_quote != 0) {
        std::string sl = extract_source_line(source, start_line);
        result.diagnostics.push_back(SyntaxError("Unclosed string literal (missing closing quote)",
                                                 SourceLocation{"", start_line, 1, sl, "add closing " + std::string(1, in_quote)}));
    } else if (paren_depth > 0) {
        std::string sl = extract_source_line(source, start_line);
        result.diagnostics.push_back(SyntaxError("Unclosed parenthesis '('",
                                                 SourceLocation{"", start_line, 1, sl, "add closing ')'"}));
    } else if (bracket_depth > 0) {
        std::string sl = extract_source_line(source, start_line);
        result.diagnostics.push_back(SyntaxError("Unclosed bracket '['",
                                                 SourceLocation{"", start_line, 1, sl, "add closing ']'"}));
    } else if (brace_depth > 0) {
        std::string sl = extract_source_line(source, start_line);
        result.diagnostics.push_back(SyntaxError("Unclosed brace '{'",
                                                 SourceLocation{"", start_line, 1, sl, "add closing '}'"}));
    } else if (in_block_comment) {
        std::string sl = extract_source_line(source, start_line);
        result.diagnostics.push_back(SyntaxError("Unclosed block comment '/*'",
                                                 SourceLocation{"", start_line, 1, sl, "add closing '*/'"}));
    }

    return result;
}

struct ChunkScanResult {
    std::vector<GeoNode> nodes;
    std::vector<SyntaxError> diagnostics;
    char trailing_in_quote = 0;
    bool trailing_escaped = false;
    bool trailing_in_comment = false;
    bool trailing_in_block_comment = false;
    int trailing_paren_depth = 0;
    int trailing_bracket_depth = 0;
    int trailing_brace_depth = 0;
    int trailing_start_line = 1;
    bool has_anchor = false;
    int first_anchor_line = 0;
    size_t first_anchor_node_index = 0;
    int line_count = 1;
};

static ChunkScanResult scan_chunk(std::string_view source, size_t chunk_start, size_t chunk_end, int start_line_num) {
    ChunkScanResult result;
    result.nodes.reserve((chunk_end - chunk_start) / 40);

    size_t pos = chunk_start;
    size_t node_start = chunk_start;
    int line_count = start_line_num;
    int current_start_line = start_line_num;

    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    char in_quote = 0;
    bool escaped = false;
    bool in_comment = false;
    bool in_block_comment = false;

    while (pos <= chunk_end) {
        char c = (pos < chunk_end) ? source[pos] : '\n';

        if (in_block_comment) {
            if (c == '*' && pos + 1 < source.size() && source[pos + 1] == '/') {
                in_block_comment = false;
                pos += 2;
                continue;
            }
        } else if (in_comment) {
            if (c == '\n') {
                in_comment = false;
            }
        } else if (in_quote) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == in_quote) in_quote = 0;
        } else {
            if (c == '/' && pos + 1 < source.size() && source[pos + 1] == '*') {
                in_block_comment = true;
                pos += 2;
                continue;
            } else if (c == '#') {
                in_comment = true;
            } else if (c == '\\' && ((pos + 1 < source.size() && source[pos + 1] == '\n') || (pos + 2 < source.size() && source[pos + 1] == '\r' && source[pos + 2] == '\n'))) {
                if (source[pos + 1] == '\r') pos += 3;
                else pos += 2;
                line_count++;
                continue;
            } else if (c == '"' || c == '\'') {
                in_quote = c;
            } else if (c == '(') {
                paren_depth++;
            } else if (c == ')') {
                paren_depth = std::max(0, paren_depth - 1);
            } else if (c == '[') {
                bracket_depth++;
            } else if (c == ']') {
                bracket_depth = std::max(0, bracket_depth - 1);
            } else if (c == '{') {
                brace_depth++;
            } else if (c == '}') {
                brace_depth = std::max(0, brace_depth - 1);
            }
        }

        if (c == '\n') {
            bool in_anomaly = (paren_depth > 0 || bracket_depth > 0 || brace_depth > 0 || in_quote != 0);

            if (in_anomaly) {
                size_t next_line_start = pos + 1;
                if (is_anchor_line(source, next_line_start)) {
                    if (in_quote != 0) {
                        std::string sl = extract_source_line(source, current_start_line);
                        result.diagnostics.push_back(SyntaxError("Unclosed string literal (missing closing " + std::string(1, in_quote) + ")",
                                                                 SourceLocation{"", current_start_line, 1, sl, "add closing " + std::string(1, in_quote)}));
                    } else if (paren_depth > 0) {
                        std::string sl = extract_source_line(source, current_start_line);
                        result.diagnostics.push_back(SyntaxError("Unclosed parenthesis '('",
                                                                 SourceLocation{"", current_start_line, 1, sl, "add closing ')'"}));
                    } else if (bracket_depth > 0) {
                        std::string sl = extract_source_line(source, current_start_line);
                        result.diagnostics.push_back(SyntaxError("Unclosed bracket '['",
                                                                 SourceLocation{"", current_start_line, 1, sl, "add closing ']'"}));
                    } else if (brace_depth > 0) {
                        std::string sl = extract_source_line(source, current_start_line);
                        result.diagnostics.push_back(SyntaxError("Unclosed brace '{'",
                                                                 SourceLocation{"", current_start_line, 1, sl, "add closing '}'"}));
                    }

                    in_quote = 0;
                    paren_depth = 0;
                    bracket_depth = 0;
                    brace_depth = 0;
                    escaped = false;
                    in_comment = false;
                    in_block_comment = false;

                    if (!result.has_anchor) {
                        result.has_anchor = true;
                        result.first_anchor_line = line_count + 1;
                        result.first_anchor_node_index = result.nodes.size();
                    }

                    node_start = pos + 1;
                    current_start_line = line_count + 1;
                    line_count++;
                    pos++;
                    continue;
                }
            } else if (!in_block_comment) {
                std::string_view raw_block = source.substr(node_start, pos - node_start);

                size_t first_non_space = raw_block.find_first_not_of(" \t\r\n");
                if (first_non_space != std::string_view::npos && raw_block[first_non_space] != '#') {
                    int indent = 0;
                    for (size_t i = 0; i < first_non_space; ++i) {
                        if (raw_block[i] == '\t') indent += 4;
                        else if (raw_block[i] == ' ') indent += 1;
                        else if (raw_block[i] == '\n' || raw_block[i] == '\r') break;
                    }
                    if (!result.has_anchor && indent == 0 && is_anchor_line(source, node_start)) {
                        result.has_anchor = true;
                        result.first_anchor_line = current_start_line;
                        result.first_anchor_node_index = result.nodes.size();
                    }
                    result.nodes.push_back({current_start_line, indent, -1, raw_block, {}, {}, false});
                }

                node_start = pos + 1;
                current_start_line = line_count + 1;
            }
            line_count++;
        }
        pos++;
    }

    result.trailing_in_quote = in_quote;
    result.trailing_escaped = escaped;
    result.trailing_in_comment = in_comment;
    result.trailing_in_block_comment = in_block_comment;
    result.trailing_paren_depth = paren_depth;
    result.trailing_bracket_depth = bracket_depth;
    result.trailing_brace_depth = brace_depth;
    result.trailing_start_line = current_start_line;
    result.line_count = line_count;

    return result;
}

TopographyResult phase1_topography_scan_parallel(std::string_view source, size_t num_threads) {
    if (source.empty()) return {};

    size_t hw = std::thread::hardware_concurrency();
    size_t actual_threads = num_threads > 0 ? num_threads : (hw > 0 ? hw : 4);
    if (actual_threads > 8) actual_threads = 8;

    if (source.size() < 16384 || actual_threads <= 1) {
        return phase1_topography_scan(source);
    }

    std::vector<size_t> chunk_starts;
    chunk_starts.push_back(0);
    size_t approx_chunk_size = source.size() / actual_threads;

    for (size_t i = 1; i < actual_threads; ++i) {
        size_t target = i * approx_chunk_size;
        size_t next_nl = source.find('\n', target);
        if (next_nl != std::string_view::npos && next_nl + 1 < source.size()) {
            if (next_nl + 1 > chunk_starts.back()) {
                chunk_starts.push_back(next_nl + 1);
            }
        }
    }
    chunk_starts.push_back(source.size());
    size_t n_chunks = chunk_starts.size() - 1;

    std::vector<int> chunk_start_lines(n_chunks, 1);
    int current_line = 1;
    for (size_t i = 0; i < n_chunks; ++i) {
        chunk_start_lines[i] = current_line;
        size_t start = chunk_starts[i];
        size_t end = chunk_starts[i + 1];
        for (size_t p = start; p < end; ++p) {
            if (source[p] == '\n') current_line++;
        }
    }

    std::vector<std::future<ChunkScanResult>> futures;
    futures.reserve(n_chunks);
    for (size_t i = 0; i < n_chunks; ++i) {
        size_t c_start = chunk_starts[i];
        size_t c_end = chunk_starts[i + 1];
        int s_line = chunk_start_lines[i];
        futures.push_back(std::async(std::launch::async, [source, c_start, c_end, s_line]() {
            return scan_chunk(source, c_start, c_end, s_line);
        }));
    }

    std::vector<ChunkScanResult> chunk_results;
    chunk_results.reserve(n_chunks);
    for (auto& f : futures) {
        chunk_results.push_back(f.get());
    }

    TopographyResult final_result;
    final_result.nodes.reserve(source.size() / 40);

    bool active_anomaly = false;
    char active_quote = 0;
    int active_paren = 0;
    int active_bracket = 0;
    int active_brace = 0;
    bool active_block_comment = false;
    int anomaly_start_line = 1;

    for (size_t i = 0; i < n_chunks; ++i) {
        auto& cr = chunk_results[i];

        if (!active_anomaly) {
            final_result.diagnostics.insert(final_result.diagnostics.end(),
                                            cr.diagnostics.begin(), cr.diagnostics.end());
            final_result.nodes.insert(final_result.nodes.end(),
                                      cr.nodes.begin(), cr.nodes.end());

            if (cr.trailing_in_quote != 0 || cr.trailing_paren_depth > 0 ||
                cr.trailing_bracket_depth > 0 || cr.trailing_brace_depth > 0 ||
                cr.trailing_in_block_comment) {
                active_anomaly = true;
                active_quote = cr.trailing_in_quote;
                active_paren = cr.trailing_paren_depth;
                active_bracket = cr.trailing_bracket_depth;
                active_brace = cr.trailing_brace_depth;
                active_block_comment = cr.trailing_in_block_comment;
                anomaly_start_line = cr.trailing_start_line;
            }
        } else {
            if (cr.has_anchor) {
                if (active_quote != 0) {
                    std::string sl = extract_source_line(source, anomaly_start_line);
                    final_result.diagnostics.push_back(SyntaxError("Unclosed string literal (missing closing " + std::string(1, active_quote) + ")",
                                                                 SourceLocation{"", anomaly_start_line, 1, sl, "add closing " + std::string(1, active_quote)}));
                } else if (active_paren > 0) {
                    std::string sl = extract_source_line(source, anomaly_start_line);
                    final_result.diagnostics.push_back(SyntaxError("Unclosed parenthesis '('",
                                                                 SourceLocation{"", anomaly_start_line, 1, sl, "add closing ')'"}));
                } else if (active_bracket > 0) {
                    std::string sl = extract_source_line(source, anomaly_start_line);
                    final_result.diagnostics.push_back(SyntaxError("Unclosed bracket '['",
                                                                 SourceLocation{"", anomaly_start_line, 1, sl, "add closing ']'"}));
                } else if (active_brace > 0) {
                    std::string sl = extract_source_line(source, anomaly_start_line);
                    final_result.diagnostics.push_back(SyntaxError("Unclosed brace '{'",
                                                                 SourceLocation{"", anomaly_start_line, 1, sl, "add closing '}'"}));
                } else if (active_block_comment) {
                    std::string sl = extract_source_line(source, anomaly_start_line);
                    final_result.diagnostics.push_back(SyntaxError("Unclosed block comment '/*'",
                                                                 SourceLocation{"", anomaly_start_line, 1, sl, "add closing '*/'"}));
                }

                if (cr.first_anchor_node_index < cr.nodes.size()) {
                    final_result.nodes.insert(final_result.nodes.end(),
                                              cr.nodes.begin() + cr.first_anchor_node_index,
                                              cr.nodes.end());
                }
                final_result.diagnostics.insert(final_result.diagnostics.end(),
                                                cr.diagnostics.begin(), cr.diagnostics.end());

                if (cr.trailing_in_quote != 0 || cr.trailing_paren_depth > 0 ||
                    cr.trailing_bracket_depth > 0 || cr.trailing_brace_depth > 0 ||
                    cr.trailing_in_block_comment) {
                    active_anomaly = true;
                    active_quote = cr.trailing_in_quote;
                    active_paren = cr.trailing_paren_depth;
                    active_bracket = cr.trailing_bracket_depth;
                    active_brace = cr.trailing_brace_depth;
                    active_block_comment = cr.trailing_in_block_comment;
                    anomaly_start_line = cr.trailing_start_line;
                } else {
                    active_anomaly = false;
                }
            }
        }
    }

    if (active_anomaly) {
        if (active_quote != 0) {
            std::string sl = extract_source_line(source, anomaly_start_line);
            final_result.diagnostics.push_back(SyntaxError("Unclosed string literal (missing closing quote)",
                                                         SourceLocation{"", anomaly_start_line, 1, sl, "add closing " + std::string(1, active_quote)}));
        } else if (active_paren > 0) {
            std::string sl = extract_source_line(source, anomaly_start_line);
            final_result.diagnostics.push_back(SyntaxError("Unclosed parenthesis '('",
                                                         SourceLocation{"", anomaly_start_line, 1, sl, "add closing ')'"}));
        } else if (active_bracket > 0) {
            std::string sl = extract_source_line(source, anomaly_start_line);
            final_result.diagnostics.push_back(SyntaxError("Unclosed bracket '['",
                                                         SourceLocation{"", anomaly_start_line, 1, sl, "add closing ']'"}));
        } else if (active_brace > 0) {
            std::string sl = extract_source_line(source, anomaly_start_line);
            final_result.diagnostics.push_back(SyntaxError("Unclosed brace '{'",
                                                         SourceLocation{"", anomaly_start_line, 1, sl, "add closing '}'"}));
        } else if (active_block_comment) {
            std::string sl = extract_source_line(source, anomaly_start_line);
            final_result.diagnostics.push_back(SyntaxError("Unclosed block comment '/*'",
                                                         SourceLocation{"", anomaly_start_line, 1, sl, "add closing '*/'"}));
        }
    }

    return final_result;
}


GPDAStackManager::GPDAStackManager(std::vector<GeoNode>* nodes) : nodes_ref(nodes) {
    if (nodes) {
        stack.reserve(nodes->size());
    }
}

GPDAStackManager::~GPDAStackManager() {
    clear();
}

GPDAStackManager::GPDAStackManager(GPDAStackManager&& other) noexcept 
    : stack(std::move(other.stack)), nodes_ref(other.nodes_ref) {
    other.nodes_ref = nullptr;
}

GPDAStackManager& GPDAStackManager::operator=(GPDAStackManager&& other) noexcept {
    if (this != &other) {
        stack = std::move(other.stack);
        nodes_ref = other.nodes_ref;
        other.nodes_ref = nullptr;
    }
    return *this;
}

void GPDAStackManager::process_node(int node_index) {
    if (!nodes_ref) return;
    auto& nodes = *nodes_ref;
    
    int target_indent = nodes[node_index].indent_level;

    while (!stack.empty() && target_indent <= nodes[stack.back()].indent_level) {
        stack.pop_back();
    }

    if (!stack.empty()) {
        int parent_idx = stack.back();
        nodes[node_index].parent_index = parent_idx;
        nodes[parent_idx].child_indices.push_back(node_index);
    } else {
        nodes[node_index].parent_index = -1;
    }
    stack.push_back(node_index);
}

void GPDAStackManager::clear() {
    stack.clear();
    nodes_ref = nullptr;
}

void phase2_topology_linking(std::vector<GeoNode>& nodes) {
    if (nodes.empty()) return;

    GPDAStackManager manager(&nodes);
    for (size_t i = 0; i < nodes.size(); ++i) {
        manager.process_node(static_cast<int>(i));
    }
}

}

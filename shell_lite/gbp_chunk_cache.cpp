#include "gbp_chunk_cache.hpp"
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace shell_lite {

uint64_t TopologicalChunkCache::compute_hash(std::string_view text) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : text) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

size_t TopologicalChunkCache::assembled_node_offset(size_t chunk_idx) const {
    size_t off = 0;
    for (size_t i = 0; i < chunk_idx && i < chunks_.size(); ++i) {
        off += chunks_[i].nodes.size();
    }
    return off;
}

size_t TopologicalChunkCache::assembled_diag_offset(size_t chunk_idx) const {
    size_t off = 0;
    for (size_t i = 0; i < chunk_idx && i < chunks_.size(); ++i) {
        off += chunks_[i].diagnostics.size();
    }
    return off;
}

void TopologicalChunkCache::build_from_source(std::string_view source) {
    chunks_.clear();
    assembled_ = TopographyResult();
    if (source.empty()) return;

    std::vector<size_t> anchor_starts;
    std::vector<int> anchor_lines;
    anchor_starts.push_back(0);
    anchor_lines.push_back(1);

    size_t pos = 0;
    int line_count = 1;
    bool in_block_comment = false;
    bool in_line_comment = false;
    char in_quote = 0;
    bool in_triple_quote = false;
    bool escaped = false;
    while (pos < source.size()) {
        char c = source[pos];
        if (in_block_comment) {
            if (c == '*' && pos + 1 < source.size() && source[pos + 1] == '/') {
                in_block_comment = false;
                pos += 2;
                continue;
            }
            if (c != '\n') {
                pos++;
                continue;
            }
        }
        if (in_line_comment) {
            if (c == '\n') {
                in_line_comment = false;
            } else {
                pos++;
                continue;
            }
        } else if (in_quote != 0) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (in_triple_quote && c == in_quote && pos + 2 < source.size() && source[pos + 1] == in_quote && source[pos + 2] == in_quote) {
                in_quote = 0;
                in_triple_quote = false;
                pos += 2;
            }
            else if (!in_triple_quote && c == in_quote) in_quote = 0;
            if (c != '\n') {
                pos++;
                continue;
            }
        } else {
            if (c == '/' && pos + 1 < source.size() && source[pos + 1] == '*') {
                in_block_comment = true;
                pos += 2;
                continue;
            }
            if (c == '#') {
                in_line_comment = true;
                pos++;
                continue;
            }
            if (c == '"' || c == '\'') {
                in_quote = c;
                if (pos + 2 < source.size() && source[pos + 1] == c && source[pos + 2] == c) {
                    in_triple_quote = true;
                    pos += 2;
                }
                pos++;
                continue;
            }
        }
        if (c == '\n') {
            size_t next_line_start = pos + 1;
            if (!in_block_comment && !in_triple_quote && next_line_start < source.size() && is_anchor_line(source, next_line_start)) {
                anchor_starts.push_back(next_line_start);
                anchor_lines.push_back(line_count + 1);
            }
            line_count++;
        }
        pos++;
    }

    size_t n = anchor_starts.size();
    chunks_.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        chunks_.emplace_back();
        TopologicalChunk& chunk = chunks_.back();
        chunk.id = static_cast<int>(i);
        chunk.start_line = anchor_lines[i];
        chunk.start_byte = anchor_starts[i];
        chunk.end_byte = (i + 1 < n) ? anchor_starts[i + 1] : source.size();

        int end_l = chunk.start_line;
        for (size_t p = chunk.start_byte; p < chunk.end_byte; ++p) {
            if (source[p] == '\n') end_l++;
        }
        chunk.end_line = std::max(chunk.start_line, (chunk.end_byte > chunk.start_byte && source[chunk.end_byte - 1] == '\n') ? end_l - 1 : end_l);

        chunk.source_text = std::string(source.substr(chunk.start_byte, chunk.end_byte - chunk.start_byte));
        chunk.content_hash = compute_hash(chunk.source_text);

        TopographyResult res = phase1_topography_scan(chunk.source_text);
        int line_offset = chunk.start_line - 1;
        for (auto& node : res.nodes) {
            node.line += line_offset;
        }
        for (auto& diag : res.diagnostics) {
            diag.location.line += line_offset;
        }
        chunk.nodes = std::move(res.nodes);
        chunk.diagnostics = std::move(res.diagnostics);
    }

    size_t total_nodes = 0;
    size_t total_diag = 0;
    for (const auto& c : chunks_) {
        total_nodes += c.nodes.size();
        total_diag += c.diagnostics.size();
    }
    assembled_.nodes.reserve(total_nodes);
    assembled_.diagnostics.reserve(total_diag);
    for (const auto& c : chunks_) {
        assembled_.nodes.insert(assembled_.nodes.end(), c.nodes.begin(), c.nodes.end());
        assembled_.diagnostics.insert(assembled_.diagnostics.end(), c.diagnostics.begin(), c.diagnostics.end());
    }
}

int TopologicalChunkCache::find_chunk_index(int line) const {
    if (chunks_.empty() || line < 1) return -1;
    auto it = std::lower_bound(chunks_.begin(), chunks_.end(), line,
                               [](const TopologicalChunk& c, int l) {
                                   return c.end_line < l;
                               });
    if (it != chunks_.end() && line >= it->start_line && line <= it->end_line) {
        return static_cast<int>(it - chunks_.begin());
    }
    return -1;
}

bool TopologicalChunkCache::apply_edit(std::string_view new_source, int edit_start_line, int edit_end_line, int line_delta, TopographyResult& out_result) {
    if (chunks_.empty()) {
        build_from_source(new_source);
        out_result = get_full_result();
        return false;
    }

    int chunk_idx = find_chunk_index(edit_start_line);
    if (chunk_idx < 0) {
        build_from_source(new_source);
        out_result = get_full_result();
        return false;
    }

    if (edit_end_line > chunks_[chunk_idx].end_line) {
        build_from_source(new_source);
        out_result = get_full_result();
        return false;
    }

    int target_start_line = chunks_[chunk_idx].start_line;
    int target_end_line = chunks_[chunk_idx].end_line + line_delta;

    size_t new_start_byte = 0;
    size_t new_end_byte = new_source.size();
    int cur_line = 1;
    size_t p = 0;
    bool found_start = (target_start_line == 1);

    while (p < new_source.size()) {
        if (cur_line == target_start_line && !found_start) {
            new_start_byte = p;
            found_start = true;
        }
        if (cur_line == target_end_line + 1) {
            new_end_byte = p;
            break;
        }
        if (new_source[p] == '\n') {
            cur_line++;
        }
        p++;
    }

    if (chunk_idx > 0 && !is_anchor_line(new_source, new_start_byte)) {
        build_from_source(new_source);
        out_result = get_full_result();
        return false;
    }

    size_t old_chunk_size = chunks_[chunk_idx].end_byte - chunks_[chunk_idx].start_byte;
    std::string_view new_chunk_text = new_source.substr(new_start_byte, new_end_byte - new_start_byte);
    uint64_t new_hash = compute_hash(new_chunk_text);

    size_t node_off = assembled_node_offset(static_cast<size_t>(chunk_idx));
    size_t diag_off = assembled_diag_offset(static_cast<size_t>(chunk_idx));
    size_t old_node_count = chunks_[chunk_idx].nodes.size();
    size_t old_diag_count = chunks_[chunk_idx].diagnostics.size();

    if (new_hash != chunks_[chunk_idx].content_hash) {
        chunks_[chunk_idx].source_text = std::string(new_chunk_text);
        TopographyResult res = phase1_topography_scan(chunks_[chunk_idx].source_text);

        int line_offset = target_start_line - 1;
        for (auto& node : res.nodes) {
            node.line += line_offset;
        }
        for (auto& diag : res.diagnostics) {
            diag.location.line += line_offset;
        }

        chunks_[chunk_idx].nodes = std::move(res.nodes);
        chunks_[chunk_idx].diagnostics = std::move(res.diagnostics);
        chunks_[chunk_idx].content_hash = new_hash;

        auto& a_nodes = assembled_.nodes;
        a_nodes.erase(a_nodes.begin() + node_off, a_nodes.begin() + node_off + old_node_count);
        a_nodes.insert(a_nodes.begin() + node_off,
                       chunks_[chunk_idx].nodes.begin(), chunks_[chunk_idx].nodes.end());
        auto& a_diags = assembled_.diagnostics;
        a_diags.erase(a_diags.begin() + diag_off, a_diags.begin() + diag_off + old_diag_count);
        a_diags.insert(a_diags.begin() + diag_off,
                       chunks_[chunk_idx].diagnostics.begin(), chunks_[chunk_idx].diagnostics.end());
    }

    std::ptrdiff_t byte_delta = static_cast<std::ptrdiff_t>(new_end_byte - new_start_byte) - static_cast<std::ptrdiff_t>(old_chunk_size);

    chunks_[chunk_idx].start_byte = new_start_byte;
    chunks_[chunk_idx].end_byte = new_end_byte;
    chunks_[chunk_idx].end_line = target_end_line;

    for (size_t i = static_cast<size_t>(chunk_idx + 1); i < chunks_.size(); ++i) {
        chunks_[i].start_byte = static_cast<size_t>(static_cast<std::ptrdiff_t>(chunks_[i].start_byte) + byte_delta);
        chunks_[i].end_byte = static_cast<size_t>(static_cast<std::ptrdiff_t>(chunks_[i].end_byte) + byte_delta);
        chunks_[i].start_line += line_delta;
        chunks_[i].end_line += line_delta;
        for (auto& node : chunks_[i].nodes) {
            node.line += line_delta;
        }
        for (auto& diag : chunks_[i].diagnostics) {
            diag.location.line += line_delta;
        }
    }

    if (line_delta != 0) {
        size_t later_node_off = node_off + chunks_[chunk_idx].nodes.size();
        for (size_t i = later_node_off; i < assembled_.nodes.size(); ++i) {
            assembled_.nodes[i].line += line_delta;
        }
        size_t later_diag_off = diag_off + chunks_[chunk_idx].diagnostics.size();
        for (size_t i = later_diag_off; i < assembled_.diagnostics.size(); ++i) {
            assembled_.diagnostics[i].location.line += line_delta;
        }
    }

    out_result = assembled_;
    return true;
}

const TopographyResult& TopologicalChunkCache::get_full_result() const {
    return assembled_;
}

} // namespace shell_lite

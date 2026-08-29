#include "gbp_chunk_cache.hpp"
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

void TopologicalChunkCache::build_from_source(std::string_view source) {
    chunks_.clear();
    if (source.empty()) return;

    std::vector<size_t> anchor_starts;
    std::vector<int> anchor_lines;
    anchor_starts.push_back(0);
    anchor_lines.push_back(1);

    size_t pos = 0;
    int line_count = 1;
    while (pos < source.size()) {
        if (source[pos] == '\n') {
            size_t next_line_start = pos + 1;
            if (next_line_start < source.size() && is_anchor_line(source, next_line_start)) {
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
        TopologicalChunk chunk;
        chunk.id = static_cast<int>(i);
        chunk.start_line = anchor_lines[i];
        chunk.start_byte = anchor_starts[i];
        chunk.end_byte = (i + 1 < n) ? anchor_starts[i + 1] : source.size();

        int end_l = chunk.start_line;
        for (size_t p = chunk.start_byte; p < chunk.end_byte; ++p) {
            if (source[p] == '\n') end_l++;
        }
        chunk.end_line = std::max(chunk.start_line, (chunk.end_byte > chunk.start_byte && source[chunk.end_byte - 1] == '\n') ? end_l - 1 : end_l);

        std::string_view chunk_text = source.substr(chunk.start_byte, chunk.end_byte - chunk.start_byte);
        chunk.content_hash = compute_hash(chunk_text);

        TopographyResult res = phase1_topography_scan(chunk_text);
        int line_offset = chunk.start_line - 1;
        for (auto& node : res.nodes) {
            node.line += line_offset;
        }
        for (auto& diag : res.diagnostics) {
            diag.location.line += line_offset;
        }
        chunk.nodes = std::move(res.nodes);
        chunk.diagnostics = std::move(res.diagnostics);

        chunks_.push_back(std::move(chunk));
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

    std::string_view new_chunk_text = new_source.substr(new_start_byte, new_end_byte - new_start_byte);
    TopographyResult res = phase1_topography_scan(new_chunk_text);

    int line_offset = target_start_line - 1;
    for (auto& node : res.nodes) {
        node.line += line_offset;
    }
    for (auto& diag : res.diagnostics) {
        diag.location.line += line_offset;
    }

    chunks_[chunk_idx].start_byte = new_start_byte;
    chunks_[chunk_idx].end_byte = new_end_byte;
    chunks_[chunk_idx].end_line = target_end_line;
    chunks_[chunk_idx].nodes = std::move(res.nodes);
    chunks_[chunk_idx].diagnostics = std::move(res.diagnostics);
    chunks_[chunk_idx].content_hash = compute_hash(new_chunk_text);

    for (size_t i = static_cast<size_t>(chunk_idx + 1); i < chunks_.size(); ++i) {
        chunks_[i].start_line += line_delta;
        chunks_[i].end_line += line_delta;
        for (auto& node : chunks_[i].nodes) {
            node.line += line_delta;
        }
        for (auto& diag : chunks_[i].diagnostics) {
            diag.location.line += line_delta;
        }
    }

    out_result = get_full_result();
    return true;
}

TopographyResult TopologicalChunkCache::get_full_result() const {
    TopographyResult result;
    size_t total_nodes = 0;
    size_t total_diag = 0;
    for (const auto& c : chunks_) {
        total_nodes += c.nodes.size();
        total_diag += c.diagnostics.size();
    }
    result.nodes.reserve(total_nodes);
    result.diagnostics.reserve(total_diag);

    for (const auto& c : chunks_) {
        result.nodes.insert(result.nodes.end(), c.nodes.begin(), c.nodes.end());
        result.diagnostics.insert(result.diagnostics.end(), c.diagnostics.begin(), c.diagnostics.end());
    }
    return result;
}

} // namespace shell_lite

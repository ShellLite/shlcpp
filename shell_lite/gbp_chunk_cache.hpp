#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <string_view>
#include "gbp_core.hpp"

namespace shell_lite {

struct TopologicalChunk {
    int id = 0;
    int start_line = 1;
    int end_line = 1;
    size_t start_byte = 0;
    size_t end_byte = 0;
    std::string anchor_keyword;
    std::vector<GeoNode> nodes;
    std::vector<SyntaxError> diagnostics;
    uint64_t content_hash = 0;
};

class TopologicalChunkCache {
public:
    TopologicalChunkCache() = default;

    void build_from_source(std::string_view source);
    bool apply_edit(std::string_view new_source, int edit_start_line, int edit_end_line, int line_delta, TopographyResult& out_result);
    TopographyResult get_full_result() const;
    int find_chunk_index(int line) const;

    const std::vector<TopologicalChunk>& get_chunks() const { return chunks_; }
    size_t chunk_count() const { return chunks_.size(); }
    void clear() { chunks_.clear(); }

private:
    std::vector<TopologicalChunk> chunks_;
    static uint64_t compute_hash(std::string_view text);
};

} // namespace shell_lite

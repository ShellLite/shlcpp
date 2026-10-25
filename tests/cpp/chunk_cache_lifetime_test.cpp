#include "gbp_chunk_cache.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace shell_lite;

static void expect(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string line_text(const std::string& source, int line_no) {
    size_t start = 0;
    int cur = 1;
    while (cur < line_no && start < source.size()) {
        size_t nl = source.find('\n', start);
        if (nl == std::string::npos) return "";
        start = nl + 1;
        ++cur;
    }
    size_t nl = source.find('\n', start);
    return source.substr(start, (nl == std::string::npos ? source.size() : nl) - start);
}

static void churn_memory() {
    std::vector<std::string> junk;
    for (int i = 0; i < 512; ++i) {
        junk.emplace_back(256, static_cast<char>('a' + (i % 26)));
    }
    std::string big(65536, 'z');
    junk.push_back(big);
}

static void check_cache(const TopologicalChunkCache& cache, const std::string& full_text) {
    const auto& chunks = cache.get_chunks();
    expect(!chunks.empty(), "cache has chunks");
    expect(chunks.front().start_byte == 0, "first chunk starts at byte 0");
    expect(chunks.back().end_byte == full_text.size(), "last chunk ends at source size");

    size_t total_nodes = 0;
    for (size_t i = 0; i < chunks.size(); ++i) {
        const TopologicalChunk& chunk = chunks[i];
        if (i > 0) {
            expect(chunk.start_byte == chunks[i - 1].end_byte, "chunk byte ranges are contiguous");
            expect(chunk.start_byte > chunks[i - 1].start_byte, "chunk start bytes increase");
        }
        expect(chunk.end_byte > chunk.start_byte, "chunk byte range non-empty");
        expect(chunk.source_text.size() == chunk.end_byte - chunk.start_byte, "owned text length matches byte range");
        expect(chunk.source_text == full_text.substr(chunk.start_byte, chunk.end_byte - chunk.start_byte), "owned text matches current source slice");

        const char* base = chunk.source_text.data();
        const char* limit = base + chunk.source_text.size();
        for (const auto& node : chunk.nodes) {
            ++total_nodes;
            const char* view_begin = node.raw_text.data();
            const char* view_end = view_begin + node.raw_text.size();
            expect(view_begin >= base && view_end <= limit, "node raw_text points into cache-owned chunk text");
            expect(node.raw_text == line_text(full_text, node.line), "node raw_text content matches its line in current source");
        }
    }

    TopographyResult full = cache.get_full_result();
    expect(full.nodes.size() == total_nodes, "assembled result node count matches chunks");
    for (const auto& node : full.nodes) {
        expect(node.raw_text == line_text(full_text, node.line), "assembled node raw_text matches current source");
    }
}

int main() {
    const std::string original =
        "to first()\n"
        "    return 1\n"
        "\n"
        "to second()\n"
        "    return 2\n"
        "\n"
        "to third()\n"
        "    return 3\n";

    TopologicalChunkCache cache;
    {
        std::string temp = original;
        cache.build_from_source(temp);
        temp.assign(8192, 'X');
        temp.clear();
        temp.shrink_to_fit();
    }
    churn_memory();
    expect(cache.chunk_count() == 3, "three chunks built");
    check_cache(cache, original);

    const std::string edited =
        "to first()\n"
        "    return 1\n"
        "\n"
        "to second()\n"
        "    return 22222 + 33333\n"
        "\n"
        "to third()\n"
        "    return 3\n";

    TopographyResult out;
    bool applied = false;
    {
        std::string temp = edited;
        applied = cache.apply_edit(temp, 5, 5, 0, out);
        temp.assign(8192, 'Y');
        temp.clear();
        temp.shrink_to_fit();
    }
    churn_memory();
    expect(applied, "apply_edit took the incremental path");
    check_cache(cache, edited);

    const auto& chunks_after = cache.get_chunks();
    size_t third_start = edited.find("to third()");
    expect(third_start != std::string::npos, "third anchor present in edited source");
    expect(chunks_after[2].start_byte == third_start, "later chunk start_byte shifted by byte delta");
    expect(chunks_after[2].end_byte == edited.size(), "later chunk end_byte shifted by byte delta");
    for (const auto& node : out.nodes) {
        expect(node.raw_text == line_text(edited, node.line), "edit result node raw_text matches edited source");
    }

    const std::string shrunk =
        "to first()\n"
        "    return 1\n"
        "\n"
        "to second()\n"
        "    return 7\n"
        "\n"
        "to third()\n"
        "    return 3\n";

    TopographyResult out2;
    bool applied2 = false;
    {
        std::string temp = shrunk;
        applied2 = cache.apply_edit(temp, 5, 5, 0, out2);
        temp.assign(8192, 'W');
        temp.clear();
        temp.shrink_to_fit();
    }
    churn_memory();
    expect(applied2, "second apply_edit took the incremental path");
    check_cache(cache, shrunk);
    const auto& chunks_final = cache.get_chunks();
    expect(chunks_final[2].start_byte == shrunk.find("to third()"), "later chunk start_byte shifted back on shrink");
    expect(chunks_final[2].end_byte == shrunk.size(), "later chunk end_byte shifted back on shrink");

    std::cout << "chunk_cache_lifetime_test: all checks passed\n";
    return 0;
}

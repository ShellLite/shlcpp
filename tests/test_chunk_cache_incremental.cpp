#include "gbp_chunk_cache.hpp"
#include <iostream>
#include <string>
#include <vector>

using namespace shell_lite;

static int failures = 0;

static void expect(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "PASS " << name << "\n";
    } else {
        std::cout << "FAIL " << name << "\n";
        failures++;
    }
}

static bool results_equal(const TopographyResult& a, const TopographyResult& b) {
    if (a.nodes.size() != b.nodes.size()) return false;
    if (a.diagnostics.size() != b.diagnostics.size()) return false;
    for (size_t i = 0; i < a.nodes.size(); ++i) {
        if (a.nodes[i].line != b.nodes[i].line) return false;
        if (a.nodes[i].indent_level != b.nodes[i].indent_level) return false;
        if (a.nodes[i].raw_text != b.nodes[i].raw_text) return false;
    }
    return true;
}

int main() {
    std::string src_a =
        "to a()\n"
        "    return 1\n"
        "to b()\n"
        "    return 2\n"
        "to c()\n"
        "    return 3\n";

    TopologicalChunkCache cache;
    cache.build_from_source(src_a);
    expect(cache.chunk_count() == 3, "build splits into 3 chunks");
    expect(results_equal(cache.get_full_result(), phase1_topography_scan(src_a)),
           "assembled result equals fresh full scan after build");

    std::vector<const char*> before_ptrs;
    for (const auto& node : cache.get_chunks()[1].nodes) {
        before_ptrs.push_back(node.raw_text.data());
    }

    std::string src_b = src_a;
    TopographyResult out;
    bool ok = cache.apply_edit(src_b, 3, 3, 0, out);
    expect(ok, "no-op edit stays on the incremental path");

    bool ptrs_unchanged = before_ptrs.size() == cache.get_chunks()[1].nodes.size();
    for (size_t i = 0; i < before_ptrs.size() && ptrs_unchanged; ++i) {
        ptrs_unchanged = cache.get_chunks()[1].nodes[i].raw_text.data() == before_ptrs[i];
    }
    expect(ptrs_unchanged, "no-op edit skips rescan and keeps the chunk nodes untouched");
    expect(results_equal(out, phase1_topography_scan(src_b)),
           "assembled result equals fresh full scan after no-op edit");

    std::string src_c =
        "to a()\n"
        "    return 1\n"
        "to b()\n"
        "    return 22\n"
        "to c()\n"
        "    return 3\n";
    ok = cache.apply_edit(src_c, 4, 4, 0, out);
    expect(ok, "content edit stays on the incremental path");
    expect(results_equal(out, phase1_topography_scan(src_c)),
           "assembled result equals fresh full scan after content edit");
    bool saw_new_text = false;
    for (const auto& node : cache.get_chunks()[1].nodes) {
        if (node.raw_text.find("return 22") != std::string_view::npos) saw_new_text = true;
    }
    expect(saw_new_text, "content edit replaces only the edited chunk nodes");

    std::string src_d =
        "to a()\n"
        "    x = 5\n"
        "    return 1\n"
        "to b()\n"
        "    return 22\n"
        "to c()\n"
        "    return 3\n";
    ok = cache.apply_edit(src_d, 2, 2, 1, out);
    expect(ok, "line-delta edit stays on the incremental path");
    expect(results_equal(out, phase1_topography_scan(src_d)),
           "assembled result equals fresh full scan after line-delta edit");
    expect(results_equal(cache.get_full_result(), phase1_topography_scan(src_d)),
           "get_full_result matches fresh full scan after all edits");

    if (failures == 0) {
        std::cout << "ALL PASS\n";
        return 0;
    }
    std::cout << failures << " FAILURES\n";
    return 1;
}

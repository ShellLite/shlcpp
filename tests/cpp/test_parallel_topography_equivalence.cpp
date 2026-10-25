#include "gbp_core.hpp"
#include <cstdint>
#include <iostream>
#include <string>

static int failures = 0;

static void check_case(const std::string& name, const std::string& src) {
    auto seq = shell_lite::phase1_topography_scan(src);
    for (size_t threads : {2, 3, 4, 8}) {
        auto par = shell_lite::phase1_topography_scan_parallel(src, threads);
        bool ok = true;
        if (seq.nodes.size() != par.nodes.size()) ok = false;
        size_t n = std::min(seq.nodes.size(), par.nodes.size());
        for (size_t i = 0; ok && i < n; ++i) {
            if (seq.nodes[i].line != par.nodes[i].line ||
                seq.nodes[i].indent_level != par.nodes[i].indent_level ||
                seq.nodes[i].raw_text != par.nodes[i].raw_text) ok = false;
        }
        if (seq.diagnostics.size() != par.diagnostics.size()) ok = false;
        size_t dn = std::min(seq.diagnostics.size(), par.diagnostics.size());
        for (size_t i = 0; ok && i < dn; ++i) {
            if (std::string(seq.diagnostics[i].what()) != std::string(par.diagnostics[i].what()) ||
                seq.diagnostics[i].location.line != par.diagnostics[i].location.line) ok = false;
        }
        if (!ok) {
            failures++;
            std::cout << "FAIL " << name << " threads=" << threads
                      << " seq(nodes=" << seq.nodes.size() << ",diags=" << seq.diagnostics.size() << ")"
                      << " par(nodes=" << par.nodes.size() << ",diags=" << par.diagnostics.size() << ")\n";
        } else {
            std::cout << "PASS " << name << " threads=" << threads
                      << " nodes=" << seq.nodes.size() << " diags=" << seq.diagnostics.size() << "\n";
        }
    }
}

static std::string funcs(const std::string& prefix, int count) {
    std::string s;
    for (int i = 0; i < count; ++i) {
        s += "to " + prefix + std::to_string(i) + "()\n    return " + std::to_string(i) + "\n";
    }
    return s;
}

static std::string pad_to(std::string s, size_t target) {
    while (s.size() < target) s += "x = 1\n";
    return s;
}

static std::string tails_to(const std::string& base, size_t target) {
    std::string s = base;
    int i = 0;
    while (s.size() < target) {
        s += "to tail" + std::to_string(i) + "()\n    return " + std::to_string(i) + "\n";
        ++i;
    }
    return s;
}

int main() {
    {
        std::string s = pad_to(funcs("pad", 70), 8300);
        std::string content = "    ";
        for (int i = 0; i < 12; ++i) content += "middle content ";
        content += "\n";
        s += "to big()\n    msg = \"start of string\n" + content + content + "    end of string\"\n    say msg\n";
        check_case("crossing_string", tails_to(s, 16800));
    }
    {
        std::string s = pad_to(funcs("pad", 100), 7900);
        s += "to big()\n    result = compute(\n        first_argument,\n        second_argument,\n        third_argument)\n    say result\n";
        check_case("crossing_brackets", tails_to(s, 16900));
    }
    {
        std::string s = pad_to(funcs("pad", 100), 8000);
        s += "/* a long block comment\n   still inside the comment\n   closing here */\nto after()\n    return 1\n";
        check_case("crossing_block_comment", tails_to(s, 17000));
    }
    {
        std::string s = pad_to(funcs("pad", 100), 8000);
        s += "to broken()\n    msg = \"unclosed string here\n    return 1\n\nto next_one()\n    return 42\n";
        check_case("anchor_recovery", tails_to(s, 17100));
    }
    {
        std::string s = pad_to(funcs("pad", 100), 8100);
        s += "/* never closed comment\nto swallowed()\n    return 9\n";
        check_case("unclosed_comment_eof", tails_to(s, 17200));
    }
    {
        std::string s = tails_to(pad_to(funcs("pad", 100), 8200), 16800);
        s += "to tail_open()\n    msg = \"never closed\n";
        check_case("unclosed_string_eof", s);
    }
    {
        std::string s;
        uint64_t state = 42;
        auto next_rand = [&state]() {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<double>((state >> 33) & 0xFFFFFF) / 16777216.0;
        };
        for (int i = 0; i < 900; ++i) {
            double r = next_rand();
            std::string idx = std::to_string(i);
            if (r < 0.55) {
                s += "value" + idx + " = " + idx + " + compute_" + std::to_string(i % 7) + "(x)\n";
            } else if (r < 0.65) {
                s += "text" + idx + " = \"a string with (parens) and [brackets] and \\\"escapes\\\"\"\n";
            } else if (r < 0.72) {
                s += "# a comment line " + idx + " with \"quotes\" and (parens\n";
            } else if (r < 0.79) {
                s += "data" + idx + " = [1, 2,\n    3, 4]\n";
            } else if (r < 0.86) {
                s += "/* block\n   comment */\n";
            } else if (r < 0.93) {
                s += "to fn" + idx + "(a, b)\n    return a + b\n";
            } else {
                s += "\n";
            }
        }
        check_case("mixed_random", s);
    }

    if (failures == 0) {
        std::cout << "ALL PASS\n";
        return 0;
    }
    std::cout << failures << " FAILURES\n";
    return 1;
}

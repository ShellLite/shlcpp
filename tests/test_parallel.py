import os
import sys
import unittest

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

try:
    from .test_runner import run_shl_code
except (ImportError, ValueError):
    from tests.test_runner import run_shl_code

class TestParallelScanner(unittest.TestCase):
    def test_large_program(self):
        lines = []
        lines.append("make global_total = 0")
        for i in range(120):
            lines.append(f"""
to calc_func_{i}(val)
    make temp = val * 2 + {i}
    if temp > 50
        return temp / 2
    else
        return temp + 1

global_total = global_total + calc_func_{i}({i % 10})
""")
        lines.append("say global_total")
        code = "\n".join(lines)

        output = run_shl_code(code).strip()
        self.assertTrue(output.isdigit())
        self.assertGreater(int(output), 0)

    def test_cross_chunk_recovery(self):
        lines = []
        lines.append('broken_str = "unterminated string literal crossing parallel chunk boundaries')
        for i in range(80):
            lines.append(f"# filler line {i} to push byte offset across chunks")

        lines.append("""
to valid_anchored_function(x, y)
    give x * y + 100

say valid_anchored_function(5, 6)
""")
        code = "\n".join(lines)
        output = run_shl_code(code).strip()
        self.assertEqual(output, "130")

    def test_cross_chunk_comment(self):
        lines = []
        lines.append("/*")
        for i in range(100):
            lines.append(f"to false_func_{i}()\n    return {i}")
        lines.append("*/")
        lines.append("""
to real_function()
    give 42

say real_function()
""")
        code = "\n".join(lines)
        output = run_shl_code(code).strip()
        self.assertEqual(output, "42")

    def test_nested_structures(self):
        lines = []
        for i in range(25):
            lines.append(f"""
structure Unit_{i}
    has id = {i}
    has multiplier = {i + 1}
    to compute(base)
        make res = base * self.multiplier
        if res > 100
            return res
        return 100
""")
        lines.append("""
u = new Unit_10(10, 11)
say u.compute(15)
""")
        code = "\n".join(lines)
        output = run_shl_code(code).strip()
        self.assertEqual(output, "165")

    def test_straddling_anomalies(self):
        lines = []
        lines.append('unclosed_one = "first malformed string without terminator')
        for i in range(50):
            lines.append(f"# padding {i}")
        
        lines.append("""
to anchor_one()
    return 10
""")
        lines.append("unclosed_bracket = [1, 2, 3, 4")
        for i in range(50):
            lines.append(f"# padding {i}")
            
        lines.append("""
to anchor_two()
    return 20
""")
        lines.append("say anchor_one() + anchor_two()")
        code = "\n".join(lines)
        output = run_shl_code(code).strip()
        self.assertEqual(output, "30")

    def test_deep_nesting(self):
        lines = ["make total = 0"]
        for d in range(40):
            indent = "    " * d
            lines.append(f"{indent}if 1 == 1")
        lines.append("    " * 40 + "total = 999")
        lines.append("say total")
        code = "\n".join(lines)
        output = run_shl_code(code).strip()
        self.assertEqual(output, "999")

if __name__ == "__main__":
    unittest.main()

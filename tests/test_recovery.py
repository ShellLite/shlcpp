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

class TestAnchorRecovery(unittest.TestCase):
    def test_unclosed_quote(self):
        code = """x = "unterminated string literal without closing quote
to calculate(a, b)
    give a + b

res = calculate(15, 25)
say res
"""
        output = run_shl_code(code).strip()
        self.assertEqual(output, "40")

    def test_unclosed_paren(self):
        code = """val = (100 + 200 + 300
structure Counter
    has count = 42
    to get_count()
        give self.count

c = new Counter(42)
say c.get_count()
"""
        output = run_shl_code(code).strip()
        self.assertEqual(output, "42")

    def test_unclosed_bracket(self):
        code = """items = [1, 2, 3, 4, 5
class Greeter
    has name = "ShellLite"
    to greet()
        give "Hello, " + self.name

g = new Greeter("ShellLite")
say g.greet()
"""
        output = run_shl_code(code).strip()
        self.assertEqual(output, "Hello, ShellLite")

    def test_multiple_anomalies(self):
        code = """s1 = "broken quote 1
to first()
    give 100

s2 = (broken paren 2
to second_fn()
    give 200

total = first() + second_fn()
say total
"""
        output = run_shl_code(code).strip()
        self.assertEqual(output, "300")

    def test_comment_with_declaration(self):
        code = """/* Usage example:
def example_fn()
    return 100
*/
to real_fn()
    give 999

say real_fn()
"""
        output = run_shl_code(code).strip()
        self.assertEqual(output, "999")

    def test_statement_not_anchor(self):
        code = """text = "multi
if condition
say 123
to anchored_fn()
    give 777

say anchored_fn()
"""
        output = run_shl_code(code).strip()
        self.assertEqual(output, "777")

    def test_unanchored_eof(self):
        code = 'x = (10 + 20'
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code(code)
        self.assertIn("Unclosed parenthesis", str(ctx.exception))

    def test_unclosed_quote_def(self):
        code = """str_val = "unterminated string literal before def
def compute_val(a, b)
    give a * b

say compute_val(6, 7)
"""
        output = run_shl_code(code).strip()
        self.assertEqual(output, "42")

    def test_unclosed_paren_class(self):
        code = """expr = (1 + 2 + 3 + 4
class Item
    has value = 100
    to get_val()
        give self.value

item = new Item(100)
say item.get_val()
"""
        output = run_shl_code(code).strip()
        self.assertEqual(output, "100")

    def test_fuzz_anchors(self):
        import random
        random.seed(42)
        
        anomalies = [
            'bad_str = "unclosed string without end\n',
            "bad_s_str = 'unclosed single quote\n",
            'bad_paren = (1 + 2 * (3 + 4\n',
            'bad_bracket = [10, 20, 30, [40, 50\n',
            'bad_brace = { "key": 10, "nested": {\n'
        ]

        for trial in range(15):
            lines = []
            expected_sum = 0
            num_anchors = random.randint(2, 5)
            for i in range(num_anchors):
                lines.append(random.choice(anomalies))
                val = (trial + 1) * 10 + i
                lines.append(f"""to fn_{trial}_{i}()
    give {val}
""")
                expected_sum += val

            call_expr = " + ".join([f"fn_{trial}_{i}()" for i in range(num_anchors)])
            lines.append(f"say {call_expr}\n")
            code = "".join(lines)
            output = run_shl_code(code).strip()
            self.assertEqual(output, str(expected_sum))

if __name__ == "__main__":
    unittest.main()

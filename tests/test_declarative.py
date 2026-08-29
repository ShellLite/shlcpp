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

class TestNativeDeclarative(unittest.TestCase):
    def test_math_typecheck(self):
        code = """
try:
    math_sqrt("not a number")
catch e:
    say e
"""
        output = run_shl_code(code).strip()
        self.assertIn("TypeError: Expected 'Number'", output)
        self.assertIn("got 'String'", output)

    def test_math_arity(self):
        code = """
try:
    math_sqrt(10, 20)
catch e:
    say e
"""
        output = run_shl_code(code).strip()
        self.assertIn("Expected 1 argument", output)
        self.assertIn("got 2", output)

    def test_math_calls(self):
        code = """
say math_sqrt(16)
say math_abs(-42)
say math_max(10, 25)
"""
        output = run_shl_code(code).strip().splitlines()
        self.assertEqual(output[0].strip(), "4")
        self.assertEqual(output[1].strip(), "42")
        self.assertEqual(output[2].strip(), "25")

    def test_archive_arity(self):
        code = """
try:
    std_archive_zip("single_arg_only")
catch e:
    say e
"""
        output = run_shl_code(code).strip()
        self.assertIn("Expected 2 arguments in 'std_archive_zip'", output)

    def test_csv_serialize(self):
        code = """
data = [["a", "b"], [1, 2]]
say std_csv_serialize(data)
"""
        output = run_shl_code(code).strip().splitlines()
        self.assertEqual(output[0].strip(), "a,b")
        self.assertEqual(output[1].strip(), "1,2")

    def test_nlp_add_error(self):
        code = """
try:
    std_nlp_add("not_a_list", "neither")
catch e:
    say e
"""
        output = run_shl_code(code).strip()
        self.assertIn("nlp_add expects a list argument", output)

if __name__ == "__main__":
    unittest.main()

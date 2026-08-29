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

class TestSyntaxAndRuntimeErrors(unittest.TestCase):
    def assert_error(self, code: str, expected_snippet: str):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code(code)
        self.assertIn(expected_snippet, str(ctx.exception))

    def test_unclosed_paren(self):
        self.assert_error("x = (10 + 20", "Unclosed parenthesis")

    def test_unclosed_bracket(self):
        self.assert_error("arr = [1, 2, 3", "Unclosed bracket")

    def test_unclosed_brace(self):
        self.assert_error('obj = {"key": "val"', "Unclosed brace")

    def test_unclosed_str(self):
        self.assert_error('s = "unterminated string', "Unclosed string literal")

    def test_invalid_assign(self):
        self.assert_error("10 = 5", "Invalid assignment target")

    def test_invalid_send(self):
        self.assert_error('send "data"', "Invalid 'send' statement syntax")

    def test_missing_operand(self):
        self.assert_error("x = 10 > ", "Missing right operand")

    def test_undefined_var(self):
        self.assert_error("say nonexistent_variable_123", "Undefined variable:")

    def test_non_callable(self):
        self.assert_error("42()", "Can only call functions and classes")

    def test_unhandled_throw(self):
        self.assert_error('throw "Fatal custom error"', "Fatal custom error")

    def test_primitive_prop(self):
        self.assert_error('x = 42\nx.prop = "fail"', "Only instances have mutable properties")

    def test_missing_import(self):
        self.assert_error("import NonExistentModule12345", "Module not found:")

    def test_error_format(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code('throw "Fatal custom error"')
        err_str = str(ctx.exception)
        self.assertIn("RuntimeError", err_str)
        self.assertIn("Fatal custom error", err_str)
        self.assertIn("File", err_str)
        self.assertNotIn("Compiler Error:", err_str)

if __name__ == "__main__":
    unittest.main()

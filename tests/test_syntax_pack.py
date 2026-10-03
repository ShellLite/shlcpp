import os
import sys
import subprocess
import tempfile
import unittest

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

try:
    from .test_runner import run_shl_code, get_shl_executable
except (ImportError, ValueError):
    from tests.test_runner import run_shl_code, get_shl_executable


class TestBlockComments(unittest.TestCase):
    def test_single_line(self):
        self.assertEqual(run_shl_code("/* hello */ say 42").strip(), "42")

    def test_multiline(self):
        code = "/* line one\nline two */\nsay 7"
        self.assertEqual(run_shl_code(code).strip(), "7")

    def test_inline(self):
        self.assertEqual(run_shl_code("say /* mid */ 1").strip(), "1")

    def test_non_nesting(self):
        self.assertEqual(run_shl_code("/* /* */ say 3").strip(), "3")

    def test_does_not_eat_division(self):
        self.assertEqual(run_shl_code("say 10 / 2").strip(), "5")

    def test_hash_comment_still_works(self):
        self.assertEqual(run_shl_code("# hi\nsay 9").strip(), "9")

    def test_unterminated(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("say 1\n/* oops")
        self.assertIn("Unclosed block comment", str(ctx.exception))

    def test_unterminated_only_input(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("/* oops")
        self.assertIn("Unclosed block comment", str(ctx.exception))


class TestPass(unittest.TestCase):
    def test_pass_alone(self):
        self.assertEqual(run_shl_code("pass").strip(), "")

    def test_pass_in_if(self):
        code = "if true:\n    pass\nsay 1"
        self.assertEqual(run_shl_code(code).strip(), "1")

    def test_pass_in_function(self):
        code = "to f\n    pass\nf()\nsay 2"
        self.assertEqual(run_shl_code(code).strip(), "2")

    def test_pass_in_while(self):
        code = "x = 0\nwhile x < 3:\n    x = x + 1\n    pass\nsay x"
        self.assertEqual(run_shl_code(code).strip(), "3")


class TestRadixLiterals(unittest.TestCase):
    def test_hex(self):
        self.assertEqual(run_shl_code("say 0xFF").strip(), "255")

    def test_hex_upper_prefix(self):
        self.assertEqual(run_shl_code("say 0Xff").strip(), "255")

    def test_binary(self):
        self.assertEqual(run_shl_code("say 0b1010").strip(), "10")

    def test_binary_upper_prefix(self):
        self.assertEqual(run_shl_code("say 0B101").strip(), "5")

    def test_octal(self):
        self.assertEqual(run_shl_code("say 0o755").strip(), "493")

    def test_octal_upper_prefix(self):
        self.assertEqual(run_shl_code("say 0O17").strip(), "15")

    def test_mixed_arithmetic(self):
        self.assertEqual(run_shl_code("say 0x10 + 0b10").strip(), "18")

    def test_zero(self):
        self.assertEqual(run_shl_code("say 0x0").strip(), "0")

    def test_invalid_hex(self):
        with self.assertRaises(RuntimeError):
            run_shl_code("say 0xGG")

    def test_invalid_binary(self):
        with self.assertRaises(RuntimeError):
            run_shl_code("say 0b12")

    def test_invalid_octal(self):
        with self.assertRaises(RuntimeError):
            run_shl_code("say 0o89")

    def test_bare_prefix(self):
        with self.assertRaises(RuntimeError):
            run_shl_code("say 0x")


class TestFloorDivision(unittest.TestCase):
    def test_basic(self):
        self.assertEqual(run_shl_code("say 7 // 2").strip(), "3")

    def test_exact(self):
        self.assertEqual(run_shl_code("say 8 // 2").strip(), "4")

    def test_negative_floors(self):
        self.assertEqual(run_shl_code("say -7 // 2").strip(), "-4")

    def test_negative_divisor(self):
        self.assertEqual(run_shl_code("say 7 // -2").strip(), "-4")

    def test_float(self):
        self.assertEqual(run_shl_code("say 7.0 // 2").strip(), "3")

    def test_precedence(self):
        self.assertEqual(run_shl_code("say 2 + 6 // 4").strip(), "3")

    def test_parens(self):
        self.assertEqual(run_shl_code("say (7 + 1) // 4").strip(), "2")

    def test_div_by_zero(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("say 1 // 0")
        self.assertIn("Division by zero", str(ctx.exception))

    def test_plain_division_unaffected(self):
        self.assertEqual(run_shl_code("say 7 / 2").strip(), "3.5")


class TestBitwiseCompoundAssign(unittest.TestCase):
    def test_and_eq(self):
        self.assertEqual(run_shl_code("x = 12\nx &= 10\nsay x").strip(), "8")

    def test_or_eq(self):
        self.assertEqual(run_shl_code("x = 12\nx |= 10\nsay x").strip(), "14")

    def test_xor_eq(self):
        self.assertEqual(run_shl_code("x = 12\nx ^= 10\nsay x").strip(), "6")

    def test_on_index_target(self):
        code = 'd = {"a": 12}\nd["a"] &= 10\nsay d["a"]'
        self.assertEqual(run_shl_code(code).strip(), "8")

    def test_chained_with_arithmetic(self):
        code = "x = 7\nx |= 8\nx &= 15\nsay x"
        self.assertEqual(run_shl_code(code).strip(), "15")


class TestEsay(unittest.TestCase):
    def run_esay(self, code):
        exe = get_shl_executable()
        with tempfile.NamedTemporaryFile(mode="w", suffix=".shl",
                                         delete=False, encoding="utf-8") as f:
            f.write(code)
            path = f.name
        try:
            result = subprocess.run([exe, path], capture_output=True, text=True,
                                    encoding="utf-8")
        finally:
            os.remove(path)
        return result

    def test_goes_to_stderr(self):
        result = self.run_esay('esay "hello"')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr.strip(), "hello")
        self.assertEqual(result.stdout.strip(), "")

    def test_say_still_stdout(self):
        result = self.run_esay('say "hi"')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.strip(), "hi")
        self.assertEqual(result.stderr.strip(), "")

    def test_esay_expression(self):
        result = self.run_esay("x = 40 + 2\nesay x")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr.strip(), "42")


if __name__ == "__main__":
    unittest.main()

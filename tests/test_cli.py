import unittest
import subprocess
import tempfile
import os
import sys
import json

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

try:
    from .test_runner import get_shl_executable
except (ImportError, ValueError):
    from tests.test_runner import get_shl_executable

class TestCliTooling(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = get_shl_executable()

    def run_cmd(self, args: list) -> subprocess.CompletedProcess:
        cmd = [f'"{self.exe}"'] + [f'"{a}"' if ' ' in a else a for a in args]
        res = subprocess.run(
            " ".join(cmd),
            capture_output=True,
            text=True,
            encoding="utf-8",
            shell=True
        )
        if res.returncode == 4551:
            raise unittest.SkipTest("Windows Smart App Control policy blocked binary execution (WinError 4551)")
        return res

    def test_version(self):
        res = self.run_cmd(["--version"])
        self.assertEqual(res.returncode, 0)
        self.assertIn("shlcpp v0.1.0", res.stdout)

    def test_help(self):
        res = self.run_cmd(["--help"])
        self.assertEqual(res.returncode, 0)
        self.assertIn("Usage: shlcpp", res.stdout)
        self.assertIn("check", res.stdout)
        self.assertIn("ast", res.stdout)

    def test_check_valid(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".shl", delete=False, encoding="utf-8") as f:
            f.write("to add(a, b):\n    give a + b\n\nsay add(10, 20)\n")
            f_path = f.name
        try:
            res = self.run_cmd(["check", f_path])
            self.assertEqual(res.returncode, 0)
            self.assertIn("Syntax OK:", res.stdout)
        finally:
            if os.path.exists(f_path):
                os.remove(f_path)

    def test_check_syntax_error(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".shl", delete=False, encoding="utf-8") as f:
            f.write("x = 10 > \n")
            f_path = f.name
        try:
            res = self.run_cmd(["check", f_path])
            self.assertEqual(res.returncode, 2)
            self.assertIn("SyntaxError", res.stderr)
            self.assertIn("^", res.stderr)
        finally:
            if os.path.exists(f_path):
                os.remove(f_path)

    def test_check_missing_file(self):
        res = self.run_cmd(["check", "non_existent_file_xyz_123.shl"])
        self.assertEqual(res.returncode, 3)
        self.assertIn("Could not open file", res.stderr)

    def test_ast(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".shl", delete=False, encoding="utf-8") as f:
            f.write('say "hello"\n')
            f_path = f.name
        try:
            res = self.run_cmd(["ast", f_path])
            self.assertEqual(res.returncode, 0)
            ast_data = json.loads(res.stdout)
            self.assertIsInstance(ast_data, list)
            self.assertEqual(ast_data[0]["type"], "Print")
        finally:
            if os.path.exists(f_path):
                os.remove(f_path)

    def test_eval(self):
        res = self.run_cmd(["-e", "say 123 + 456"])
        self.assertEqual(res.returncode, 0)
        self.assertIn("579", res.stdout)

    def test_compile_bytecode(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".shl", delete=False, encoding="utf-8") as f_shl:
            f_shl.write("a = 100\nb = 250\nsay a + b\n")
            shl_path = f_shl.name
        shbc_path = shl_path.replace(".shl", ".shbc")

        try:
            res_comp = self.run_cmd(["-c", shl_path, shbc_path])
            self.assertEqual(res_comp.returncode, 0)
            self.assertIn("Compiled", res_comp.stdout)
            self.assertTrue(os.path.exists(shbc_path))

            res_run = self.run_cmd([shbc_path])
            self.assertEqual(res_run.returncode, 0)
            self.assertEqual(res_run.stdout.strip(), "350")
        finally:
            if os.path.exists(shl_path):
                os.remove(shl_path)
            if os.path.exists(shbc_path):
                os.remove(shbc_path)


if __name__ == "__main__":
    unittest.main()

import unittest
import subprocess
import tempfile
import os
import sys

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

try:
    from .test_runner import get_shl_executable
except (ImportError, ValueError):
    from tests.test_runner import get_shl_executable


class TestFmt(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = get_shl_executable()

    def run_fmt(self, args, stdin_data=None):
        cmd = [self.exe, "fmt"] + args
        try:
            res = subprocess.run(
                cmd,
                input=stdin_data,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
        except OSError as e:
            if getattr(e, "winerror", None) == 4551:
                raise unittest.SkipTest("Windows Smart App Control blocked binary")
            raise
        if res.returncode == 4551:
            raise unittest.SkipTest("Windows Smart App Control blocked binary")
        return res

    def fmt_stdin(self, source, extra=None):
        args = ["--stdin"] + (extra or [])
        res = self.run_fmt(args, stdin_data=source)
        self.assertEqual(res.returncode, 0, f"stderr: {res.stderr}")
        return res.stdout

    def test_basic_canonical(self):
        out = self.fmt_stdin("x=5+3\n")
        self.assertEqual(out, "x = 5 + 3\n")

    def test_string_quotes(self):
        out = self.fmt_stdin("s='hi'\n")
        self.assertEqual(out, 's = "hi"\n')

    def test_indentation(self):
        src = "to f()\n    x=1\n    say x\n"
        out = self.fmt_stdin(src)
        self.assertEqual(out, "to f()\n    x = 1\n    say x\n")

    def test_empty_block_pass(self):
        src = "to f()\n"
        out = self.fmt_stdin(src)
        self.assertIn("pass", out)

    def test_comments_preserved(self):
        src = "# header\nx=1  # trailing\n"
        out = self.fmt_stdin(src)
        self.assertIn("# header", out)
        self.assertIn("# trailing", out)
        self.assertIn("x = 1", out)

    def test_noise_preserved(self):
        src = "let x = 5\n"
        out = self.fmt_stdin(src)
        self.assertIn("let", out)
        self.assertIn("x = 5", out)

    def test_radix_preserved(self):
        out = self.fmt_stdin("x=0xFF\n")
        self.assertIn("0xFF", out)

    def test_in_place_default(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.shl")
            with open(p, "w") as f:
                f.write("x=1\n")
            res = self.run_fmt([p])
            self.assertEqual(res.returncode, 0)
            with open(p) as f:
                self.assertEqual(f.read(), "x = 1\n")

    def test_check_clean(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.shl")
            with open(p, "w") as f:
                f.write("x = 1\n")
            res = self.run_fmt(["--check", p])
            self.assertEqual(res.returncode, 0)

    def test_check_dirty(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.shl")
            with open(p, "w") as f:
                f.write("x=1\n")
            res = self.run_fmt(["--check", p])
            self.assertEqual(res.returncode, 1)

    def test_diff(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.shl")
            with open(p, "w") as f:
                f.write("x=1\n")
            res = self.run_fmt(["--diff", p])
            self.assertEqual(res.returncode, 0)
            self.assertIn("-", res.stdout)
            self.assertIn("+", res.stdout)
            # file not modified
            with open(p) as f:
                self.assertEqual(f.read(), "x=1\n")

    def test_stdin(self):
        out = self.fmt_stdin("y=2*3\n")
        self.assertEqual(out, "y = 2 * 3\n")

    def test_symlink_refused(self):
        if os.name == "nt":
            self.skipTest("symlink test not for windows")
        with tempfile.TemporaryDirectory() as d:
            real = os.path.join(d, "real.shl")
            link = os.path.join(d, "link.shl")
            with open(real, "w") as f:
                f.write("x=1\n")
            os.symlink(real, link)
            res = self.run_fmt([link])
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("symlink", res.stderr.lower())

    def test_range(self):
        src = "a=1\nb=2\nc=3\n"
        out = self.fmt_stdin(src, ["--range=2:2"])
        self.assertEqual(out, "a=1\nb = 2\nc=3\n")

    def test_width_break(self):
        src = 'x = f("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "b")\n'
        out = self.fmt_stdin(src)
        self.assertIn("(\n", out)
        self.assertIn(",\n", out)

    def test_idempotent(self):
        src = "to f(a,b=10)\n    if a>1\n        say a\n    else\n        say b\nx=[1,2,3]\n"
        once = self.fmt_stdin(src)
        twice = self.fmt_stdin(once)
        self.assertEqual(once, twice)

    def test_examples_corpus(self):
        exdir = os.path.join(_PROJECT_ROOT, "examples")
        if not os.path.isdir(exdir):
            self.skipTest("no examples dir")
        count = 0
        for fn in sorted(os.listdir(exdir)):
            if not fn.endswith(".shl"):
                continue
            p = os.path.join(exdir, fn)
            with open(p, encoding="utf-8") as f:
                src = f.read()
            res = self.run_fmt(["--stdin"], stdin_data=src)
            if res.returncode != 0:
                continue  # skip files with syntax errors
            once = res.stdout
            twice = self.fmt_stdin(once)
            self.assertEqual(once, twice, f"not idempotent: {fn}")
            count += 1
        self.assertGreater(count, 0, "no example files tested")


    def run_shl(self, source):
        with tempfile.NamedTemporaryFile(
            "w", suffix=".shl", delete=False, encoding="utf-8"
        ) as f:
            f.write(source)
            path = f.name
        try:
            res = subprocess.run(
                [self.exe, path],
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            return res
        finally:
            os.unlink(path)

    def test_try_always_no_phantom_catch(self):
        # B1 formatter must not invent catch on try always without catch
        src = 'try\n    throw "boom"\nalways\n    say "cleanup"\n'
        out = self.fmt_stdin(src)
        self.assertNotIn("catch", out)
        before = self.run_shl(src)
        after = self.run_shl(out)
        self.assertEqual(before.returncode, after.returncode)
        self.assertEqual(before.stdout, after.stdout)

    def test_try_no_phantom_catch(self):
        src = 'try\n    say "ok"\n'
        out = self.fmt_stdin(src)
        self.assertNotIn("catch", out)

    def test_try_catch_kept(self):
        src = 'try\n    throw "x"\ncatch e\n    say e\n'
        out = self.fmt_stdin(src)
        self.assertIn("catch e", out)

    def test_regex_hash_not_comment(self):
        # B2 # inside a regex literal is not a comment
        src = 'r = /a#b/\nsay r\n'
        once = self.fmt_stdin(src)
        self.assertEqual(once, src)
        twice = self.fmt_stdin(once)
        self.assertEqual(once, twice)

    def test_regex_noise_word_no_blank_growth(self):
        # M2 noise word inside regex must not force verbatim blank lines
        src = "r = /the/\nsay 1\n"
        once = self.fmt_stdin(src)
        twice = self.fmt_stdin(once)
        self.assertEqual(once, twice)
        self.assertNotIn("\n\n", once)

    def test_block_trailing_comment_not_stolen(self):
        # M1 trailing comment of the next statement stays on its line
        src = "if 1\n    say 1\nx = 2  # trailing\n"
        out = self.fmt_stdin(src)
        self.assertIn("x = 2  # trailing", out)
        twice = self.fmt_stdin(out)
        self.assertEqual(out, twice)

    def test_block_interior_comment_no_phantom_blank(self):
        src = "if 1\n    say 1\n    # note\nx = 2\n"
        out = self.fmt_stdin(src)
        self.assertIn("    # note", out)
        twice = self.fmt_stdin(out)
        self.assertEqual(out, twice)

    def test_block_comment_only_file(self):
        # M3 file with only a block comment is valid input
        res = self.run_fmt(["--stdin"], stdin_data="/* only */\n")
        self.assertEqual(res.returncode, 0, f"stderr: {res.stderr}")

    def test_empty_body_comment_stays_inside(self):
        # m1 interior comment of an empty body is not dedented out
        src = "to f()\n    # only a comment\n"
        out = self.fmt_stdin(src)
        self.assertIn("    # only a comment", out)

    def test_trailing_block_comment_stays_trailing(self):
        # m2 trailing block comment keeps its line like hash does
        src = "x = 1 /* keep */\n"
        out = self.fmt_stdin(src)
        self.assertIn("x = 1  /* keep */", out)

    def test_empty_class_gets_pass(self):
        # m3 same explicit empty rule as empty functions
        out = self.fmt_stdin("class Foo\n")
        self.assertIn("pass", out)

    def test_lambda_expr_roundtrip(self):
        # R1 lambda keeps its syntax no function keyword invented
        src = "f = lambda x => x + 1\n"
        out = self.fmt_stdin(src)
        self.assertEqual(out, src)
        self.assertEqual(self.fmt_stdin(out), out)
        self.assertNotIn("function(", out)
        self.assertEqual(self.run_shl(src + "say f(41)\n").stdout, "42\n")
        self.assertEqual(self.run_shl(out + "say f(41)\n").stdout, "42\n")

    def test_lambda_block_roundtrip(self):
        src = "f = lambda x do\n    give x + 1\n"
        out = self.fmt_stdin(src)
        self.assertEqual(out, src)
        self.assertEqual(self.fmt_stdin(out), out)
        self.assertEqual(self.run_shl(src + "say f(41)\n").stdout, "42\n")

    def test_lambda_multiarg(self):
        src = "f = lambda x, y => x + y\n"
        out = self.fmt_stdin(src)
        self.assertEqual(out, src)
        self.assertEqual(self.fmt_stdin(out), out)

    def test_when_roundtrip(self):
        # R2 when is otherwise not match case else
        src = "x = 1\nwhen x\n    is 1\n        say 10\n    otherwise\n        say 20\n"
        out = self.fmt_stdin(src)
        self.assertEqual(out, src)
        self.assertEqual(self.fmt_stdin(out), out)
        self.assertNotIn("match", out)
        self.assertNotIn("case", out)
        self.assertEqual(self.run_shl(src).stdout, "10\n")
        self.assertEqual(self.run_shl(out).stdout, "10\n")

    def test_when_otherwise_branch_runs(self):
        src = "x = 2\nwhen x\n    is 1\n        say 10\n    otherwise\n        say 20\n"
        out = self.fmt_stdin(src)
        self.assertEqual(self.run_shl(out).stdout, "20\n")

    def test_multiline_list_rhs_no_bogus_blank(self):
        # M2 multiline rhs must not inject blank lines after itself
        src = "xs = [\n    1,\n    2,\n]\nsay 1\n"
        out = self.fmt_stdin(src)
        self.assertEqual(out, "xs = [1, 2]\nsay 1\n")
        self.assertEqual(self.fmt_stdin(out), out)

    def test_lambda_block_rhs_no_bogus_blank(self):
        src = "f = lambda x do\n    say x\nsay 1\n"
        out = self.fmt_stdin(src)
        self.assertNotIn("\n\n", out)
        self.assertEqual(self.fmt_stdin(out), out)

    def test_trailing_comment_on_bracket_line(self):
        src = "xs = [1,\n    2]  # trailing\nsay 1\n"
        out = self.fmt_stdin(src)
        self.assertIn("xs = [1, 2]  # trailing\n", out)
        self.assertEqual(self.fmt_stdin(out), out)

    def test_range_partial_rhs_left_verbatim(self):
        # range covering only the first line of a multiline rhs leaves it alone
        src = "xs = [\n    1,\n    2,\n]\nsay 1\n"
        out = self.fmt_stdin(src, extra=["--range=1:1"])
        self.assertEqual(out, src)

    def test_range_full_rhs_formats(self):
        src = "xs = [\n    1,\n    2,\n]\nsay 1\n"
        out = self.fmt_stdin(src, extra=["--range=1:4"])
        self.assertEqual(out, "xs = [1, 2]\nsay 1\n")

    def test_check_crlf_clean(self):
        # M1 crlf file that is already canonical must pass check
        with tempfile.NamedTemporaryFile(
            "wb", suffix=".shl", delete=False
        ) as f:
            f.write(b"x = 1\r\nsay x\r\n")
            path = f.name
        try:
            res = self.run_fmt(["--check", path])
            self.assertEqual(res.returncode, 0, f"stderr: {res.stderr}")
        finally:
            os.unlink(path)

    def test_inplace_crlf_untouched(self):
        # M1 in place fmt must not rewrite line endings when code is clean
        with tempfile.NamedTemporaryFile(
            "wb", suffix=".shl", delete=False
        ) as f:
            f.write(b"x = 1\r\nsay x\r\n")
            path = f.name
        try:
            res = self.run_fmt([path])
            self.assertEqual(res.returncode, 0, f"stderr: {res.stderr}")
            with open(path, "rb") as f:
                self.assertEqual(f.read(), b"x = 1\r\nsay x\r\n")
        finally:
            os.unlink(path)

    def _write_ec_test(self, d, configs, relpath):
        for name, text in configs:
            p = os.path.join(d, name)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8") as f:
                f.write(text)
        p = os.path.join(d, relpath)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", encoding="utf-8") as f:
            f.write("to f()\n        x = 1\n")
        res = self.run_fmt([p])
        self.assertEqual(res.returncode, 0, f"stderr: {res.stderr}")
        with open(p, encoding="utf-8") as f:
            return f.read()

    def test_editorconfig_root_true_stops_search(self):
        # root true means parent configs dont apply
        with tempfile.TemporaryDirectory() as d:
            out = self._write_ec_test(
                d,
                [
                    (".editorconfig", "[*.shl]\nindent_size = 6\n"),
                    (
                        "kid/.editorconfig",
                        "root = true\n[*.shl]\nindent_size = 3\n",
                    ),
                ],
                "kid/t.shl",
            )
            self.assertIn("\n   x = 1\n", out)

    def test_editorconfig_no_match_falls_through(self):
        # child config with no matching section keeps searching parents
        with tempfile.TemporaryDirectory() as d:
            out = self._write_ec_test(
                d,
                [
                    (".editorconfig", "[*.shl]\nindent_size = 2\n"),
                    ("kid/.editorconfig", "[*.js]\nindent_size = 6\n"),
                ],
                "kid/t.shl",
            )
            self.assertIn("\n  x = 1\n", out)

    def test_editorconfig_glob_relative_path(self):
        # section globs match against the path relative to the config
        with tempfile.TemporaryDirectory() as d:
            out = self._write_ec_test(
                d,
                [(".editorconfig", "[sub/**.shl]\nindent_size = 2\n")],
                "sub/t.shl",
            )
            self.assertIn("\n  x = 1\n", out)
            out2 = self._write_ec_test(
                d,
                [(".editorconfig", "[sub/**.shl]\nindent_size = 2\n")],
                "top.shl",
            )
            self.assertIn("\n    x = 1\n", out2)


if __name__ == "__main__":
    unittest.main()

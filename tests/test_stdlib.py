import os
import sys
import tempfile
import unittest

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

try:
    from .test_runner import run_shl_code
except (ImportError, ValueError):
    from tests.test_runner import run_shl_code


class TestStdLib(unittest.TestCase):
    def test_builtin_values(self):
        cases = [
            ("items_len = len([1, 2, 3])\nsay items_len", "3"),
            ("flag = bool(1)\nsay flag", "true"),
            ("decimal = float(7)\nsay decimal", "7"),  # C++ prints double without trailing .0 if not needed
            ("text = str(42)\nsay text", "42"),
            ("whole = int(7.8)\nsay whole", "7"),
        ]

        for code, expected in cases:
            with self.subTest(code=code):
                self.assertEqual(run_shl_code(code).strip(), expected)

    def test_print(self):
        self.assertEqual(run_shl_code('say "Hello ShellLite"').strip(), "Hello ShellLite")

    def test_io_helpers(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "sample.txt").replace("\\", "/")
            code = "\n".join([
                f'io_write("{path}", "hello world")',
                f'content = io_read("{path}")',
                f'flag = io_exists("{path}")',
                "say content",
                "say flag"
            ])
            lines = run_shl_code(code).strip().splitlines()
            self.assertEqual(lines[0], "hello world")
            self.assertEqual(lines[1], "true")

    def test_math_helpers(self):
        code = "\n".join([
            "a = math_max(10, 20)",
            "b = math_min(10, 20)",
            "c = math_clamp(25, 0, 10)",
            "say a",
            "say b",
            "say c"
        ])
        lines = run_shl_code(code).strip().splitlines()
        self.assertEqual(lines[0], "20")
        self.assertEqual(lines[1], "10")

    def test_io_append(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "append_test.txt").replace("\\", "/")
            code = "\n".join([
                f'io_write("{path}", "part1 ")',
                f'io_append("{path}", "part2")',
                f'content = io_read("{path}")',
                "say content"
            ])
            self.assertEqual(run_shl_code(code).strip(), "part1 part2")

    def test_math_lerp(self):
        code = "\n".join([
            "val = math_lerp(0, 100, 0.25)",
            "say val"
        ])
        self.assertEqual(run_shl_code(code).strip(), "25")

    def test_csv_parse(self):
        code = """res = csv_op("parse", "a,b,c\\n1,2,3")
say len(res)
say res[0][0]"""
        lines = run_shl_code(code).strip().splitlines()
        self.assertEqual(lines[0], "2")
        self.assertEqual(lines[1], "a")

    def test_assert(self):
        code = """assert(1 == 1, "Should not fail")
say "OK" """
        self.assertEqual(run_shl_code(code).strip(), "OK")

    def test_json(self):
        with tempfile.TemporaryDirectory() as td:
            json_file = os.path.join(td, "test_data.json").replace("\\", "/")
            code = f"""from "json" import json_is_valid, json_pretty, json_get, json_set, json_has, json_keys, json_values, json_write_file_pretty, json_read_file

assert(json_is_valid("{{\\"valid\\": true}}"), "json_is_valid failed on valid json")
assert(not json_is_valid("{{invalid json"), "json_is_valid returned true on invalid json")

data_obj = {{"title": "ShellLite", "version": 1, "nested": {{"count": 42}}}}
pretty_str = json_pretty(data_obj)
assert(len(pretty_str) > 0, "json_pretty empty")

assert(json_get(data_obj, "nested.count") == 42, "json_get failed on nested property")
json_set(data_obj, "nested.count", 99)
assert(json_get(data_obj, "nested.count") == 99, "json_set failed to update property")
json_set(data_obj, "new.deep.value", "hello")
assert(json_get(data_obj, "new.deep.value") == "hello", "json_set failed deep property creation")

assert(json_has(data_obj, "title"), "json_has failed")
assert(not json_has(data_obj, "nonexistent"), "json_has returned true for missing key")
keys = json_keys(data_obj)
assert(len(keys) >= 2, "json_keys length mismatch")
vals = json_values(data_obj)
assert(len(vals) >= 2, "json_values length mismatch")

json_write_file_pretty("{json_file}", data_obj)
loaded_json = json_read_file("{json_file}")
assert(loaded_json["title"] == "ShellLite", "json_read_file title mismatch")
io_delete("{json_file}")
say "JSON OK"
"""
            self.assertEqual(run_shl_code(code).strip(), "JSON OK")

    def test_math(self):
        code = """from "math" import mean, median, variance, std_dev, sign, distance

assert(sin(0) == 0, "sin(0) failed")
assert(cos(0) == 1, "cos(0) failed")
assert(round(sin(PI / 2)) == 1, "sin(PI/2) failed")

num_list = [10, 20, 30, 40, 50]
assert(mean(num_list) == 30, "mean failed")
assert(median(num_list) == 30, "median odd failed")
assert(median([10, 20, 30, 40]) == 25, "median even failed")
assert(variance([10, 10, 10]) == 0, "variance failed")
assert(std_dev([10, 10, 10]) == 0, "std_dev failed")
assert(sign(-15) == -1, "sign negative failed")
assert(sign(15) == 1, "sign positive failed")
assert(sign(0) == 0, "sign zero failed")
assert(distance(0, 0, 3, 4) == 5, "distance failed")
say "MATH OK"
"""
        self.assertEqual(run_shl_code(code).strip(), "MATH OK")

    def test_io_paths(self):
        with tempfile.TemporaryDirectory() as td:
            lines_file = os.path.join(td, "lines_test.txt").replace("\\", "/")
            code = f"""from "io" import join_paths, get_filename, get_file_extension, get_directory, write_all_lines, read_all_lines, get_file_size

joined = join_paths("folder", "file.txt")
assert(len(joined) > 0, "join_paths failed")
assert(get_filename("folder/document.pdf") == "document.pdf", "get_filename failed")
assert(get_file_extension("archive.tar.gz") == ".gz", "get_file_extension failed")
assert(get_directory("folder/sub/doc.txt") == "folder/sub", "get_directory failed")

test_lines = ["Line 1", "Line 2", "Line 3"]
write_all_lines("{lines_file}", test_lines)
read_back = read_all_lines("{lines_file}")
assert(len(read_back) == 3, "read_all_lines length mismatch")
assert(read_back[0] == "Line 1", "read_all_lines line 0 mismatch")
assert(read_back[2] == "Line 3", "read_all_lines line 2 mismatch")
assert(get_file_size("{lines_file}") > 0, "get_file_size failed")
std_io_delete("{lines_file}")
say "IO OK"
"""
            self.assertEqual(run_shl_code(code).strip(), "IO OK")

    def test_str_collections(self):
        code = """assert(str_trim("   hello world   ") == "hello world", "str_trim failed")
assert(str_upper("shell") == "SHELL", "str_upper failed")
assert(str_lower("LITE") == "lite", "str_lower failed")
assert(str_capitalize("hello") == "Hello", "str_capitalize failed")
assert(str_starts_with("prefix_value", "prefix"), "str_starts_with failed")
assert(str_ends_with("filename.shl", ".shl"), "str_ends_with failed")
assert(str_replace("aaa bbb ccc", "bbb", "xxx") == "aaa xxx ccc", "str_replace failed")
assert(str_pad_left("42", 5, "0") == "00042", "str_pad_left failed")
assert(str_pad_right("42", 5, "0") == "42000", "str_pad_right failed")

rev = list_reverse([1, 2, 3])
assert(rev[0] == 3 and rev[2] == 1, "list_reverse failed")
sorted_list = list_sort([5, 1, 4, 2, 3])
assert(sorted_list[0] == 1 and sorted_list[4] == 5, "list_sort failed")
say "STRINGS_COLLECTIONS OK"
"""
        self.assertEqual(run_shl_code(code).strip(), "STRINGS_COLLECTIONS OK")

    def test_datetime(self):
        code = """from "datetime" import get_timestamp, get_current_date, format_time
ts = get_timestamp()
assert(ts > 1700000000, "get_timestamp failed")
date_str = get_current_date()
assert(len(date_str) == 10, "get_current_date format mismatch")
custom_fmt = format_time(ts, "%Y")
assert(len(custom_fmt) == 4, "format_time failed")
say "DATETIME OK"
"""
        self.assertEqual(run_shl_code(code).strip(), "DATETIME OK")

    def test_import_scope(self):
        code = """use "math"
assert(math.mean([10, 20, 30]) == 20, "math.mean property call failed")
say "NO POLLUTION OK"
"""
        self.assertEqual(run_shl_code(code).strip(), "NO POLLUTION OK")

    def test_db_requires_open(self):
        code = """std_db_close()
caught = false
try
    std_db_query_rows("SELECT 1")
catch err
    caught = true
say caught
"""
        out = run_shl_code(code).strip()
        self.assertEqual(out, "true")
        self.assertFalse(os.path.exists("shell_lite.db"))

    def test_stdlib_script(self):
        tests_dir = os.path.dirname(os.path.abspath(__file__))
        script_path = os.path.join(tests_dir, "scripts", "test_stdlib.shl")
        with open(script_path, "r", encoding="utf-8") as f:
            code = f.read()
        output = run_shl_code(code).strip()
        self.assertIn("standard library tests passed!", output)


if __name__ == "__main__":
    unittest.main()

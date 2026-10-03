import os
import struct
import subprocess
import sys
import tempfile
import unittest

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

try:
    from .test_runner import run_shl_code, get_shl_executable
except (ImportError, ValueError):
    from tests.test_runner import run_shl_code, get_shl_executable


def _write_temp_file(content):
    fd, path = tempfile.mkstemp(suffix=".txt")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(content)
    return path.replace("\\", "/")


class TestBugfixNFileRead(unittest.TestCase):
    def assert_fileread_error(self, size_expr):
        path = _write_temp_file("hello")
        try:
            code = 'f = file_open("%s")\nfile_read(f, %s)\n' % (path, size_expr)
            with self.assertRaises(RuntimeError) as ctx:
                run_shl_code(code)
            self.assertIn("file_read: size must be a non-negative number",
                          str(ctx.exception))
        finally:
            os.remove(path)

    def test_negative_size(self):
        self.assert_fileread_error("-1")

    def test_nan_size(self):
        self.assert_fileread_error("(1e308 * 10) - (1e308 * 10)")

    def test_inf_size(self):
        self.assert_fileread_error("1e308 * 10")

    def test_huge_size_reads_to_eof(self):
        path = _write_temp_file("hello")
        try:
            code = ('f = file_open("%s")\n'
                    'say file_read(f, 1e15)\n'
                    'say "after"\n') % path
            out = run_shl_code(code).strip().splitlines()
            self.assertEqual(out[0], "hello")
            self.assertEqual(out[1], "after")
        finally:
            os.remove(path)

    def test_string_size(self):
        self.assert_fileread_error('"oops"')

    def test_negative_size_catchable(self):
        path = _write_temp_file("hello")
        try:
            code = ('f = file_open("%s")\n'
                    'try\n'
                    '    file_read(f, -5)\n'
                    'catch e\n'
                    '    say "caught: " + e\n'
                    'say "after"\n') % path
            out = run_shl_code(code).strip().splitlines()
            self.assertEqual(out[0], "caught: file_read: size must be a non-negative number")
            self.assertEqual(out[1], "after")
        finally:
            os.remove(path)

    def test_valid_size_still_works(self):
        path = _write_temp_file("hello world")
        try:
            code = 'f = file_open("%s")\nsay file_read(f, 5)\n' % path
            self.assertEqual(run_shl_code(code).strip(), "hello")
        finally:
            os.remove(path)


class TestBugfixOJsonStringify(unittest.TestCase):
    def test_cyclic_list(self):
        code = 'l = [1]\npush(l, l)\nsay json_stringify(l)\n'
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code(code)
        self.assertIn("cyclic structure", str(ctx.exception))

    def test_cyclic_dict(self):
        code = 'd = {"a": 1}\nd["self"] = d\nsay json_stringify(d)\n'
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code(code)
        self.assertIn("cyclic structure", str(ctx.exception))

    def test_deep_acyclic_nesting(self):
        code = ("root = []\ncur = root\nrepeat 2000 times\n"
                "    n = []\n    push(cur, n)\n    cur = n\n"
                "say json_stringify(root)\n")
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code(code)
        self.assertIn("nesting too deep", str(ctx.exception))

    def test_cycle_catchable(self):
        code = ('l = [1]\npush(l, l)\ntry\n    say json_stringify(l)\n'
                'catch e\n    say "caught: " + e\n')
        out = run_shl_code(code).strip()
        self.assertEqual(out, "caught: json_stringify: cyclic structure")

    def test_shared_acyclic_ok(self):
        code = 'inner = [1]\nouter = [inner, inner]\nsay json_stringify(outer)\n'
        self.assertEqual(run_shl_code(code).strip(), "[[1], [1]]")

    def test_normal_ok(self):
        code = 'say json_stringify({"a": [1, 2]})\n'
        self.assertEqual(run_shl_code(code).strip(), '{"a": [1, 2]}')


class TestBugfixPJsonParse(unittest.TestCase):
    def _deep_json(self, depth=1500):
        return "[" * depth + "]" * depth

    def test_deep_nesting_rejected(self):
        code = 's = "%s"\njson_parse(s)\n' % self._deep_json()
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code(code)
        self.assertIn("Invalid JSON", str(ctx.exception))

    def test_deep_nesting_is_valid_false(self):
        code = 's = "%s"\nsay json_is_valid(s)\n' % self._deep_json()
        self.assertEqual(run_shl_code(code).strip(), "false")

    def test_normal_parse_ok(self):
        code = 'say json_parse("[1, 2]")[1]\n'
        self.assertEqual(run_shl_code(code).strip(), "2")


class TestBugfixQDbBindShift(unittest.TestCase):
    def test_explicit_handle_placements(self):
        fd, db_path = tempfile.mkstemp(suffix=".db")
        os.close(fd)
        os.remove(db_path)
        db_path = db_path.replace("\\", "/")
        try:
            code = ('mydb = std_db_open("%s")\n'
                    'say std_db_query_rows("SELECT ? + ?", 10, mydb, 20)\n'
                    'say std_db_query_rows("SELECT ? + ?", mydb, 10, 20)\n'
                    'std_db_close(mydb)\n') % db_path
            out = run_shl_code(code).strip().splitlines()
            self.assertIn("30", out[0])
            self.assertIn("30", out[1])
        finally:
            if os.path.exists(db_path):
                os.remove(db_path)

    def test_handle_first(self):
        fd, db_path = tempfile.mkstemp(suffix=".db")
        os.close(fd)
        os.remove(db_path)
        db_path = db_path.replace("\\", "/")
        try:
            code = ('mydb = std_db_open("%s")\n'
                    'say std_db_query_rows(mydb, "SELECT ? + ?", 10, 20)\n'
                    'std_db_close(mydb)\n') % db_path
            out = run_shl_code(code).strip().splitlines()
            self.assertIn("30", out[0])
        finally:
            if os.path.exists(db_path):
                os.remove(db_path)


def _run_shbc_bytes(data):
    exe = get_shl_executable()
    with tempfile.NamedTemporaryFile(suffix=".shbc", delete=False) as f:
        f.write(data)
        path = f.name
    try:
        return subprocess.run([exe, path], capture_output=True, text=True,
                              timeout=120)
    finally:
        os.remove(path)


def _shbc_chunk(nested_func_bytes):
    b = struct.pack('<I', 0x43424853)
    b += struct.pack('<I', 0x00010000)
    if nested_func_bytes is None:
        b += struct.pack('<I', 0)
        code = bytes([36])
    else:
        b += struct.pack('<I', 1)
        b += bytes([0x04]) + nested_func_bytes
        code = bytes([0, 0, 0, 36])
    b += struct.pack('<I', len(code)) + code
    b += struct.pack('<I', 0)
    return b


def _shbc_func(inner_chunk, upvalue_count=0):
    b = struct.pack('<I', 0x43424853)
    b += struct.pack('<HH', 1, 0)
    b += struct.pack('<H', 0)  # arity
    b += struct.pack('<H', upvalue_count)
    b += struct.pack('<I', 0)  # name len
    b += struct.pack('<I', 0)  # source len
    b += inner_chunk
    return b


def _shbc_nested(depth):
    inner = _shbc_chunk(None)
    for _ in range(depth):
        inner = _shbc_chunk(_shbc_func(inner))
    return _shbc_func(inner)


def _shbc_closure(is_local, idx):
    inner_fn = bytes([0x04]) + _shbc_func(_shbc_chunk(None), upvalue_count=1)
    code = (bytes([34, 0, 0]) + bytes([is_local, (idx >> 8) & 0xFF, idx & 0xFF])
            + bytes([4, 1, 36]))  # OP_CLOSURE; upvalue desc; OP_POP; OP_NULL; OP_RETURN
    consts = struct.pack('<III', 0x43424853, 0x00010000, 1) + inner_fn
    consts += struct.pack('<I', len(code)) + code + struct.pack('<I', 0)
    return _shbc_func(consts)


class TestBugfixRShbcNesting(unittest.TestCase):
    def test_deep_nesting_rejected(self):
        res = _run_shbc_bytes(_shbc_nested(1005))
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("nesting too deep", res.stdout + res.stderr)

    def test_shallow_nesting_ok(self):
        res = _run_shbc_bytes(_shbc_nested(5))
        self.assertEqual(res.returncode, 0)


class TestBugfixSClosureUpvalues(unittest.TestCase):
    def test_local_upvalue_oob(self):
        res = _run_shbc_bytes(_shbc_closure(1, 9999))
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("Upvalue index out of range", res.stdout + res.stderr)

    def test_nonlocal_upvalue_oob(self):
        res = _run_shbc_bytes(_shbc_closure(0, 9999))
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("Upvalue index out of range", res.stdout + res.stderr)

    def test_local_upvalue_at_pushed_closure_rejected(self):
        res = _run_shbc_bytes(_shbc_closure(1, 1))
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("Upvalue index out of range", res.stdout + res.stderr)

    def test_local_upvalue_at_enclosing_slot_ok(self):
        res = _run_shbc_bytes(_shbc_closure(1, 0))
        self.assertEqual(res.returncode, 0)


if __name__ == "__main__":
    unittest.main()

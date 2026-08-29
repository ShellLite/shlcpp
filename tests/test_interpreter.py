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


class TestInterpreter(unittest.TestCase):
    def test_var_assign(self):
        code = "x = 10\nsay x"
        self.assertEqual(run_shl_code(code).strip(), "10")

    def test_bin_ops(self):
        code = "x = 5 + 5\nsay x"
        self.assertEqual(run_shl_code(code).strip(), "10")

        code = "y = 10 - 2\nsay y"
        self.assertEqual(run_shl_code(code).strip(), "8")

        code = "z = 10 * 2\nsay z"
        self.assertEqual(run_shl_code(code).strip(), "20")

        code = "w = 10 / 2\nsay w"
        self.assertEqual(run_shl_code(code).strip(), "5")

    def test_conditionals(self):
        code = """x = 10
y = 0
if x > 5:
    y = 1
else:
    y = 2
say y"""
        self.assertEqual(run_shl_code(code).strip(), "1")

        code_else = """x = 3
y = 0
if x > 5:
    y = 1
elif x == 3:
    y = 3
else:
    y = 2
say y"""
        self.assertEqual(run_shl_code(code_else).strip(), "3")

    def test_while_loop(self):
        code = """x = 0
while x < 5:
    x = x + 1
say x"""
        self.assertEqual(run_shl_code(code).strip(), "5")

    def test_function_call(self):
        code = """to get_num
    give 15
result = get_num()
say result"""
        self.assertEqual(run_shl_code(code).strip(), "15")

    def test_list_ops(self):
        code = """arr = [1, 2, 3]
val = arr[0]
say val"""
        self.assertEqual(run_shl_code(code).strip(), "1")

    def test_stop_while(self):
        code = """x = 0
while x < 10:
    x = x + 1
    if x == 5:
        stop
say x"""
        self.assertEqual(run_shl_code(code).strip(), "5")

    def test_skip_loop(self):
        code = """x = 0
total = 0
while x < 5:
    x = x + 1
    if x == 3:
        skip
    total = total + x
say total"""
        # total should be 1 + 2 + 4 + 5 = 12
        self.assertEqual(run_shl_code(code).strip(), "12")

    def test_slice_step(self):
        code = """arr = [1, 2, 3, 4, 5]
rev = arr[::-1]
say rev[0]"""
        self.assertEqual(run_shl_code(code).strip(), "5")

    def test_bitwise_ops(self):
        code = "a = 5 & 3\nsay a"
        self.assertEqual(run_shl_code(code).strip(), "1")

        code = "b = 5 | 2\nsay b"
        self.assertEqual(run_shl_code(code).strip(), "7")

        code = "c = 1 << 3\nsay c"
        self.assertEqual(run_shl_code(code).strip(), "8")

    def test_modulo_pow(self):
        code = "a = 10 % 3\nsay a"
        self.assertEqual(run_shl_code(code).strip(), "1")

        code = "b = 2 ** 3\nsay b"
        self.assertEqual(run_shl_code(code).strip(), "8")

    def test_repeat_loop(self):
        code = """x = 0
repeat 3:
    x = x + 1
say x"""
        self.assertEqual(run_shl_code(code).strip(), "3")

    def test_for_count(self):
        code = """x = 0
for 4:
    x = x + 2
say x"""
        self.assertEqual(run_shl_code(code).strip(), "8")

    def test_unless(self):
        code = """x = 0
unless false:
    x = 42
say x"""
        self.assertEqual(run_shl_code(code).strip(), "42")

        code_skip = """x = 0
unless true:
    x = 42
say x"""
        self.assertEqual(run_shl_code(code_skip).strip(), "0")

    def test_until(self):
        code = """x = 0
until x == 5:
    x = x + 1
say x"""
        self.assertEqual(run_shl_code(code).strip(), "5")

    def test_list_comp(self):
        code = """arr = [1, 2, 3]
res = [x * 2 for x in arr]
say res[0]
say res[1]
say res[2]"""
        output = run_shl_code(code).strip().split()
        self.assertEqual(output, ["2", "4", "6"])

    def test_convert(self):
        code = """s = convert 42 to "string"
say s"""
        self.assertEqual(run_shl_code(code).strip(), "42")

    def test_scientific_notation(self):
        code = """x = 1e2
say x"""
        self.assertEqual(run_shl_code(code).strip(), "100")

    def test_eval_flag(self):
        output = run_shl_code("say 123 * 2")
        self.assertEqual(output.strip(), "246")

    def test_random(self):
        code = """use "random"
r = randint(5, 10)
say r >= 5 and r <= 10"""
        self.assertEqual(run_shl_code(code).strip(), "true")

    def test_json(self):
        code = """parsed = json_parse("{\\"k\\": 42}")
say parsed["k"]
dumped = json_stringify(parsed)
say dumped"""
        output = run_shl_code(code).strip().splitlines()
        self.assertEqual(output[0], "42")
        self.assertIn('"k"', output[1])

    def test_list_repr(self):
        code = """arr = [1, 2, "abc"]
say arr"""
        self.assertEqual(run_shl_code(code).strip(), '[1, 2, "abc"]')

    def test_dict_repr(self):
        code = """d = {"a": 10}
say d"""
        self.assertEqual(run_shl_code(code).strip(), '{"a": 10}')

    def test_range_args(self):
        code = """r = range(1, 6, 2)
say r"""
        self.assertEqual(run_shl_code(code).strip(), "[1, 3, 5]")

    def test_empty_script(self):
        output = run_shl_code("")
        self.assertEqual(output.strip(), "")

    def test_shift_bounds(self):
        code = """a = 1 << 2
b = 8 >> 2
say a
say b"""
        self.assertEqual(run_shl_code(code).strip().split(), ["4", "2"])

    def test_large_randint(self):
        code = """use "random"
r = randint(100000, 200000)
say r >= 100000 and r <= 200000"""
        self.assertEqual(run_shl_code(code).strip(), "true")

    def test_variable_names(self):
        code = """size = 42
item = "apple"
items = [1, 2, 3]
app = "myapp"
count = 100
say size
say item
say items
say app
say count"""
        self.assertEqual(run_shl_code(code).strip().split("\n"), ["42", "apple", '[1, 2, 3]', "myapp", "100"])

    def test_csv_quotes(self):
        code = """csv_str = "\\"hello\\nworld\\",123\\nfoo,bar"
res = csv_op("parse", csv_str)
say len(res)
say res[0][0]"""
        self.assertEqual(run_shl_code(code).strip(), "2\nhello\nworld")

    def test_io_listdir(self):
        code = """files = io_listdir(".")
say len(files) >= 0"""
        self.assertEqual(run_shl_code(code).strip(), "true")

    def test_string_methods(self):
        code = """s = "  hello world  "
say s.strip()
say s.trim()
s_dash = "foo-bar"
say s_dash.replace("-", "_")
s_hello = "hello"
say s_hello.starts_with("he")
say s_hello.ends_with("lo")
s_apple = "apple"
say s_apple.capitalize()
s_csv = "a,b,c"
parts = s_csv.split(",")
say parts.len()
s_abc = "abcdef"
say s_abc.contains("cde")"""
        out = run_shl_code(code).strip().splitlines()
        self.assertEqual(out, [
            "hello world",
            "hello world",
            "foo_bar",
            "true",
            "true",
            "Apple",
            "3",
            "true"
        ])

    def test_nested_lists(self):
        code = """grid = [[1, 2], [3, 4]]
grid[0][1] = 99
grid[1][0] = 77
say grid[0][1]
say grid[1][0]
say len(grid)"""
        out = run_shl_code(code).strip().splitlines()
        self.assertEqual(out, ["99", "77", "2"])

    def test_closure_state(self):
        code = """to make_counter(init_val)
    val = init_val
    to next_val()
        val = val + 1
        give val
    give next_val

c = make_counter(10)
say c()
say c()
say c()"""
        out = run_shl_code(code).strip().splitlines()
        self.assertEqual(out, ["11", "12", "13"])

    def test_concatenation(self):
        code = """prefix = "Hello, "
target = "World"
full = prefix + target + "!"
say full"""
        self.assertEqual(run_shl_code(code).strip(), "Hello, World!")

    def test_boolean_logic(self):
        code = """a = true
b = false
say a and b
say a or b
say not b"""
        out = run_shl_code(code).strip().splitlines()
        self.assertEqual(out, ["false", "true", "true"])


if __name__ == "__main__":
    unittest.main()




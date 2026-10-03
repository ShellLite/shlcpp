import unittest

try:
    from .test_runner import run_shl_code
except ImportError:
    from tests.test_runner import run_shl_code


class TestInheritance(unittest.TestCase):
    def test_child_inherits_parent_method(self):
        code = (
            "class Animal\n"
            "    to speak()\n"
            "        say \"generic\"\n"
            "class Dog extends Animal\n"
            "d = Dog()\n"
            "d.speak()\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "generic")

    def test_child_override_wins(self):
        code = (
            "class Animal\n"
            "    to speak()\n"
            "        say \"generic\"\n"
            "class Dog extends Animal\n"
            "    to speak()\n"
            "        say \"woof\"\n"
            "d = Dog()\n"
            "d.speak()\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "woof")

    def test_grandchild_inherits_through_chain(self):
        code = (
            "class A\n"
            "    to who()\n"
            "        say \"a\"\n"
            "class B extends A\n"
            "class C extends B\n"
            "c = C()\n"
            "c.who()\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "a")

    def test_child_init_wins(self):
        code = (
            "class Base\n"
            "    has x = 10\n"
            "class Child extends Base\n"
            "    to init()\n"
            "        x = 7\n"
            "c = Child()\n"
            "say c.x\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "7")

    def test_inherited_init_sets_parent_fields(self):
        code = (
            "class Base\n"
            "    has x = 10\n"
            "class Child extends Base\n"
            "c = Child()\n"
            "say c.x\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "10")

    def test_child_own_field_without_init(self):
        code = (
            "class Base\n"
            "    has x = 10\n"
            "class Child extends Base\n"
            "    has y = 20\n"
            "c = Child()\n"
            "say c.y\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "20")

    def test_child_init_sets_parent_and_own_fields(self):
        code = (
            "class Base\n"
            "    has x = 10\n"
            "class Child extends Base\n"
            "    has y = 20\n"
            "c = Child()\n"
            "say c.x\n"
            "say c.y\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "10\n20")

    def test_grandchild_chain_initializes_all_fields(self):
        code = (
            "class A\n"
            "    has x = 1\n"
            "class B extends A\n"
            "    has y = 2\n"
            "class C extends B\n"
            "    has z = 3\n"
            "c = C()\n"
            "say c.x\n"
            "say c.y\n"
            "say c.z\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "1\n2\n3")

    def test_child_without_new_fields_keeps_parent_positional_init(self):
        code = (
            "class Base\n"
            "    has x = 10\n"
            "class Child extends Base\n"
            "c = Child(99)\n"
            "say c.x\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "99")

    def test_child_field_overrides_parent_default(self):
        code = (
            "class Base\n"
            "    has x = 10\n"
            "class Child extends Base\n"
            "    has x = 99\n"
            "c = Child()\n"
            "say c.x\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "99")

    def test_explicit_parent_init_arity_error_propagates(self):
        code = (
            "class Base\n"
            "    to init(a)\n"
            "        self.x = a\n"
            "class Child extends Base\n"
            "    has y = 20\n"
            "c = Child()\n"
        )
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code(code)
        self.assertIn("Expected 1 arguments but got 0", str(ctx.exception))

    def test_missing_parent_is_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("class Dog extends Nope\n")
        self.assertIn("Nope", str(ctx.exception))

    def test_non_class_parent_is_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("x = 5\nclass Dog extends x\n")
        self.assertIn("non-class", str(ctx.exception))

    def test_super_forwards_args_to_parent_init(self):
        code = (
            "class Base\n"
            "    to init(a)\n"
            "        self.x = a\n"
            "class Child extends Base\n"
            "    to init(a, b)\n"
            "        super(a)\n"
            "        self.y = b\n"
            "c = Child(10, 20)\n"
            "say c.x\n"
            "say c.y\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "10\n20")

    def test_super_no_args_calls_parameterless_parent_init(self):
        code = (
            "class Base\n"
            "    to init()\n"
            "        self.x = 42\n"
            "class Child extends Base\n"
            "    to init()\n"
            "        super()\n"
            "        self.y = 7\n"
            "c = Child()\n"
            "say c.x\n"
            "say c.y\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "42\n7")


class TestRegexLiterals(unittest.TestCase):
    def test_match_true(self):
        code = "r = /hello/\nsay r.test(\"say hello\")\n"
        self.assertEqual(run_shl_code(code).strip(), "true")

    def test_match_false(self):
        code = "r = /hello/\nsay r.test(\"goodbye\")\n"
        self.assertEqual(run_shl_code(code).strip(), "false")

    def test_case_insensitive_flag(self):
        code = "r = /hello/i\nsay r.test(\"HELLO\")\n"
        self.assertEqual(run_shl_code(code).strip(), "true")

    def test_invalid_regex_is_runtime_error(self):
        # [z-a] is accepted by libc++ std::regex, brace range throws everywhere
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("r = /a{2,1}/\n")
        self.assertIn("invalid regex", str(ctx.exception))

    def test_unknown_flag_is_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("r = /hello/g\n")
        self.assertIn("unknown regex flag", str(ctx.exception))

    def test_test_without_args_is_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("r = /hello/\nsay r.test()\n")
        self.assertIn("expects 1 argument", str(ctx.exception))


class TestDefaultArguments(unittest.TestCase):
    def test_omitted_uses_default(self):
        code = (
            "to greet(name, greeting=\"hello\")\n"
            "    say greeting + \" \" + name\n"
            "greet(\"bob\")\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "hello bob")

    def test_explicit_overrides_default(self):
        code = (
            "to greet(name, greeting=\"hello\")\n"
            "    say greeting + \" \" + name\n"
            "greet(\"bob\", \"hi\")\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "hi bob")

    def test_multiple_defaults(self):
        code = (
            "to add(a, b=10, c=100)\n"
            "    say a + b + c\n"
            "add(1)\n"
            "add(1, 2)\n"
            "add(1, 2, 3)\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "111\n103\n6")

    def test_non_trailing_default_is_compile_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("to f(a=1, b)\n    say a\n")
        self.assertIn("default arguments must be trailing", str(ctx.exception))

    def test_too_many_args_is_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("to f(a, b=2)\n    say a\nf(1, 2, 3)\n")
        self.assertIn("Expected 1 to 2 arguments but got 3", str(ctx.exception))

    def test_too_few_args_is_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("to f(a, b=2)\n    say a\nf()\n")
        self.assertIn("Expected 1 to 2 arguments but got 0", str(ctx.exception))


class TestDelStatement(unittest.TestCase):
    def test_del_dict_key(self):
        code = (
            "d = {\"a\": 1, \"b\": 2}\n"
            "del d[\"a\"]\n"
            "say \"a\" in d\n"
            "say d[\"b\"]\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "false\n2")

    def test_del_dict_attr(self):
        code = "d = {\"a\": 1}\ndel d.a\nsay d\n"
        self.assertEqual(run_shl_code(code).strip(), "{}")

    def test_del_instance_field(self):
        code = (
            "class Point\n"
            "    to init(x, y)\n"
            "        self.x = x\n"
            "        self.y = y\n"
            "p = Point(1, 2)\n"
            "del p.x\n"
            "say p.y\n"
        )
        self.assertEqual(run_shl_code(code).strip(), "2")

    def test_del_missing_instance_field_is_error(self):
        code = (
            "class Point\n"
            "    to init(x)\n"
            "        self.x = x\n"
            "p = Point(1)\n"
            "del p.z\n"
        )
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code(code)
        self.assertIn("del: field not found", str(ctx.exception))

    def test_del_list_index(self):
        code = "l = [1, 2, 3]\ndel l[1]\nsay l\n"
        self.assertEqual(run_shl_code(code).strip(), "[1, 3]")

    def test_del_missing_key_is_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("d = {\"a\": 1}\ndel d[\"zzz\"]\n")
        self.assertIn("del: key not found", str(ctx.exception))

    def test_del_list_out_of_range_is_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("l = [1]\ndel l[5]\n")
        self.assertIn("del: list index out of range", str(ctx.exception))

    def test_del_non_container_is_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("x = 5\ndel x[0]\n")
        self.assertIn("del: can only delete from dicts, lists, and instances", str(ctx.exception))

    def test_del_nested_index_target(self):
        code = (
            "d = {\"x\": {\"y\": 1, \"z\": 2}}\n"
            "del d[\"x\"][\"y\"]\n"
            "say d\n"
        )
        self.assertEqual(run_shl_code(code).strip(), '{"x": {"z": 2}}')

    def test_del_compound_target_in_method(self):
        code = (
            "class K\n"
            "    has cache = {\"a\": 1, \"b\": 2}\n"
            "    has items = [10, 20, 30]\n"
            "    to clear()\n"
            "        del self.cache[\"a\"]\n"
            "        del self.items[0]\n"
            "k = K()\n"
            "k.clear()\n"
            "say k.cache\n"
            "say k.items\n"
        )
        self.assertEqual(run_shl_code(code).strip(), '{"b": 2}\n[20, 30]')

    def test_del_bare_name_is_syntax_error(self):
        with self.assertRaises(RuntimeError) as ctx:
            run_shl_code("x = 5\ndel x\n")
        self.assertIn("del", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()

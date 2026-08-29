import os
import sys
import unittest

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

import importlib.util

_LSP_PATH = os.path.join(_PROJECT_ROOT, "shell_lite", "lsp_server.py")
_spec = importlib.util.spec_from_file_location("local_lsp_server", _LSP_PATH)
_lsp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lsp)

ShellLiteDocument = _lsp.ShellLiteDocument
SymbolKind = _lsp.SymbolKind

class TestTopologicalCaching(unittest.TestCase):
    def test_chunk_anchors(self):
        code = """to first()
    give 10

to second()
    give 20

structure Third
    has val = 30
"""
        doc = ShellLiteDocument("file:///test.shl", code)
        self.assertEqual(len(doc.chunks), 3)
        self.assertEqual(len(doc.symbols), 3)
        self.assertEqual(doc.symbols[0]["name"], "first")
        self.assertEqual(doc.symbols[1]["name"], "second")
        self.assertEqual(doc.symbols[2]["name"], "Third")

    def test_incremental_edit(self):
        code = """to alpha()
    give 1

to beta()
    give 2

to gamma()
    give 3
"""
        doc = ShellLiteDocument("file:///test.shl", code)
        self.assertEqual(len(doc.chunks), 3)

        modified_code = """to alpha()
    give 1

to beta_renamed()
    make extra = 99
    give 2

to gamma()
    give 3
"""
        doc.update(modified_code)
        self.assertEqual(len(doc.chunks), 3)
        self.assertEqual(doc.symbols[0]["name"], "alpha")
        self.assertEqual(doc.symbols[1]["name"], "beta_renamed")
        self.assertEqual(doc.symbols[2]["name"], "extra")
        self.assertEqual(doc.symbols[3]["name"], "gamma")

    def test_line_shift(self):
        code = """to fn1()
    give 1

to fn2()
    give 2
"""
        doc = ShellLiteDocument("file:///test.shl", code)
        self.assertEqual(doc.chunks[1].start_line, 4)
        fn2_sym = [s for s in doc.symbols if s["name"] == "fn2"][0]
        self.assertEqual(fn2_sym["range"]["start"]["line"], 3)

        expanded_code = """to fn1()
    make a = 1
    make b = 2
    make c = 3
    give a + b + c

to fn2()
    give 2
"""
        doc.update(expanded_code)
        self.assertEqual(len(doc.chunks), 2)
        self.assertEqual(doc.chunks[1].start_line, 7)
        fn2_sym_after = [s for s in doc.symbols if s["name"] == "fn2"][0]
        self.assertEqual(fn2_sym_after["range"]["start"]["line"], 6)

    def test_anchor_resync(self):
        code = """to fn1()
    give 1

to fn2()
    give 2
"""
        doc = ShellLiteDocument("file:///test.shl", code)
        self.assertEqual(len(doc.chunks), 2)

        mutated_code = """to fn1()
    give 1

structure DataHolder
    has x = 10
"""
        doc.update(mutated_code)
        self.assertEqual(len(doc.chunks), 2)
        self.assertEqual(doc.symbols[0]["name"], "fn1")
        self.assertEqual(doc.symbols[1]["name"], "DataHolder")
        self.assertEqual(doc.symbols[1]["kind"], SymbolKind.Struct)

if __name__ == "__main__":
    unittest.main()

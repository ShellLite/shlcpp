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

class TestConcurrencyCOW(unittest.TestCase):
    def test_shared_send(self):
        code = """
ch = chan_open()
orig = [10, 20, 30]

chan_send(ch, orig)

orig[0] = 999
received = chan_recv(ch)
say orig[0]
say received[0]
"""
        output = run_shl_code(code).strip().splitlines()
        self.assertEqual(output[0].strip(), "999")
        self.assertEqual(output[1].strip(), "10")

    def test_transfer_aliasing(self):
        code = """
ch = chan_open()
x = [1, 2, 3]
y = x

chan_transfer(ch, x)

y[0] = 777
received = chan_recv(ch)

say y[0]
say received[0]
"""
        output = run_shl_code(code).strip().splitlines()
        self.assertEqual(output[0].strip(), "777")
        self.assertEqual(output[1].strip(), "1")

    def test_dict_cow(self):
        code = """
ch = chan_open()
d = {"a": 100}
chan_send(ch, d)

d["a"] = 999
rec = chan_recv(ch)

say d["a"]
say rec["a"]
"""
        output = run_shl_code(code).strip().splitlines()
        self.assertEqual(output[0].strip(), "999")
        self.assertEqual(output[1].strip(), "100")

    def test_lock_acquire(self):
        code = """
my_lock = create_lock()
lock_acquire(my_lock)
say "locked"
lock_release(my_lock)
say "unlocked"
"""
        output = run_shl_code(code).strip().splitlines()
        self.assertEqual(output, ["locked", "unlocked"])

    def test_lock_block(self):
        code = """
my_lock = create_lock()
to run_critical():
    say "inside lock block"
lock_block(my_lock, run_critical)
"""
        output = run_shl_code(code).strip()
        self.assertEqual(output, "inside lock block")

if __name__ == "__main__":
    unittest.main()

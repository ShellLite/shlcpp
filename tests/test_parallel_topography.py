import os
import shutil
import subprocess
import sys
import tempfile
import unittest

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _find_library():
    candidates = [
        os.path.join(_PROJECT_ROOT, "build_cpp", "Release", "shell_lite_lib.dll"),
        os.path.join(_PROJECT_ROOT, "build_cpp", "libshell_lite_lib.so"),
        os.path.join(_PROJECT_ROOT, "build_cpp", "libshell_lite_lib.dylib"),
        os.path.join(_PROJECT_ROOT, "shell_lite_lib.dll"),
    ]
    for path in candidates:
        if os.path.exists(path):
            return path
    return None


class TestParallelTopographyEquivalence(unittest.TestCase):
    def test_parallel_matches_sequential(self):
        lib = _find_library()
        if lib is None:
            self.skipTest("shell_lite_lib not built")
        compiler = shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            self.skipTest("no C++ compiler available")
        test_src = os.path.join(_PROJECT_ROOT, "tests", "cpp", "test_parallel_topography_equivalence.cpp")
        include_dir = os.path.join(_PROJECT_ROOT, "shell_lite")
        lib_dir = os.path.dirname(lib)
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "test_parallel_topography_equivalence")
            build = subprocess.run(
                [compiler, "-std=c++20", test_src, "-I", include_dir,
                 "-L", lib_dir, "-lshell_lite_lib", "-o", exe],
                capture_output=True, text=True, timeout=600,
            )
            self.assertEqual(build.returncode, 0, build.stderr[-2000:])
            env = dict(os.environ)
            env["LD_LIBRARY_PATH"] = lib_dir + os.pathsep + env.get("LD_LIBRARY_PATH", "")
            run = subprocess.run([exe], capture_output=True, text=True, timeout=300, env=env)
            self.assertIn("ALL PASS", run.stdout, run.stdout[-2000:] + run.stderr[-2000:])
            self.assertEqual(run.returncode, 0, run.stdout[-2000:])


if __name__ == "__main__":
    unittest.main()

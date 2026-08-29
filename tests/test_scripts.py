import os
import sys
import glob
import subprocess
import unittest
import ctypes

_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

try:
    from .test_runner import get_shl_lib, get_shl_executable
except (ImportError, ValueError):
    from tests.test_runner import get_shl_lib, get_shl_executable

class TestNativeScripts(unittest.TestCase):
    pass

def generate_test(script_path):
    def test(self):
        with open(script_path, "r", encoding="utf-8") as f:
            script_content = f.read()
            
        expected_error = None
        for line in script_content.splitlines():
            if line.startswith("# EXPECT_ERROR:"):
                expected_error = line.replace("# EXPECT_ERROR:", "").strip()
                break
                
        shl_lib = get_shl_lib()
        is_fail_test = expected_error is not None
        
        if shl_lib and hasattr(shl_lib, "run_shl_file"):
            import sys
            sys.stdout.flush()
            sys.stderr.flush()
            stdout_fd = 1
            stderr_fd = 2
            saved_stdout_fd = os.dup(stdout_fd)
            saved_stderr_fd = os.dup(stderr_fd)
            r, w = os.pipe()
            os.dup2(w, stdout_fd)
            os.dup2(w, stderr_fd)
            os.close(w)

            try:
                shl_lib.run_shl_file.argtypes = [ctypes.c_char_p]
                shl_lib.run_shl_file.restype = ctypes.c_int
                ret = shl_lib.run_shl_file(script_path.encode("utf-8"))
            finally:
                os.dup2(saved_stdout_fd, stdout_fd)
                os.dup2(saved_stderr_fd, stderr_fd)
                os.close(saved_stdout_fd)
                os.close(saved_stderr_fd)
                captured_output = os.read(r, 65536).decode("utf-8")
                os.close(r)

            if is_fail_test:
                if ret == 0:
                    self.fail(f"Script {script_path} was expected to fail with '{expected_error}', but succeeded.")
                elif expected_error not in captured_output:
                    self.fail(f"Script {script_path} failed, but did not produce expected error: '{expected_error}'.\nActual output:\n{captured_output}")
            else:
                if ret != 0 or "Exception:" in captured_output:
                    self.fail(f"Script {script_path} failed with code {ret}!\nOutput:\n{captured_output}")
            return

        binary_name = "shlcpp.exe" if os.name == "nt" else "shlcpp"
        exe_path = os.path.join(os.path.dirname(__file__), "..", binary_name)
        result = subprocess.run([exe_path, script_path], capture_output=True, text=True)
        combined_output = result.stdout + "\n" + result.stderr
        
        if is_fail_test:
            if result.returncode == 0:
                self.fail(f"Script {script_path} was expected to fail with '{expected_error}', but succeeded.")
            elif expected_error not in combined_output:
                self.fail(f"Script {script_path} failed, but did not produce expected error: '{expected_error}'.\nActual output:\n{combined_output}")
        else:
            if result.returncode != 0 or "Exception:" in result.stdout or "Exception:" in result.stderr:
                error_msg = f"Script {script_path} failed!\nExit code: {result.returncode}\nSTDOUT:\n{result.stdout}\nSTDERR:\n{result.stderr}\n"
                self.fail(error_msg)
            
    return test

scripts_dir = os.path.join(os.path.dirname(__file__), "scripts")
script_files = glob.glob(os.path.join(scripts_dir, "*.shl"))

for _script_file in script_files:
    base = os.path.basename(_script_file).replace(".shl", "")
    _test_name = base if base.startswith("test_") else f"test_{base}"
    setattr(TestNativeScripts, _test_name, generate_test(_script_file))

if __name__ == "__main__":
    unittest.main()

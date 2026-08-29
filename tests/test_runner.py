import os
import subprocess
import tempfile
import sys
import ctypes

_SHL_LIB = None

# Ensure SHL_PATH points to the project's stdlib directory for tests
_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_STDLIB_DIR = os.path.join(_PROJECT_ROOT, "shell_lite", "stdlib")
if "SHL_PATH" not in os.environ:
    os.environ["SHL_PATH"] = _STDLIB_DIR
elif _STDLIB_DIR not in os.environ["SHL_PATH"]:
    os.environ["SHL_PATH"] = f"{_STDLIB_DIR}{os.pathsep}{os.environ['SHL_PATH']}"

def get_shl_lib():
    global _SHL_LIB
    if _SHL_LIB is None:
        candidates = [
            os.path.join(_PROJECT_ROOT, "build_cpp", "Release", "shell_lite_lib.dll"),
            os.path.join(_PROJECT_ROOT, "build_cpp", "libshell_lite_lib.so"),
            os.path.join(_PROJECT_ROOT, "build_cpp", "libshell_lite_lib.dylib"),
            os.path.join(_PROJECT_ROOT, "shell_lite_lib.dll"),
        ]
        for dll_path in candidates:
            if os.path.exists(dll_path):
                try:
                    _SHL_LIB = ctypes.CDLL(dll_path)
                    break
                except Exception:
                    pass
        if _SHL_LIB is None:
            _SHL_LIB = False
    return _SHL_LIB if _SHL_LIB is not False else None

def get_shl_executable():
    tests_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(tests_dir)
    
    candidates = [
        os.path.join(project_root, "build_cpp", "Release", "shlcpp.exe"),
        os.path.join(project_root, "build_cpp", "Release", "shell_lite_exec.exe"),
        os.path.join(project_root, "build_cpp", "shlcpp"),
        os.path.join(project_root, "build_cpp", "shell_lite_exec"),
        os.path.join(project_root, "shlcpp.exe"),
        os.path.join(project_root, "shlcpp"),
    ]
    for exe_path in candidates:
        if os.path.exists(exe_path):
            return exe_path
        
    raise FileNotFoundError(
        "Could not find native 'shell_lite_exec' or 'shlcpp' binary. "
        "Please compile the C++ source first before running tests."
    )

def run_shl_code(code: str, args: list = None) -> str:
    with tempfile.NamedTemporaryFile(mode="w", suffix=".shl", delete=False, encoding="utf-8") as temp_script:
        temp_script.write(code)
        temp_script_path = temp_script.name

    try:
        shl_lib = get_shl_lib()
        if shl_lib and hasattr(shl_lib, "run_shl_file"):
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
                if args and hasattr(shl_lib, "run_shl_file_args"):
                    shl_lib.run_shl_file_args.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
                    shl_lib.run_shl_file_args.restype = ctypes.c_int
                    args_bytes = [a.encode("utf-8") for a in args]
                    args_arr = (ctypes.c_char_p * len(args_bytes))(*args_bytes)
                    ret = shl_lib.run_shl_file_args(temp_script_path.encode("utf-8"), len(args_bytes), args_arr)
                else:
                    shl_lib.run_shl_file.argtypes = [ctypes.c_char_p]
                    shl_lib.run_shl_file.restype = ctypes.c_int
                    ret = shl_lib.run_shl_file(temp_script_path.encode("utf-8"))
            finally:
                os.dup2(saved_stdout_fd, stdout_fd)
                os.dup2(saved_stderr_fd, stderr_fd)
                os.close(saved_stdout_fd)
                os.close(saved_stderr_fd)
                captured_output = os.read(r, 65536).decode("utf-8")
                os.close(r)

            if ret != 0:
                raise RuntimeError(f"shlcpp execution failed (exit {ret}):\n{captured_output}")
            return captured_output

        exe_path = get_shl_executable()
        cmd = [exe_path, temp_script_path]
        if args:
            cmd.extend(args)
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8"
        )
        if result.returncode != 0:
            raise RuntimeError(f"shlcpp execution failed (exit {result.returncode}):\n{result.stderr}\nSTDOUT:\n{result.stdout}")
        return result.stdout
    finally:
        if os.path.exists(temp_script_path):
            os.remove(temp_script_path)

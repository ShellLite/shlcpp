"""
shlcpp Python Runtime Bindings
Loads and invokes the native C++ shlcpp engine (shell_lite_lib.dll / libshell_lite_lib.so).
"""
import ctypes
from pathlib import Path
from typing import List, Optional

def _find_library() -> str:
    base_dir = Path(__file__).resolve().parent
    candidates = [
        base_dir / "shell_lite_lib.dll",
        base_dir.parent / "shell_lite_lib.dll",
        base_dir / "libshell_lite_lib.so",
        base_dir.parent / "libshell_lite_lib.so",
        base_dir / "libshell_lite_lib.dylib",
        base_dir.parent / "libshell_lite_lib.dylib",
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    return "shell_lite_lib"

_LIB_PATH = _find_library()
try:
    _lib = ctypes.CDLL(_LIB_PATH)
    _lib.run_shl_file.argtypes = [ctypes.c_char_p]
    _lib.run_shl_file.restype = ctypes.c_int
    _lib.run_shl_file_args.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    _lib.run_shl_file_args.restype = ctypes.c_int
except Exception:
    _lib = None

def run_file(file_path: str, args: Optional[List[str]] = None) -> int:
    """Run a shlcpp source or bytecode file using the native C++ runtime."""
    if not _lib:
        raise RuntimeError(f"shlcpp native library could not be loaded from '{_LIB_PATH}'")
    if args is None:
        return _lib.run_shl_file(str(file_path).encode("utf-8"))
    c_args = (ctypes.c_char_p * len(args))(*[a.encode("utf-8") for a in args])
    return _lib.run_shl_file_args(str(file_path).encode("utf-8"), len(args), c_args)
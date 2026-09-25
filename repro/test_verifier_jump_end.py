import struct
import subprocess
import tempfile
import os

SHBC_MAGIC = 0x43424853
CHUNK_MAGIC = 0x43424853
CHUNK_VERSION = 0x00010000

def create_valid_shbc(code_bytes, constants=None):
    if constants is None:
        constants = []
    
    out = bytearray()
    out.extend(struct.pack("<I", SHBC_MAGIC))
    out.extend(struct.pack("<H", 1))
    out.extend(struct.pack("<H", 0))
    out.extend(struct.pack("<H", 0))
    out.extend(struct.pack("<H", 0))
    name = "main"
    out.extend(struct.pack("<I", len(name)))
    out.extend(name.encode("utf-8"))
    src = "main.shl"
    out.extend(struct.pack("<I", len(src)))
    out.extend(src.encode("utf-8"))

    out.extend(struct.pack("<I", CHUNK_MAGIC))
    out.extend(struct.pack("<I", CHUNK_VERSION))
    out.extend(struct.pack("<I", len(constants)))
    out.extend(struct.pack("<I", len(code_bytes)))
    out.extend(code_bytes)
    out.extend(struct.pack("<I", 0))
    return bytes(out)

def test_chunk_end():
    # OP_JUMP (41) with offset 1 on a 4-byte chunk:
    # [41, 0, 1, 85] -> after read_short, ip=3. target = 3 + 1 = 4 == code.size()
    code_jump_end = bytes([41, 0, 1, 85])
    shbc_data = create_valid_shbc(code_jump_end)
    with tempfile.NamedTemporaryFile(suffix=".shbc", delete=False) as f:
        f.write(shbc_data)
        fname = f.name
    try:
        res = subprocess.run(["./shlcpp.exe", fname], capture_output=True, text=True)
        print(f"Jump to code.size(): rc={res.returncode}, stderr={res.stderr.strip()}")
        assert res.returncode != 0
    finally:
        if os.path.exists(fname):
            os.remove(fname)

if __name__ == "__main__":
    test_chunk_end()

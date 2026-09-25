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
    # ObjFunction header
    out.extend(struct.pack("<I", SHBC_MAGIC))
    out.extend(struct.pack("<H", 1)) # major
    out.extend(struct.pack("<H", 0)) # minor
    out.extend(struct.pack("<H", 0)) # arity
    out.extend(struct.pack("<H", 0)) # upvalue_count
    name = "main"
    out.extend(struct.pack("<I", len(name)))
    out.extend(name.encode("utf-8"))
    src = "main.shl"
    out.extend(struct.pack("<I", len(src)))
    out.extend(src.encode("utf-8"))

    # Chunk header
    out.extend(struct.pack("<I", CHUNK_MAGIC))
    out.extend(struct.pack("<I", CHUNK_VERSION))
    out.extend(struct.pack("<I", len(constants)))
    for tag, val in constants:
        out.append(tag)
        if tag == 0:
            pass
        elif tag == 1:
            out.append(1 if val else 0)
        elif tag == 2:
            out.extend(struct.pack("<d", float(val)))
        elif tag == 3:
            s_bytes = val.encode("utf-8")
            out.extend(struct.pack("<I", len(s_bytes)))
            out.extend(s_bytes)
    out.extend(struct.pack("<I", len(code_bytes)))
    out.extend(code_bytes)
    out.extend(struct.pack("<I", 0)) # locations
    return bytes(out)

def test_chunk(code_bytes, constants=None):
    shbc_data = create_valid_shbc(code_bytes, constants)
    with tempfile.NamedTemporaryFile(suffix=".shbc", delete=False) as f:
        f.write(shbc_data)
        fname = f.name
    try:
        res = subprocess.run(["./shlcpp.exe", fname], capture_output=True, text=True)
        return res.returncode, res.stdout, res.stderr
    finally:
        if os.path.exists(fname):
            os.remove(fname)

if __name__ == "__main__":
    # Test 1: Empty chunk (Finding B)
    rc, out, err = test_chunk(b"")
    print(f"Empty chunk: rc={rc}, err={err.strip()}")

    # Test 2: Jump into middle of operand (Finding G)
    # OP_JUMP (41) offset 1 -> lands at offset 3 (middle of OP_CONSTANT 2-byte operand)
    # Code: [OP_JUMP, 0, 1, OP_CONSTANT, 0, 0, OP_HALT]
    # ip after reading offset is 3. target = 3 + 1 = 4 (which is middle of OP_CONSTANT operand at [3..5])
    code_mid_jump = bytes([41, 0, 1, 0, 0, 0, 85])
    rc, out, err = test_chunk(code_mid_jump, constants=[(2, 42.0)])
    print(f"Jump into operand: rc={rc}, err={err.strip()}")

    # Test 3: Jump out of bounds (Finding G)
    # OP_JUMP (41) offset 100 on 4-byte chunk
    code_oob_jump = bytes([41, 0, 100, 85])
    rc, out, err = test_chunk(code_oob_jump)
    print(f"Jump out of bounds: rc={rc}, err={err.strip()}")

    # Test 4: OP_GET_LOCAL with slot >= 1024 (Finding F)
    # OP_GET_LOCAL (5), slot = 2000 (0x07D0)
    code_slot_oob = bytes([5, 0x07, 0xD0, 85])
    rc, out, err = test_chunk(code_slot_oob)
    print(f"Slot OOB: rc={rc}, err={err.strip()}")

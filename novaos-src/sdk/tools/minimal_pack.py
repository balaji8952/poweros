#!/usr/bin/env python3
"""The smallest possible NovaOS package writer: no dependencies, ~25 lines.
Shows exactly what mkpkg.py does, so you can generate packages from any language."""
import struct, zlib, sys

def make_package(meta: dict, typ: dict, data: bytes) -> bytes:
    m = "".join("%s=%s\n" % kv for kv in meta.items()).encode("ascii")     # META section
    t = "".join("%s=%s\n" % kv for kv in typ.items()).encode("ascii")      # TYPE section
    total = 48 + len(m) + len(t) + len(data)
    header = (b"NPKG" + struct.pack("<HHII", 1, 48, total, 0)               # magic, version, header size, total, crc(0 for now)
              + struct.pack("<IIIIII", 48, len(m), 48 + len(m), len(t), 48 + len(m) + len(t), len(data))
              + b"\0" * 8)                                                  # reserved
    blob = header + m + t + data
    crc = zlib.crc32(blob) & 0xFFFFFFFF                                      # CRC-32 with the crc field still zero
    return blob[:12] + struct.pack("<I", crc) + blob[16:]

if __name__ == "__main__":
    pkg = make_package({"id": "tiny.app", "name": "Tiny App", "version": "1.0.0"}, {"type": "textview"}, b"Made without mkpkg!\n")
    open(sys.argv[1] if len(sys.argv) > 1 else "tiny.powor", "wb").write(pkg)
    print("wrote", len(pkg), "bytes")

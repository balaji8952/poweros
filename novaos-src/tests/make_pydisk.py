#!/usr/bin/env python3
"""Step 1 of the Python<->C disk cross-test: Python formats an image and fills it (apps, nested folders, a multi-cluster file)."""
import os, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from novadisk import NovaDisk
img = os.path.join(ROOT, "build", "pydisk.img")
if os.path.exists(img): os.remove(img)
d = NovaDisk(img, create=True, size=16 << 20)
for pk in ("a4.bin", "a5.bin", "a6.bin"):                       # Hello, Manual, Photos Shortcut (built by gen_assets.py)
    d.install(open(os.path.join(ROOT, "build", pk), "rb").read())
big = bytes((i * 31 + (i >> 8)) & 0xFF for i in range(100000))
open(os.path.join(ROOT, "build", "pydisk_big.bin"), "wb").write(big)
d.put(big, "/Documents/Projects/deep", "big.bin")
d.put(b"hello from python\n", "/Documents", "notes.txt")
d.put(b"", "/Documents", "empty.txt")
d.commit()
assert not d.check(), d.check()
print("python wrote", img, "- apps:", [m["id"] for _, m, _, _ in d.installed()])

#!/usr/bin/env python3
"""Step 3 of the cross-test: Python reads the image the C kernel code modified and checks every byte."""
import os, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from novadisk import NovaDisk
d = NovaDisk(os.path.join(ROOT, "build", "cdisk.img"))
bad = d.check()
assert not bad, "inconsistent after C writes: %s" % bad
ids = sorted(m["id"] for _, m, _, e in d.installed() if m)
assert ids == ["full.app", "hello.text", "novaos.manual"], ids
assert not [1 for _, m, _, e in d.installed() if e], "an installed file is invalid"
assert d.read_file(d.resolve("/Documents/from_c.txt")) == b"written by the C kernel code\n"
pat = bytes((i * 7 + 3) & 0xFF for i in range(300000))
assert d.read_file(d.resolve("/Documents/pattern.bin")) == pat
big = open(os.path.join(ROOT, "build", "pydisk_big.bin"), "rb").read()
assert d.read_file(d.resolve("/Documents/Projects/deep/big.bin")) == big          # untouched by C
assert d.read_file(d.resolve("/Documents/notes.txt")) == b"hello from python\n"
try:
    d.resolve("/System/Installed Applications/photos.link.power"); raise SystemExit("uninstalled app still on disk")
except Exception as e:
    assert "not found" in str(e)
print("python verified the C-modified image: consistent; apps %s; all file contents exact" % ids)

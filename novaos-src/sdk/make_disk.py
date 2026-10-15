#!/usr/bin/env python3
"""Creates (or updates) a NovaOS disk image and installs every package from examples/dist/ into it.
Close NovaOS / QEMU first - never edit an image while it is running.
Usage:  python make_disk.py [disk.img] [--size 64M] [--dir some/folder/with/packages]"""
import argparse, glob, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tools") if os.path.isdir(os.path.join(HERE, "tools", "mkpkg.py")) or os.path.exists(os.path.join(HERE, "tools", "mkpkg.py")) else os.path.join(HERE, "..", "tools"))
import mkpkg
from novadisk import NovaDisk, DiskError, parse_size

ap = argparse.ArgumentParser()
ap.add_argument("image", nargs="?", default="disk.img")
ap.add_argument("--size", default="64M")
ap.add_argument("--dir", default=os.path.join(HERE, "examples", "dist"))
a = ap.parse_args()
try:
    d = NovaDisk(a.image, create=True, size=parse_size(a.size))
    files = sorted(glob.glob(os.path.join(a.dir, "*.powor")) + glob.glob(os.path.join(a.dir, "*.appd")))
    if not files: sys.exit("no packages found in %s (run: python build_all.py)" % a.dir)
    for f in files:
        blob = open(f, "rb").read()
        new_id = mkpkg.validate(blob)[0]["id"]
        for k, m, _, _ in d.installed():                   # updating: replace an older copy of the same app
            if m and m["id"] == new_id: d.delete(k)
        meta = d.install(blob)
        print("installed %-18s %-22s v%s" % (meta["id"], meta["name"], meta["version"]))
    d.commit()
    problems = d.check()
    if problems: sys.exit("file system check failed: %s" % problems)
    print("\n%s is ready: %d apps installed, %d KB free. Boot NovaOS with it (see scripts/run-qemu.*)." % (a.image, len(d.installed()), d.free_clusters() * 4))
except (DiskError, mkpkg.PkgError) as e:
    sys.exit("error: %s" % e)

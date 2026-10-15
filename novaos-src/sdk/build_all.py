#!/usr/bin/env python3
"""Builds every project in examples/ (and any folder you add there) into examples/dist/, then validates the results
with the same rules the NovaOS kernel uses.     Usage:  python build_all.py"""
import glob, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tools") if os.path.isdir(os.path.join(HERE, "tools", "mkpkg.py")) or os.path.exists(os.path.join(HERE, "tools", "mkpkg.py")) else os.path.join(HERE, "..", "tools"))
import mkpkg

dist = os.path.join(HERE, "examples", "dist")
os.makedirs(dist, exist_ok=True)
for old in glob.glob(os.path.join(dist, "*.pow*")) + glob.glob(os.path.join(dist, "*.appd")):
    os.remove(old)
failed = 0
for manifest in sorted(glob.glob(os.path.join(HERE, "examples", "*", "app.manifest"))):
    proj = os.path.dirname(manifest); name = os.path.basename(proj)
    try:
        blob, app_id, ext = mkpkg.pack_project(proj, fix=True)
        out = os.path.join(dist, "%s.%s" % (name, ext))
        open(out, "wb").write(blob)
        print("built  %-34s %5d bytes   id=%s" % (os.path.relpath(out, HERE), len(blob), app_id))
    except (mkpkg.PkgError, OSError) as e:
        print("FAILED %s: %s" % (name, e)); failed += 1
sys.exit(1 if failed else 0)

#!/usr/bin/env python3
"""Turns build/assets.txt into build/assets_host.c so the native test harnesses can link the same asset table."""
import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
rows = [l.rstrip("\n").split("\t") for l in open("build/assets.txt")]
out = ['typedef unsigned char u8; typedef unsigned int u32;', 'struct AssetEnt { const char *name, *folder; const u8 *data; u32 size; };']
for i, (n, f, p) in enumerate(rows):
    out.append("static const u8 d%d[]={%s};" % (i, ",".join(map(str, open(p, "rb").read()))))
out.append("const struct AssetEnt asset_table[]={" + ",".join('{"%s","%s",d%d,sizeof d%d}' % (n, f, i, i) for i, (n, f, p) in enumerate(rows)) + "};")
out.append("const u32 asset_count=%d;" % len(rows))
open("build/assets_host.c", "w").write("\n".join(out))
print("build/assets_host.c: %d assets" % len(rows))

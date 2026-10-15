#!/usr/bin/env python3
"""patch_iso.py BASE.iso kernel.elf grub.cfg OUT.iso

Usage: use "-" in place of grub.cfg to keep the base ISO's menu untouched.
Fallback for machines without grub-mkrescue/xorriso: takes an existing NovaOS
(GRUB rescue) ISO and swaps in a new /boot/kernel.elf and /boot/grub/grub.cfg
by appending the new data and re-pointing their ISO9660 directory records."""
import struct, sys
SEC = 2048
def find_record(d, lba, size, parts):
    """return (lba,size) of the file at path parts, walking ISO9660 dirs (Rock Ridge names)"""
    data = d[lba*SEC:lba*SEC+size]; o = 0
    while o < len(data):
        l = data[o]
        if l == 0: o = (o//SEC+1)*SEC; continue
        nl = data[o+32]; name = data[o+33:o+33+nl]
        su = data[o+33+nl+(1-nl % 2):o+l]; nm = b''; j = 0
        while j+4 <= len(su):
            ln = su[j+2]
            if ln < 4: break
            if su[j:j+2] == b'NM': nm = su[j+5:j+ln]
            j += ln
        real = (nm or name.split(b';')[0]).decode(errors='replace').lower()
        if real == parts[0].lower():
            el, sz = struct.unpack_from('<I', data, o+2)[0], struct.unpack_from('<I', data, o+10)[0]
            return (el, sz) if len(parts) == 1 else find_record(d, el, sz, parts[1:])
        o += l
    raise SystemExit("not found: " + "/".join(parts))
def both32(v): return struct.pack('<I', v) + struct.pack('>I', v)

base, kern, cfg, out = sys.argv[1:5]
d = bytearray(open(base, 'rb').read())
assert d[0x8000+1:0x8000+6] == b'CD001'
root = d[0x8000+156:0x8000+190]
rl, rs = struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]
appended = False
for path, newfile in ((["boot", "kernel.elf"], kern), (["boot", "grub", "grub.cfg"], cfg)):
    if newfile == "-":                      # leave this file exactly as it is in the base ISO
        continue
    lba, size = find_record(d, rl, rs, path)
    new = open(newfile, 'rb').read()
    room = ((size + SEC - 1) // SEC) * SEC  # sectors the old file owns
    if len(new) <= room:                    # fits: overwrite in place, same layout as the base ISO
        d[lba*SEC:lba*SEC+room] = new + b'\0' * (room - len(new))
        nlba = lba
    else:                                   # too big: append at the end of the image
        d += b'\0' * ((-len(d)) % SEC)
        nlba = len(d)//SEC
        d += new + b'\0' * ((-len(new)) % SEC)
        appended = True
    old, repl = both32(lba) + both32(size), both32(nlba) + both32(len(new))
    assert d.count(old) >= 1
    d = d.replace(old, repl)                # directory record(s)
    print("%-22s lba %d -> %d (%s), %d -> %d bytes" % ("/".join(path), lba, nlba, "in place" if nlba == lba else "appended", size, len(new)))
if appended:
    d += b'\0' * ((-len(d)) % SEC)
    d[0x8000+80:0x8000+88] = both32(len(d)//SEC)
    if d[510:512] == b'\x55\xaa' and d[446+4] != 0:
        struct.pack_into('<I', d, 446+12, len(d)//512 - 1)
open(out, 'wb').write(d)
print("wrote", out, len(d), "bytes", "(same size as base)" if len(d) == len(open(base,'rb').read()) else "")

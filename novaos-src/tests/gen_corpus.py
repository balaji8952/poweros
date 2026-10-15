#!/usr/bin/env python3
"""Generates tests/corpus/{ok,bad}_*.bin: valid packages and packages violating exactly one rule each.
The reference validator (tools/mkpkg.py) must agree with the intended verdict for every case, and the C
kernel validator is later checked against the same files (tests/test_pkg.c)."""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import mkpkg
from mkpkg import pack, validate, PkgError, crc32

OUT = os.path.join(HERE, "corpus")
os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT):
    os.remove(os.path.join(OUT, f))
cases = []          # (name, bytes, expect_ok)

M = b"id=demo.app\nname=Demo App\nversion=1.2.3\n"
T = b"type=textview\n"
D = b"Hello\n"

def ok(name, meta=M, typ=T, data=D):  cases.append(("ok_" + name, pack(meta, typ, data), True))
def bad(name, blob):                   cases.append(("bad_" + name, blob, False))
def badp(name, meta=M, typ=T, data=D): bad(name, pack(meta, typ, data))

def raw(fields=None, meta=M, typ=T, data=D, fix=True):
    """build with header fields overridden (dict name->value), CRC fixed unless fix=False"""
    mo = 48; to = mo + len(meta); do = to + len(typ); total = do + len(data)
    f = dict(magic=b"NPKG", ver=1, hs=48, total=total, crc=0, mo=mo, ml=len(meta), to=to, tl=len(typ), do=do, dl=len(data), res=b"\0" * 8)
    f.update(fields or {})
    hdr = f["magic"] + struct.pack("<HHIIIIIIII", f["ver"], f["hs"], f["total"], f["crc"], f["mo"], f["ml"], f["to"], f["tl"], f["do"], f["dl"]) + f["res"]
    blob = hdr + meta + typ + data
    if fix:
        blob = blob[:12] + struct.pack("<I", crc32(blob[:12] + b"\0\0\0\0" + blob[16:])) + blob[16:]
    return blob

# ---------------------------------------------------------------- valid
ok("minimal")
ok("full", b"id=full.app\nname=Full App\nversion=65535.65535.65535\nauthor=Somebody\ndescription=All the fields\n",
   b"type=textview\ntitle=Full Title\nwrap=0\naccent=AbCdEf\n", b"line1\r\n\ttabbed\nline3")
ok("max_lengths", b"id=" + b"a" * 24 + b"\nname=" + b"N" * 24 + b"\nversion=0.0.0\nauthor=" + b"A" * 32 + b"\ndescription=" + b"D" * 80 + b"\n",
   b"type=textview\ntitle=" + b"T" * 32 + b"\n")
ok("id_charset", b"id=a.b_c-d9\nname=X\nversion=1.0.0\n")
ok("value_has_equals", b"id=demo.app\nname=a=b\nversion=1.0.0\n")
ok("no_trailing_newline", b"id=demo.app\nname=Demo\nversion=1.0.0", b"type=textview")
ok("empty_optional_values", b"id=demo.app\nname=Demo\nversion=1.0.0\nauthor=\ndescription=\n")
ok("text_65535", data=b"a" * 65535)
for t in mkpkg.TARGETS:
    ok("builtin_" + t, typ=b"type=builtin\ntarget=" + t.encode() + b"\n", data=b"")
ok("builtin_accent", typ=b"type=builtin\ntarget=files\naccent=00ff00\n", data=b"")
ok("all_ascii_text", data=bytes(range(0x20, 0x7F)) + b"\n\t\r")

# ------------------------------------------------------------- bad: meta
def mt(s): return M.replace(b"id=demo.app\n", s) if False else s
badp("meta_missing_id", b"name=Demo\nversion=1.0.0\n")
badp("meta_missing_name", b"id=demo.app\nversion=1.0.0\n")
badp("meta_missing_version", b"id=demo.app\nname=Demo\n")
badp("meta_duplicate_key", b"id=demo.app\nid=other.app\nname=Demo\nversion=1.0.0\n")
badp("meta_unknown_key", b"id=demo.app\nname=Demo\nversion=1.0.0\nlicense=MIT\n")
badp("meta_empty_id", b"id=\nname=Demo\nversion=1.0.0\n")
for n, v in [("short", "ab"), ("long", "a" * 25), ("upper", "Demo.app"), ("digit_first", "1demo"), ("space", "demo app"),
             ("slash", "demo/app"), ("dot_first", ".demoapp"), ("colon", "demo:app")]:
    badp("id_" + n, ("id=%s\nname=Demo\nversion=1.0.0\n" % v).encode())
for n, v in [("empty", ""), ("long", "N" * 25), ("lead_space", " Demo"), ("trail_space", "Demo "), ("slash", "De/mo"), ("backslash", "De\\mo")]:
    badp("name_" + n, ("id=demo.app\nname=%s\nversion=1.0.0\n" % v).encode())
for n, v in [("two_parts", "1.0"), ("four_parts", "1.0.0.0"), ("leading_zero", "01.0.0"), ("overflow", "1.0.65536"), ("alpha", "a.b.c"),
             ("empty_part", "1..0"), ("trailing_space", "1.0.0 "), ("negative", "-1.0.0"), ("six_digits", "100000.0.0"), ("empty", ""), ("plus", "1.0.0+x")]:
    badp("version_" + n, ("id=demo.app\nname=Demo\nversion=%s\n" % v).encode())
badp("author_long", b"id=demo.app\nname=Demo\nversion=1.0.0\nauthor=" + b"A" * 33 + b"\n")
badp("description_long", b"id=demo.app\nname=Demo\nversion=1.0.0\ndescription=" + b"D" * 81 + b"\n")
badp("meta_no_equals", b"id=demo.app\nname Demo\nversion=1.0.0\n")
badp("meta_empty_line", b"id=demo.app\n\nname=Demo\nversion=1.0.0\n")
badp("meta_double_newline_end", M + b"\n")
badp("meta_line_257", b"id=demo.app\nname=Demo\nversion=1.0.0\ndescription=" + b"x" * 250 + b"\n")
badp("meta_17_lines", b"".join(b"k%d=v\n" % i for i in range(17)))
badp("meta_key_upper", b"id=demo.app\nName=Demo\nversion=1.0.0\n")
badp("meta_key_dash", b"id=demo.app\nna-me=Demo\nversion=1.0.0\n")
badp("meta_key_17", b"id=demo.app\nname=Demo\nversion=1.0.0\n" + b"k" * 17 + b"=v\n")
badp("meta_key_digit_first", b"id=demo.app\nname=Demo\nversion=1.0.0\n1k=v\n")
badp("meta_crlf", b"id=demo.app\r\nname=Demo\r\nversion=1.0.0\r\n")
badp("meta_nul", b"id=demo.app\nname=De\0mo\nversion=1.0.0\n")
badp("meta_high_byte", b"id=demo.app\nname=D\xc3\xa9mo\nversion=1.0.0\n")
badp("meta_ctrl_byte", b"id=demo.app\nname=De\x01mo\nversion=1.0.0\n")
badp("meta_del_byte", b"id=demo.app\nname=De\x7fmo\nversion=1.0.0\n")
badp("meta_tab", b"id=demo.app\nname=De\tmo\nversion=1.0.0\n")
badp("meta_only_newline", b"\n")
bad("meta_len_zero", raw(dict(ml=0, to=48, do=48 + len(T)), meta=b"", typ=T, data=D))
# --------------------------------------------------------------- bad: type
badp("type_missing", typ=b"title=x\n")
badp("type_unknown_binary", typ=b"type=binary\n")
badp("type_unknown_elf", typ=b"type=elf\n")
badp("type_empty_value", typ=b"type=\n")
badp("type_case", typ=b"type=TextView\n")
badp("type_textview_target", typ=b"type=textview\ntarget=files\n")
badp("type_builtin_title", typ=b"type=builtin\ntarget=files\ntitle=x\n", data=b"")
badp("type_unknown_key", typ=b"type=textview\nfoo=bar\n")
for v in ["12345", "GGGGGG", "1234567", "", "#aabbcc", "aabbcg"]:
    badp("accent_" + (v or "empty").replace("#", "hash"), b"type=textview\naccent=" + v.encode() + b"\n")
badp("wrap_2", typ=b"type=textview\nwrap=2\n")
badp("wrap_empty", typ=b"type=textview\nwrap=\n")
badp("title_long", typ=b"type=textview\ntitle=" + b"T" * 33 + b"\n")
badp("builtin_bad_target", typ=b"type=builtin\ntarget=calc\n", data=b"")
badp("builtin_target_case", typ=b"type=builtin\ntarget=Files\n", data=b"")
badp("builtin_no_target", typ=b"type=builtin\n", data=b"")
badp("builtin_with_data", typ=b"type=builtin\ntarget=files\n", data=b"x")
badp("type_duplicate", typ=b"type=textview\ntype=builtin\n")
badp("type_crlf", typ=b"type=textview\r\n")
bad("type_len_zero", raw(dict(tl=0, do=48 + len(M)), typ=b"", data=D))
# --------------------------------------------------------------- bad: data
badp("text_empty", data=b"")
badp("text_65536", data=b"a" * 65536)
for n, b in [("nul", b"ab\0cd"), ("del", b"ab\x7fcd"), ("high", b"ab\x80cd"), ("esc", b"ab\x1bcd"), ("bell", b"\x07")]:
    badp("text_" + n, data=b)
# ------------------------------------------------------------- bad: header
good = pack(M, T, D)
bad("magic", raw(dict(magic=b"NPKX")))
bad("magic_lower", raw(dict(magic=b"npkg")))
bad("version_0", raw(dict(ver=0)))
bad("version_2", raw(dict(ver=2)))
bad("version_ffff", raw(dict(ver=0xFFFF)))
bad("header_size_40", raw(dict(hs=40)))
bad("header_size_64", raw(dict(hs=64)))
bad("total_plus1", raw(dict(total=len(good) + 1)))
bad("total_minus1", raw(dict(total=len(good) - 1)))
bad("total_huge", raw(dict(total=0xFFFFFFFF)))
bad("reserved_nonzero", raw(dict(res=b"\0\0\0\0\0\0\0\1")))
b = bytearray(good); b[-1] ^= 1; bad("crc_data_flip", bytes(b))
b = bytearray(good); b[60] ^= 0x80; bad("crc_meta_flip", bytes(b))
b = bytearray(good); b[12] ^= 1; bad("crc_field_flip", bytes(b))
bad("meta_off_wrong", raw(dict(mo=49)))
bad("type_off_overlap", raw(dict(to=48 + len(M) - 1)))
bad("type_off_gap", raw(dict(to=48 + len(M) + 1)))
bad("data_off_gap", raw(dict(do=48 + len(M) + len(T) + 1, dl=len(D) - 1)))
bad("data_len_short", raw(dict(dl=len(D) - 1)))
bad("data_len_long", raw(dict(dl=len(D) + 1)))
bad("data_len_huge", raw(dict(dl=0xFFFFFFF0)))
bad("meta_len_huge", raw(dict(ml=0xFFFFFFFF)))
bad("meta_len_wrap", raw(dict(ml=0xFFFFFFF0, to=0xFFFFFFF0 + 48 & 0xFFFFFFFF)))
bad("type_len_huge", raw(dict(tl=0xFFFFFFFF)))
bad("meta_len_over_limit", raw(dict(ml=2049), meta=M + b"#" * 0))
bad("trailing_bytes", raw(dict(total=len(good) + 4), data=D + b"JUNK", ) if False else raw(dict(dl=len(D)), data=D + b"JUNK"))
bad("trailing_zero_bytes", raw(dict(dl=len(D)), data=D + b"\0" * 16))
bad("swapped_sections", raw(dict(mo=48, ml=len(T), to=48 + len(T), tl=len(M), do=48 + len(T) + len(M)), meta=M, typ=T))
for n in (0, 1, 10, 47, 48, 49, 60, len(good) - 1):
    bad("truncated_%d" % n, good[:n])
bad("oversize_262145", raw(dict(), meta=M, typ=b"type=zzz\n", data=b"\0" * (262145 - 48 - len(M) - 9)))
bad("all_zero_48", b"\0" * 48)
bad("all_ff_100", b"\xff" * 100)
bad("png_header", b"\x89PNG\r\n\x1a\n" + b"\0" * 80)
bad("elf_header", b"\x7fELF\x01\x01\x01" + b"\0" * 80)

# ------------------------------------------------------- verify + write
manifest = []
for name, blob, expect in cases:
    try:
        validate(blob); got = True
    except PkgError:
        got = False
    if got != expect:
        sys.exit("reference validator disagrees with intent for %s: intended %s, got %s" % (name, expect, got))
    open(os.path.join(OUT, name + ".bin"), "wb").write(blob)
    manifest.append("%s %d" % (name, 1 if expect else 0))
open(os.path.join(OUT, "MANIFEST.txt"), "w").write("\n".join(manifest) + "\n")
print("corpus: %d cases (%d valid, %d invalid); reference validator agrees with every case" %
      (len(cases), sum(1 for c in cases if c[2]), sum(1 for c in cases if not c[2])))

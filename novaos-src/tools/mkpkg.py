#!/usr/bin/env python3
"""NovaOS package tool (format: docs/NOVAPKG_FORMAT.md)

  mkpkg.py build  OUT.powor --id ID --name NAME --version 1.0.0 --type textview --data FILE [--author A] [--description D] [--title T] [--wrap 0|1] [--accent RRGGBB]
  mkpkg.py build  OUT.appd  --id ID --name NAME --version 1.0.0 --type builtin  --target files
  mkpkg.py check  FILE...        validate with the same rules as the kernel
"""
import argparse, glob, os, re, struct, sys, zlib

MAGIC, FMT, HDR = b"NPKG", 1, 48
MAX_PKG, MAX_META, MAX_TYPE, MAX_DATA, MAX_TEXT = 262144, 2048, 1024, 196608, 65535
TARGETS = ("files", "notepad", "photos", "display", "about")
PRINT = set(range(0x20, 0x7F))


class PkgError(Exception):
    pass


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def pack(meta: bytes, typ: bytes, data: bytes, fix_crc=True) -> bytes:
    mo = HDR; to = mo + len(meta); do = to + len(typ); total = do + len(data)
    hdr = MAGIC + struct.pack("<HHIIIIIIII", FMT, HDR, total, 0, mo, len(meta), to, len(typ), do, len(data)) + b"\0" * 8
    blob = hdr + meta + typ + data
    if fix_crc:
        blob = blob[:12] + struct.pack("<I", crc32(blob)) + blob[16:]
    return blob


def kv_parse(sec: bytes, what: str):
    if not sec: raise PkgError(f"{what}: empty")
    if any(c != 10 and c not in PRINT for c in sec): raise PkgError(f"{what}: invalid byte")
    text = sec.decode("ascii")
    if text.endswith("\n"): text = text[:-1]
    lines = text.split("\n")
    if len(lines) > 16: raise PkgError(f"{what}: too many lines")
    out = {}
    for ln in lines:
        if not ln or len(ln) > 256 or "=" not in ln: raise PkgError(f"{what}: bad line")
        k, v = ln.split("=", 1)
        if not re.fullmatch(r"[a-z][a-z0-9_]{0,15}", k): raise PkgError(f"{what}: bad key")
        if k in out: raise PkgError(f"{what}: key '{k}' appears twice")
        out[k] = v
    return out


def only(d, allowed, what):
    for k in d:
        if k not in allowed: raise PkgError(f"{what}: unknown key '{k}' (see docs/05-reference.md for the allowed keys)")


def validate(buf: bytes):
    if len(buf) < HDR or len(buf) > MAX_PKG: raise PkgError("this is not a package: size must be between 48 bytes and 256 KB (is it really a .powor/.appd file?)")
    if buf[:4] != MAGIC: raise PkgError("this is not a NovaOS package (missing NPKG signature)")
    ver, hs, total, crc, mo, ml, to, tl, do, dl = struct.unpack_from("<HHIIIIIIII", buf, 4)
    if ver != FMT: raise PkgError("unsupported package format version (this NovaOS understands v1)")
    if hs != HDR or total != len(buf) or buf[40:48] != b"\0" * 8: raise PkgError("damaged package header (header size, total size or reserved bytes are wrong)")
    if crc32(buf[:12] + b"\0\0\0\0" + buf[16:]) != crc: raise PkgError("checksum mismatch: the package is corrupted or was edited after building - rebuild it with mkpkg")
    if mo != HDR or to != mo + ml or do != to + tl or do + dl != total: raise PkgError("damaged package layout: META/TYPE/DATA sections are not contiguous or overlap")
    if not (1 <= ml <= MAX_META) or not (1 <= tl <= MAX_TYPE) or dl > MAX_DATA: raise PkgError("a section is empty or too large (META 1-2048, TYPE 1-1024, DATA up to 196608 bytes)")
    meta = kv_parse(buf[mo:to], "meta"); typ = kv_parse(buf[to:do], "type"); data = buf[do:do + dl]
    only(meta, ("id", "name", "version", "author", "description"), "meta")
    for k in ("id", "name", "version"):
        if k not in meta: raise PkgError(f"meta: required key '{k}' is missing")
    if not re.fullmatch(r"[a-z][a-z0-9._-]{2,23}", meta["id"]): raise PkgError("meta: id must be 3-24 characters of a-z 0-9 . _ - and start with a letter (e.g. my.notes)")
    n = meta["name"]
    if not (1 <= len(n) <= 24) or "/" in n or "\\" in n or n != n.strip(" "): raise PkgError("meta: name must be 1-24 printable characters, no / or \\, no space at either end")
    if not re.fullmatch(r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})", meta["version"]) \
            or any(int(x) > 65535 for x in meta["version"].split(".")): raise PkgError("meta: version must look like 1.0.0 (three numbers 0-65535, no leading zeros)")
    if len(meta.get("author", "")) > 32 or len(meta.get("description", "")) > 80: raise PkgError("meta: author is limited to 32 characters and description to 80")
    t = typ.get("type")
    if t is None: raise PkgError("type: the TYPE section needs a type=... line (textview or builtin)")
    if "accent" in typ and not re.fullmatch(r"[0-9a-fA-F]{6}", typ["accent"]): raise PkgError("type: accent must be exactly 6 hex digits, e.g. 2F6FED (no #)")
    if t == "textview":
        only(typ, ("type", "title", "wrap", "accent"), "type")
        if len(typ.get("title", "")) > 32: raise PkgError("type: title is limited to 32 characters")
        if typ.get("wrap", "1") not in ("0", "1"): raise PkgError("type: wrap must be 0 or 1")
        if not (1 <= dl <= MAX_TEXT): raise PkgError("data: a textview app needs between 1 and 65535 bytes of text")
        for i, c in enumerate(data):
            if c not in PRINT and c not in (9, 10, 13):
                line = data[:i].count(b"\n") + 1; col = i - (data.rfind(b"\n", 0, i) + 1) + 1
                raise PkgError("data: byte 0x%02X at line %d, column %d is not plain ASCII text (use --fix-text to convert)" % (c, line, col))
    elif t == "builtin":
        only(typ, ("type", "target", "accent"), "type")
        if typ.get("target") not in TARGETS: raise PkgError("type: builtin needs target=files|notepad|photos|display|about")
        if dl != 0: raise PkgError("data: a builtin shortcut must not have any data")
    else:
        raise PkgError("unsupported application type (this NovaOS supports: textview, builtin)")
    return meta, typ, data


def build(meta: dict, typ: dict, data: bytes) -> bytes:
    m = "".join(f"{k}={v}\n" for k, v in meta.items()).encode()
    t = "".join(f"{k}={v}\n" for k, v in typ.items()).encode()
    blob = pack(m, t, data)
    validate(blob)
    return blob


# ---- helpers for authors -------------------------------------------------
_FOLD = {"\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"', "\u2013": "-", "\u2014": "--", "\u2026": "...",
         "\u00a0": " ", "\u2022": "*", "\u00b7": "*", "\u2192": "->", "\u2190": "<-", "\u00a9": "(c)", "\u00ae": "(R)", "\ufeff": ""}


def fix_text(raw: bytes):
    """Make arbitrary UTF-8 text acceptable to the textview type: BOM/CRLF handling, punctuation -> ASCII,
    anything else non-ASCII -> '?'.  Returns (bytes, number_of_replaced_characters)."""
    txt = raw.decode("utf-8", "replace").replace("\r\n", "\n").replace("\r", "\n")
    out, repl = [], 0
    for ch in txt:
        if ch in _FOLD: out.append(_FOLD[ch]); repl += bool(_FOLD[ch] or ch != "\ufeff")
        elif ch in "\n\t" or 0x20 <= ord(ch) <= 0x7E: out.append(ch)
        else: out.append("?"); repl += 1
    return "".join(out).encode("ascii"), repl


PROJECT_KEYS = {"id", "name", "version", "author", "description", "type", "title", "wrap", "accent", "target", "data", "output"}


def read_project(path: str):
    """A project is a folder with app.manifest (key=value, '#' comments) and, for textview, a data file."""
    mf = os.path.join(path, "app.manifest") if os.path.isdir(path) else path
    base = os.path.dirname(os.path.abspath(mf))
    cfg = {}
    for n, ln in enumerate(open(mf, encoding="utf-8").read().splitlines(), 1):
        ln = ln.strip()
        if not ln or ln.startswith("#"): continue
        if "=" not in ln: raise PkgError(f"{mf}:{n}: expected key=value")
        k, v = ln.split("=", 1); k = k.strip(); v = v.strip()
        if k not in PROJECT_KEYS: raise PkgError(f"{mf}:{n}: unknown key '{k}' (allowed: {', '.join(sorted(PROJECT_KEYS))})")
        if k in cfg: raise PkgError(f"{mf}:{n}: duplicate key '{k}'")
        cfg[k] = v
    return cfg, base


def pack_project(path: str, fix=False):
    cfg, base = read_project(path)
    meta = {k: cfg[k] for k in ("id", "name", "version", "author", "description") if cfg.get(k)}
    typ = {k: cfg[k] for k in ("type", "title", "wrap", "target", "accent") if cfg.get(k)}
    data = b""
    if cfg.get("data"):
        data = open(os.path.join(base, cfg["data"]), "rb").read()
        if fix: data, n = fix_text(data); (n and print(f"  --fix-text: replaced {n} character(s)"))
    ext = cfg.get("output", "powor").lstrip(".")
    if ext not in ("powor", "appd"): raise PkgError("output must be powor or appd")
    return build(meta, typ, data), meta["id"] if "id" in meta else "app", ext


def describe(buf: bytes) -> str:
    m, t, d = validate(buf)
    lines = ["Package OK  (%d bytes, format v%d)" % (len(buf), FMT), "  [META]"]
    lines += ["    %-12s %s" % (k, v) for k, v in m.items()]
    lines += ["  [TYPE]"] + ["    %-12s %s" % (k, v) for k, v in t.items()]
    lines += ["  [DATA]  %d bytes" % len(d)]
    if t["type"] == "textview":
        txt = d.decode("ascii").replace("\r", "")
        lines.append("    %d lines, longest %d chars; first line: %r" % (txt.count("\n") + 1, max(map(len, txt.split("\n"))), txt.split("\n")[0][:60]))
    lines.append("  installs as: /System/Installed Applications/%s.power" % m["id"])
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(prog="mkpkg", description="Build / check / inspect NovaOS packages (.powor / .appd)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack", help="build a package from a project folder (app.manifest)")
    p.add_argument("project"); p.add_argument("-o", "--out", help="output file or folder (default: <id>.<output> next to the project)")
    p.add_argument("--fix-text", action="store_true", help="convert curly quotes, dashes, CRLF etc. to plain ASCII")
    b = sub.add_parser("build", help="build from command-line options (no project folder)"); b.add_argument("out")
    for a in ("id", "name", "version", "type"): b.add_argument("--" + a, required=True)
    for a in ("author", "description", "data", "title", "wrap", "accent", "target"): b.add_argument("--" + a)
    c = sub.add_parser("check", help="validate packages with the same rules as the kernel"); c.add_argument("files", nargs="+")
    i = sub.add_parser("inspect", help="validate and describe a package"); i.add_argument("file")
    a = ap.parse_args()
    try:
        if a.cmd == "pack":
            blob, pid, ext = pack_project(a.project, a.fix_text)
            out = a.out or os.path.join(os.path.dirname(os.path.abspath(a.project if os.path.isfile(a.project) else a.project)), "%s.%s" % (pid, ext))
            if os.path.isdir(out): out = os.path.join(out, "%s.%s" % (pid, ext))
            open(out, "wb").write(blob); print("wrote %s (%d bytes)" % (out, len(blob)))
        elif a.cmd == "build":
            meta = {"id": a.id, "name": a.name, "version": a.version}
            if a.author: meta["author"] = a.author
            if a.description: meta["description"] = a.description
            typ = {"type": a.type}
            for k in ("title", "wrap", "target", "accent"):
                if getattr(a, k): typ[k] = getattr(a, k)
            data = open(a.data, "rb").read() if a.data else b""
            open(a.out, "wb").write(build(meta, typ, data)); print("wrote", a.out)
        elif a.cmd == "inspect":
            print(describe(open(a.file, "rb").read()))
        else:
            rc = 0
            files = []
            for pat in a.files: files += sorted(glob.glob(pat)) if any(ch in pat for ch in "*?[") else [pat]
            for f in files:
                try:
                    m, t, d = validate(open(f, "rb").read()); print(f"OK   {f}: {m['id']} {m['version']} type={t['type']} data={len(d)}B")
                except PkgError as e:
                    print(f"FAIL {f}: {e}"); rc = 1
            sys.exit(rc)
    except PkgError as e:
        sys.exit("error: %s" % e)
    except OSError as e:
        sys.exit("error: %s" % e)


if __name__ == "__main__":
    main()

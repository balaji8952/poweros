#!/usr/bin/env python3
"""novadisk - create and edit NovaOS disk images (NovaFS) from your PC.   Python 3.8+, standard library only.

NovaOS has no network or USB, so this is how files and apps get in: edit disk.img while NovaOS is NOT running,
then boot NovaOS with  -hda disk.img.

  novadisk.py create  disk.img [--size 64M]        make (or format a blank) image
  novadisk.py install disk.img app.powor [...]     validate + install apps (-> /System/Installed Applications/<id>.power)
  novadisk.py apps    disk.img                     list installed apps
  novadisk.py uninstall disk.img <app-id>          remove an installed app
  novadisk.py ls      disk.img [/path]             list a folder
  novadisk.py put     disk.img local.file /Documents [--name NAME]
  novadisk.py get     disk.img /Documents/x.txt local.file
  novadisk.py mkdir   disk.img /Documents/Projects
  novadisk.py rm      disk.img /Documents/old.txt   (folders are removed recursively)
  novadisk.py check   disk.img                     verify file-system consistency
  novadisk.py info    disk.img

The on-disk layout is the one in NovaOS's fs.h: sector 0 superblock "NOVAFS01"; 128 sectors of node table
(1024 x 64 bytes); a FAT of u32 per 4 KB cluster; then the data clusters.  At most the first 256 MB is used.
"""
import argparse, glob, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkpkg  # package validation (same rules as the kernel)

SEC, NODE_SECS, MAXNODES, NAME_MAX = 512, 128, 1024, 31
CL_SECS, CL_BYTES, MAX_CL, FAT_END = 8, 4096, 65536, 0xFFFFFFFF
MAX_CAP_SECTORS = 524288
APPS_PATH = ("System", "Installed Applications")


class DiskError(Exception):
    pass


def parse_size(s):
    s = s.strip().upper()
    mult = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}
    return int(float(s[:-1]) * mult[s[-1]]) if s[-1] in mult else int(s)


def geometry(total_sectors):
    """identical to fs_format() in fs.h"""
    cap = min(total_sectors, MAX_CAP_SECTORS)
    if cap < 2048:
        raise DiskError("image too small (need at least 1 MB)")
    ncl, dlba = (cap - 200) // CL_SECS, 0
    for _ in range(3):
        ncl = min(ncl, MAX_CL)
        fsecs = (ncl + 1 + 127) // 128
        dlba = (1 + NODE_SECS + fsecs + 7) & ~7
        ncl = (cap - dlba) // CL_SECS
    ncl = min(ncl, MAX_CL)
    return cap, ncl, (ncl + 1 + 127) // 128, 1 + NODE_SECS, dlba


class NovaDisk:
    def __init__(self, path, create=False, size=None, allow_format=False):
        self.path = path
        exists = os.path.exists(path)
        if not exists:
            if not create:
                raise DiskError("%s does not exist (create it with:  novadisk.py create %s)" % (path, path))
            with open(path, "wb") as f:
                f.truncate(size or (64 << 20))
        elif create and size and os.path.getsize(path) < size:
            with open(path, "r+b") as f:
                f.truncate(size)
        self.f = open(path, "r+b")
        self.total = os.path.getsize(path) // SEC
        sb = self._read(0, 1)
        if sb[:8] == b"NOVAFS01":
            self._mount(sb)
        else:
            head = self._read(0, 64)
            if any(head):
                if not allow_format:
                    raise DiskError("%s contains data that is not a NovaOS disk; refusing to touch it (use 'create --force' to erase it)" % path)
            if not (create or allow_format):
                raise DiskError("%s is blank / not formatted - run:  novadisk.py create %s" % (path, path))
            self._format()

    # -- raw I/O --
    def _read(self, lba, n):
        self.f.seek(lba * SEC); return self.f.read(n * SEC).ljust(n * SEC, b"\0")

    def _write(self, lba, data):
        self.f.seek(lba * SEC); self.f.write(data)

    # -- format / mount --
    def _format(self):
        cap, ncl, fsecs, flba, dlba = geometry(self.total)
        self.ncl, self.fat_secs, self.fat_lba, self.data_lba, self.cap = ncl, fsecs, flba, dlba, cap
        self.fat = [0] * (fsecs * 128)
        self.nodes = [dict(used=0, dir=0, parent=0, size=0, first=0, name="") for _ in range(MAXNODES)]
        self.nodes[0] = dict(used=1, dir=1, parent=0xFFFFFFFF, size=0, first=0, name="")
        self.f.seek(0); self.f.write(b"\0" * (dlba * SEC))
        sb = bytearray(512); sb[:8] = b"NOVAFS01"
        struct.pack_into("<IIIIIII", sb, 8, 1, cap, ncl, 1, flba, fsecs, dlba)
        self._write(0, bytes(sb))
        for n in ("Desktop", "Documents", "Pictures"):
            self.mkdir("/" + n)
        self.commit()

    def _mount(self, sb):
        ver, cap, ncl, nlba, flba, fsecs, dlba = struct.unpack_from("<IIIIIII", sb, 8)
        if (ver != 1 or nlba != 1 or ncl < 8 or ncl > MAX_CL or flba != 1 + NODE_SECS or fsecs != (ncl + 1 + 127) // 128
                or dlba < flba + fsecs or dlba + ncl * CL_SECS > self.total):
            raise DiskError("%s has a damaged or unsupported NovaFS superblock" % self.path)
        self.ncl, self.fat_secs, self.fat_lba, self.data_lba, self.cap = ncl, fsecs, flba, dlba, cap
        raw = self._read(1, NODE_SECS)
        self.nodes = []
        for i in range(MAXNODES):
            e = raw[i * 64:(i + 1) * 64]
            nm = e[16:48].split(b"\0")[0].decode("latin-1")
            self.nodes.append(dict(used=1 if e[0] else 0, dir=1 if e[1] else 0, parent=struct.unpack_from("<I", e, 4)[0],
                                   size=struct.unpack_from("<I", e, 8)[0], first=struct.unpack_from("<I", e, 12)[0], name=nm))
        if not (self.nodes[0]["used"] and self.nodes[0]["dir"]):
            raise DiskError("%s: root folder missing (corrupt)" % self.path)
        self.fat = list(struct.unpack("<%dI" % (fsecs * 128), self._read(flba, fsecs)))

    def commit(self):
        """data was written as we went; now FAT, then the node table (same order NovaOS uses)"""
        self._write(self.fat_lba, struct.pack("<%dI" % len(self.fat), *self.fat))
        raw = bytearray()
        for n in self.nodes:
            e = bytearray(64)
            e[0], e[1] = n["used"], n["dir"]
            struct.pack_into("<III", e, 4, n["parent"] & 0xFFFFFFFF, n["size"], n["first"])
            e[16:16 + len(n["name"])] = n["name"].encode("latin-1")
            raw += e
        self._write(1, bytes(raw))
        self.f.flush(); os.fsync(self.f.fileno())

    # -- names / paths --
    @staticmethod
    def valid_name(name):
        return (1 <= len(name) <= NAME_MAX and name == name.strip(" ") and name not in (".", "..")
                and all(32 <= ord(c) <= 126 and c not in "/\\" for c in name))

    def lookup(self, parent, name):
        for i in range(1, MAXNODES):
            n = self.nodes[i]
            if n["used"] and n["parent"] == parent and n["name"].lower() == name.lower():
                return i
        return -1

    def resolve(self, path):
        idx = 0
        for part in [p for p in path.split("/") if p]:
            if not self.nodes[idx]["dir"]:
                raise DiskError("not a folder in path: " + path)
            idx = self.lookup(idx, part)
            if idx < 0:
                raise DiskError("not found: " + path)
        return idx

    def children(self, idx):
        kids = [i for i in range(1, MAXNODES) if self.nodes[i]["used"] and self.nodes[i]["parent"] == idx]
        return sorted(kids, key=lambda i: (-self.nodes[i]["dir"], self.nodes[i]["name"].lower()))

    def path_of(self, idx):
        parts = []
        while idx > 0:
            parts.append(self.nodes[idx]["name"]); idx = self.nodes[idx]["parent"]
        return "/" + "/".join(reversed(parts))

    # -- operations --
    def new_node(self, parent, name, is_dir):
        if not self.valid_name(name):
            raise DiskError("invalid name %r (1-31 printable ASCII chars, no / or \\, no leading/trailing space)" % name)
        if self.lookup(parent, name) >= 0:
            raise DiskError("%r already exists in %s" % (name, self.path_of(parent)))
        for i in range(1, MAXNODES):
            if not self.nodes[i]["used"]:
                self.nodes[i] = dict(used=1, dir=1 if is_dir else 0, parent=parent, size=0, first=0, name=name)
                return i
        raise DiskError("file table is full (1024 entries)")

    def mkdir(self, path, parents=True):
        idx = 0
        for part in [p for p in path.split("/") if p]:
            nx = self.lookup(idx, part)
            if nx < 0:
                nx = self.new_node(idx, part, True)
            elif not self.nodes[nx]["dir"]:
                raise DiskError("%s exists and is a file" % part)
            idx = nx
        return idx

    def free_clusters(self):
        return sum(1 for c in range(1, self.ncl + 1) if self.fat[c] == 0)

    def _free_chain(self, c):
        guard = 0
        while c and c != FAT_END and c <= self.ncl and guard <= self.ncl:
            nx = self.fat[c]; self.fat[c] = 0; c = nx; guard += 1

    def write_file(self, idx, data):
        need = (len(data) + CL_BYTES - 1) // CL_BYTES
        free = [c for c in range(1, self.ncl + 1) if self.fat[c] == 0]
        if len(free) < need:
            raise DiskError("disk full: need %d KB, only %d KB free" % (need * 4, len(free) * 4))
        old = self.nodes[idx]["first"]
        chain = free[:need]
        for k, c in enumerate(chain):
            self.fat[c] = chain[k + 1] if k + 1 < need else FAT_END
            chunk = data[k * CL_BYTES:(k + 1) * CL_BYTES]
            self._write(self.data_lba + (c - 1) * CL_SECS, chunk.ljust(CL_BYTES, b"\0"))
        self.nodes[idx]["first"] = chain[0] if chain else 0
        self.nodes[idx]["size"] = len(data)
        self._free_chain(old)

    def read_file(self, idx):
        n = self.nodes[idx]
        if n["dir"]:
            raise DiskError("%s is a folder" % self.path_of(idx))
        out, c = bytearray(), n["first"]
        while len(out) < n["size"]:
            if not c or c == FAT_END or c > self.ncl:
                raise DiskError("%s: broken cluster chain" % self.path_of(idx))
            out += self._read(self.data_lba + (c - 1) * CL_SECS, CL_SECS)
            c = self.fat[c]
        return bytes(out[:n["size"]])

    def delete(self, idx):
        if idx <= 0:
            raise DiskError("cannot delete the root folder")
        for k in self.children(idx):
            self.delete(k)
        self._free_chain(self.nodes[idx]["first"])
        self.nodes[idx] = dict(used=0, dir=0, parent=0, size=0, first=0, name="")

    def put(self, data, dest_dir, name):
        d = self.mkdir(dest_dir)
        idx = self.new_node(d, name, False)
        try:
            self.write_file(idx, data)
        except DiskError:
            self.delete(idx); raise
        return idx

    # -- apps --
    def apps_dir(self, create=False):
        idx = self.mkdir("/" + "/".join(APPS_PATH)) if create else None
        if idx is None:
            try: idx = self.resolve("/" + "/".join(APPS_PATH))
            except DiskError: return -1
        return idx

    def installed(self):
        a = self.apps_dir()
        res = []
        if a < 0: return res
        for k in self.children(a):
            n = self.nodes[k]
            if n["dir"] or not n["name"].lower().endswith(".power"): continue
            try:
                meta, typ, _ = mkpkg.validate(self.read_file(k)); res.append((k, meta, typ, None))
            except (mkpkg.PkgError, DiskError) as e:
                res.append((k, None, None, str(e)))
        return res

    def install(self, blob):
        meta, typ, _ = mkpkg.validate(blob)            # raises PkgError with the exact rule that failed
        for _, m, _, _ in self.installed():
            if m and m["id"] == meta["id"]:
                raise DiskError("an app with id '%s' is already installed (version %s) - uninstall it first" % (meta["id"], m["version"]))
        a = self.apps_dir(create=True)
        fname = meta["id"] + ".power"
        if self.lookup(a, fname) >= 0:
            raise DiskError("a file named %s already exists in Installed Applications" % fname)
        if len(self.children(a)) >= 32:
            raise DiskError("too many installed apps (limit 32)")
        idx = self.new_node(a, fname, False)
        try:
            self.write_file(idx, blob)
        except DiskError:
            self.delete(idx); raise
        if self.read_file(idx) != blob:                 # read-after-write check
            self.delete(idx); raise DiskError("verification failed - nothing installed")
        return meta

    # -- consistency check (same invariants as the kernel's test suite) --
    def check(self):
        problems, seen, used = [], set(), 0
        for i in range(1, MAXNODES):
            n = self.nodes[i]
            if not n["used"]: continue
            if n["parent"] >= MAXNODES or not self.nodes[n["parent"]]["used"] or not self.nodes[n["parent"]]["dir"]:
                problems.append("node %d (%s) has no valid parent" % (i, n["name"]))
            if n["dir"]: continue
            need = (n["size"] + CL_BYTES - 1) // CL_BYTES
            c, got = n["first"], 0
            while c and c != FAT_END and c <= self.ncl and got <= need + 1:
                if c in seen: problems.append("cluster %d shared (%s)" % (c, n["name"])); break
                seen.add(c); got += 1; c = self.fat[c]
            if got != need or (need and c != FAT_END):
                problems.append("%s: chain length %d, expected %d" % (self.path_of(i), got, need))
            used += got
        nz = sum(1 for c in range(1, self.ncl + 1) if self.fat[c])
        if nz != used:
            problems.append("FAT has %d used clusters but files own %d (leak)" % (nz, used))
        return problems


# ------------------------------------------------------------------ CLI
def main():
    ap = argparse.ArgumentParser(prog="novadisk", description="NovaOS disk image tool (close NovaOS/QEMU before editing an image)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("create"); c.add_argument("image"); c.add_argument("--size", default="64M"); c.add_argument("--force", action="store_true", help="erase a non-blank image")
    for name in ("apps", "check", "info"): sub.add_parser(name).add_argument("image")
    i = sub.add_parser("install"); i.add_argument("image"); i.add_argument("packages", nargs="+", help="files or wildcards (*.powor)")
    i.add_argument("--update", action="store_true", help="replace an installed app that has the same id")
    u = sub.add_parser("uninstall"); u.add_argument("image"); u.add_argument("app_id")
    l = sub.add_parser("ls"); l.add_argument("image"); l.add_argument("path", nargs="?", default="/")
    p = sub.add_parser("put"); p.add_argument("image"); p.add_argument("local"); p.add_argument("dest", nargs="?", default="/Documents"); p.add_argument("--name")
    g = sub.add_parser("get"); g.add_argument("image"); g.add_argument("path"); g.add_argument("local")
    m = sub.add_parser("mkdir"); m.add_argument("image"); m.add_argument("path")
    r = sub.add_parser("rm"); r.add_argument("image"); r.add_argument("path")
    a = ap.parse_args()
    try:
        if a.cmd == "create":
            if a.force and os.path.exists(a.image):
                with open(a.image, "r+b") as f: f.write(b"\0" * (1 << 20))
            d = NovaDisk(a.image, create=True, size=parse_size(a.size))
            print("%s ready: %d MB image, %d KB free" % (a.image, os.path.getsize(a.image) >> 20, d.free_clusters() * 4))
            return
        d = NovaDisk(a.image)
        if a.cmd == "install":
            files = []
            for pat in a.packages:
                hit = sorted(glob.glob(pat)) if any(c in pat for c in "*?[") else [pat]
                if not hit: raise DiskError("no files match " + pat)
                files += hit
            for pk in files:
                blob = open(pk, "rb").read()
                if a.update:
                    try: new_id = mkpkg.validate(blob)[0]["id"]
                    except mkpkg.PkgError: new_id = None
                    for k, m, _, _ in d.installed():
                        if m and m["id"] == new_id: d.delete(k); print("  (replacing installed %s v%s)" % (m["id"], m["version"]))
                meta = d.install(blob)
                print("installed %-16s %-24s v%s  -> /System/Installed Applications/%s.power" % (meta["id"], meta["name"], meta["version"], meta["id"]))
            d.commit()
        elif a.cmd == "uninstall":
            hit = [k for k, m, _, _ in d.installed() if m and m["id"] == a.app_id]
            if not hit: raise DiskError("no installed app with id '%s' (see: novadisk.py apps %s)" % (a.app_id, a.image))
            d.delete(hit[0]); d.commit(); print("uninstalled", a.app_id)
        elif a.cmd == "apps":
            apps = d.installed()
            if not apps: print("(no installed apps)")
            for k, m, t, err in apps:
                print("%-18s %-24s v%-9s %-9s %s" % (m["id"], m["name"], m["version"], t["type"], m.get("author", "")) if m else "INVALID %s: %s" % (d.nodes[k]["name"], err))
        elif a.cmd == "ls":
            idx = d.resolve(a.path)
            if not d.nodes[idx]["dir"]: raise DiskError(a.path + " is a file")
            for k in d.children(idx):
                n = d.nodes[k]; print("%-5s %10s  %s" % ("<dir>" if n["dir"] else "", "" if n["dir"] else n["size"], n["name"]))
        elif a.cmd == "put":
            data = open(a.local, "rb").read()
            idx = d.put(data, a.dest, a.name or os.path.basename(a.local)); d.commit(); print("wrote %s (%d bytes)" % (d.path_of(idx), len(data)))
        elif a.cmd == "get":
            open(a.local, "wb").write(d.read_file(d.resolve(a.path))); print("wrote", a.local)
        elif a.cmd == "mkdir":
            d.mkdir(a.path); d.commit(); print("created", a.path)
        elif a.cmd == "rm":
            d.delete(d.resolve(a.path)); d.commit(); print("removed", a.path)
        elif a.cmd == "check":
            pr = d.check()
            print("OK: file system is consistent" if not pr else "PROBLEMS:\n  " + "\n  ".join(pr)); sys.exit(1 if pr else 0)
        elif a.cmd == "info":
            used = sum(1 for n in d.nodes[1:] if n["used"])
            print("image %s: %d MB, usable %d MB, %d clusters (%d KB free), %d files/folders, %d apps installed" %
                  (a.image, os.path.getsize(a.image) >> 20, d.cap * 512 >> 20, d.ncl, d.free_clusters() * 4, used, len(d.installed())))
    except (DiskError, mkpkg.PkgError) as e:
        sys.exit("error: %s" % e)
    except OSError as e:
        sys.exit("error: %s" % e)


if __name__ == "__main__":
    main()

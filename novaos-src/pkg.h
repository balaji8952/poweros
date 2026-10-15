/* pkg.h - NovaOS native package format ("NPKG" v1, see docs/NOVAPKG_FORMAT.md),
 * application registry, installer / uninstaller and launch dispatch.
 *
 * SECURITY MODEL: a package is DATA.  Nothing in this file (or anywhere in the
 * kernel) ever jumps to, or executes, bytes that came from a package.  Each
 * application *type* has an in-kernel handler that interprets the payload.
 * Everything is validated before use and re-validated at every launch.       */
#ifndef NOVA_PKG_H
#define NOVA_PKG_H
#include "core.h"
#include "fs.h"

#define PKG_FORMAT_VERSION 1
#define PKG_HDR       48
#define PKG_MAX_SIZE  262144u        /* whole package */
#define PKG_MAX_META  2048u
#define PKG_MAX_TYPE  1024u
#define PKG_MAX_DATA  196608u
#define PKG_MAX_TEXT  65535u         /* textview payload */
#define PKG_ID_MAX    24
#define PKG_NAME_MAX  24
#define PKG_VER_MAX   17             /* "65535.65535.65535" */
#define PKG_AUTH_MAX  32
#define PKG_DESC_MAX  80
#define PKG_TITLE_MAX 32
#define REG_MAX       32             /* installed applications */
#define KV_MAX        16
#define KV_LINE_MAX   256

enum { PKG_T_NONE = 0, PKG_T_TEXTVIEW, PKG_T_BUILTIN, PKG_NTYPES };
static const char *pkg_type_name[PKG_NTYPES] = { "", "textview", "builtin" };
/* order matches the APP_* enum in ui_base.h (checked by the tests) */
static const char *pkg_targets[5] = { "files", "notepad", "photos", "display", "about" };

enum {
    PKG_OK = 0,
    PKG_E_SIZE = -10, PKG_E_MAGIC = -11, PKG_E_VERSION = -12, PKG_E_HEADER = -13, PKG_E_CRC = -14,
    PKG_E_LAYOUT = -15, PKG_E_META = -16, PKG_E_TYPE = -17, PKG_E_DATA = -18, PKG_E_UNSUPPORTED = -19,
    PKG_E_DUP = -20, PKG_E_NAMEEXISTS = -21, PKG_E_REGFULL = -22, PKG_E_READ = -23, PKG_E_VERIFY = -24,
    PKG_E_NODIR = -25, PKG_E_NOTFOUND = -26, PKG_E_BUSY = -27
};

typedef struct {
    char id[PKG_ID_MAX + 1], name[PKG_NAME_MAX + 1], version[PKG_VER_MAX + 1];
    char author[PKG_AUTH_MAX + 1], desc[PKG_DESC_MAX + 1], title[PKG_TITLE_MAX + 1];
    int type, wrap, target;
    u32 accent;
    u32 data_off, data_len, size;
} PkgInfo;

static char pkg_detail[72];                       /* which field/rule failed (for messages) */
static int pkg_fail(int code, const char *what) { scpy(pkg_detail, what, sizeof pkg_detail); return code; }

static const char *pkg_err_text(int e) {
    switch (e) {
    case PKG_E_SIZE:        return "The package size is not valid.";
    case PKG_E_MAGIC:       return "This is not a NovaOS package.";
    case PKG_E_VERSION:     return "Unsupported package format version.";
    case PKG_E_HEADER:      return "The package header is invalid.";
    case PKG_E_CRC:         return "The package is corrupted (checksum mismatch).";
    case PKG_E_LAYOUT:      return "The package layout is invalid.";
    case PKG_E_META:        return "The package metadata is invalid.";
    case PKG_E_TYPE:        return "The application type section is invalid.";
    case PKG_E_DATA:        return "The application data is invalid.";
    case PKG_E_UNSUPPORTED: return "This application type is not supported.";
    case PKG_E_DUP:         return "An application with this ID is already installed.";
    case PKG_E_NAMEEXISTS:  return "A file with this application's name already exists.";
    case PKG_E_REGFULL:     return "Too many applications are installed.";
    case PKG_E_READ:        return "The package could not be read (disk error or out of memory).";
    case PKG_E_VERIFY:      return "The installed copy did not verify - installation was rolled back.";
    case PKG_E_NODIR:       return "The Installed Applications folder is unavailable.";
    case PKG_E_NOTFOUND:    return "That application is not installed.";
    case PKG_E_BUSY:        return "No free application window slots.";
    case FS_ERR_NAME:       return "The application ID is not a valid file name.";
    case FS_ERR_EXISTS:     return "A file with this name already exists.";
    case FS_ERR_FULL:       return "No space left on the disk.";
    case FS_ERR_IO:         return "Disk error - the change could not be written.";
    case FS_ERR_NOMEM:      return "Out of memory.";
    default:                return "Unknown error.";
    }
}

/* ------------------------------------------------------------- CRC-32/IEEE */
static u32 pkg_crc_tab[256]; static int pkg_crc_ready;
static u32 pkg_crc(const u8 *b, u32 n) {          /* the stored checksum field (bytes 12..15) counts as zero */
    if (!pkg_crc_ready) {
        for (u32 i = 0; i < 256; i++) { u32 c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; pkg_crc_tab[i] = c; }
        pkg_crc_ready = 1;
    }
    u32 c = 0xFFFFFFFFu;
    for (u32 i = 0; i < n; i++) c = pkg_crc_tab[(c ^ ((i >= 12 && i < 16) ? 0 : b[i])) & 0xFF] ^ (c >> 8);
    return ~c;
}
static u32 pk16(const u8 *p) { return p[0] | (p[1] << 8); }

/* -------------------------------------------------------- manifest parsing */
typedef struct { char key[17]; const u8 *v; u32 vl; } Kv;

static int pkg_kv(const u8 *s, u32 len, Kv *kv, int *nkv, int code) {
    if (len == 0) return pkg_fail(code, "empty section");
    for (u32 i = 0; i < len; i++) if (s[i] != '\n' && (s[i] < 0x20 || s[i] > 0x7E)) return pkg_fail(code, "invalid byte");
    if (s[len - 1] == '\n') len--;
    int n = 0; u32 p = 0;
    for (;;) {
        u32 e = p; while (e < len && s[e] != '\n') e++;
        u32 ll = e - p;
        if (ll == 0 || ll > KV_LINE_MAX) return pkg_fail(code, "bad line length");
        if (n >= KV_MAX) return pkg_fail(code, "too many lines");
        u32 q = p; while (q < e && s[q] != '=') q++;
        if (q == e) return pkg_fail(code, "line without '='");
        u32 kl = q - p;
        if (kl < 1 || kl > 16) return pkg_fail(code, "bad key length");
        for (u32 j = 0; j < kl; j++) {
            u8 c = s[p + j];
            int ok = (c >= 'a' && c <= 'z') || (j > 0 && ((c >= '0' && c <= '9') || c == '_'));
            if (!ok) return pkg_fail(code, "bad key character");
            kv[n].key[j] = (char)c;
        }
        kv[n].key[kl] = 0;
        for (int j = 0; j < n; j++) if (!memcmp(kv[j].key, kv[n].key, kl + 1)) return pkg_fail(code, "duplicate key");
        kv[n].v = s + q + 1; kv[n].vl = e - q - 1; n++;
        if (e >= len) break;
        p = e + 1;
    }
    *nkv = n;
    return 0;
}
static const Kv *kv_find(const Kv *kv, int n, const char *k) {
    for (int i = 0; i < n; i++) if (!memcmp(kv[i].key, k, (u32)slen(k) + 1)) return &kv[i];
    return 0;
}
static int kv_is(const Kv *k, const char *lit) { return k && k->vl == (u32)slen(lit) && !memcmp(k->v, lit, k->vl); }
static int kv_allowed(const Kv *kv, int n, const char *const *allowed, int na) {
    for (int i = 0; i < n; i++) {
        int ok = 0;
        for (int j = 0; j < na; j++) if (!memcmp(kv[i].key, allowed[j], (u32)slen(allowed[j]) + 1)) ok = 1;
        if (!ok) return 0;
    }
    return 1;
}
static void kv_copy(char *dst, u32 max, const Kv *k) {
    u32 n = k ? k->vl : 0;
    if (n > max) n = max;
    if (n) memcpy(dst, k->v, n);
    dst[n] = 0;
}
static int pkg_is_ver(const Kv *k, int *ok_out) {
    const u8 *s = k->v; u32 n = k->vl, i = 0; int parts = 0;
    while (parts < 3) {
        u32 st = i, val = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') { val = val * 10 + (s[i] - '0'); i++; if (i - st > 5) return 0; }
        if (i == st) return 0;
        if (i - st > 1 && s[st] == '0') return 0;                 /* no leading zeros */
        if (val > 65535) return 0;
        parts++;
        if (parts < 3) { if (i >= n || s[i] != '.') return 0; i++; }
    }
    (void)ok_out;
    return i == n;
}

/* ---------------------------------------------------------------- validator
 * Returns PKG_OK and fills *o, or a PKG_E_* code (pkg_detail says which rule).
 * Never reads outside [b, b+len) and never allocates.                        */
static int pkg_validate(const u8 *b, u32 len, PkgInfo *o) {
    Kv meta[KV_MAX], typ[KV_MAX]; int nm = 0, nt = 0;
    pkg_detail[0] = 0;
    memset(o, 0, sizeof *o);
    if (!b || len < PKG_HDR || len > PKG_MAX_SIZE) return pkg_fail(PKG_E_SIZE, "size out of range");
    if (memcmp(b, "NPKG", 4)) return pkg_fail(PKG_E_MAGIC, "bad magic");
    if (pk16(b + 4) != PKG_FORMAT_VERSION) return pkg_fail(PKG_E_VERSION, "format version");
    if (pk16(b + 6) != PKG_HDR || ld32(b + 8) != len) return pkg_fail(PKG_E_HEADER, "header size / total size");
    for (int i = 40; i < 48; i++) if (b[i]) return pkg_fail(PKG_E_HEADER, "reserved bytes not zero");
    if (pkg_crc(b, len) != ld32(b + 12)) return pkg_fail(PKG_E_CRC, "crc32");
    u32 mo = ld32(b + 16), ml = ld32(b + 20), to = ld32(b + 24), tl = ld32(b + 28), dof = ld32(b + 32), dl = ld32(b + 36);
    if (ml < 1 || ml > PKG_MAX_META || tl < 1 || tl > PKG_MAX_TYPE || dl > PKG_MAX_DATA) return pkg_fail(PKG_E_LAYOUT, "section size");
    if (mo != PKG_HDR || to != mo + ml || dof != to + tl || dof + dl != len) return pkg_fail(PKG_E_LAYOUT, "sections not contiguous");

    if (pkg_kv(b + mo, ml, meta, &nm, PKG_E_META)) return PKG_E_META;
    if (pkg_kv(b + to, tl, typ, &nt, PKG_E_TYPE)) return PKG_E_TYPE;
    static const char *const meta_ok[5] = { "id", "name", "version", "author", "description" };
    if (!kv_allowed(meta, nm, meta_ok, 5)) return pkg_fail(PKG_E_META, "unknown key");
    const Kv *id = kv_find(meta, nm, "id"), *name = kv_find(meta, nm, "name"), *ver = kv_find(meta, nm, "version");
    const Kv *auth = kv_find(meta, nm, "author"), *desc = kv_find(meta, nm, "description");
    if (!id || !name || !ver) return pkg_fail(PKG_E_META, "missing id/name/version");
    if (id->vl < 3 || id->vl > PKG_ID_MAX) return pkg_fail(PKG_E_META, "id length");
    for (u32 i = 0; i < id->vl; i++) {
        u8 c = id->v[i];
        int ok = (c >= 'a' && c <= 'z') || (i > 0 && ((c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'));
        if (!ok) return pkg_fail(PKG_E_META, "id characters");
    }
    if (name->vl < 1 || name->vl > PKG_NAME_MAX) return pkg_fail(PKG_E_META, "name length");
    if (name->v[0] == ' ' || name->v[name->vl - 1] == ' ') return pkg_fail(PKG_E_META, "name has edge space");
    for (u32 i = 0; i < name->vl; i++) if (name->v[i] == '/' || name->v[i] == '\\') return pkg_fail(PKG_E_META, "name characters");
    if (!pkg_is_ver(ver, 0)) return pkg_fail(PKG_E_META, "version must be N.N.N");
    if ((auth && auth->vl > PKG_AUTH_MAX) || (desc && desc->vl > PKG_DESC_MAX)) return pkg_fail(PKG_E_META, "author/description length");

    const Kv *t = kv_find(typ, nt, "type"), *acc = kv_find(typ, nt, "accent");
    if (!t) return pkg_fail(PKG_E_TYPE, "missing type");
    u32 accent = 0x4F7BE8;
    if (acc) {
        if (acc->vl != 6) return pkg_fail(PKG_E_TYPE, "accent must be 6 hex digits");
        accent = 0;
        for (u32 i = 0; i < 6; i++) {
            u8 c = acc->v[i]; u32 v;
            if (c >= '0' && c <= '9') v = c - '0'; else if (c >= 'a' && c <= 'f') v = c - 'a' + 10; else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else return pkg_fail(PKG_E_TYPE, "accent must be 6 hex digits");
            accent = (accent << 4) | v;
        }
    }
    o->wrap = 1; o->target = -1;
    if (kv_is(t, "textview")) {
        static const char *const ok[4] = { "type", "title", "wrap", "accent" };
        if (!kv_allowed(typ, nt, ok, 4)) return pkg_fail(PKG_E_TYPE, "unknown key");
        const Kv *ti = kv_find(typ, nt, "title"), *wr = kv_find(typ, nt, "wrap");
        if (ti && ti->vl > PKG_TITLE_MAX) return pkg_fail(PKG_E_TYPE, "title length");
        if (wr) { if (kv_is(wr, "0")) o->wrap = 0; else if (kv_is(wr, "1")) o->wrap = 1; else return pkg_fail(PKG_E_TYPE, "wrap must be 0 or 1"); }
        if (dl < 1 || dl > PKG_MAX_TEXT) return pkg_fail(PKG_E_DATA, "textview needs 1..65535 bytes");
        for (u32 i = 0; i < dl; i++) {
            u8 c = b[dof + i];
            if ((c < 0x20 || c > 0x7E) && c != '\n' && c != '\t' && c != '\r') return pkg_fail(PKG_E_DATA, "textview: non-text byte");
        }
        kv_copy(o->title, PKG_TITLE_MAX, ti);
        o->type = PKG_T_TEXTVIEW;
    } else if (kv_is(t, "builtin")) {
        static const char *const ok[3] = { "type", "target", "accent" };
        if (!kv_allowed(typ, nt, ok, 3)) return pkg_fail(PKG_E_TYPE, "unknown key");
        const Kv *tg = kv_find(typ, nt, "target");
        if (!tg) return pkg_fail(PKG_E_TYPE, "builtin needs target");
        for (int i = 0; i < 5; i++) if (kv_is(tg, pkg_targets[i])) o->target = i;
        if (o->target < 0) return pkg_fail(PKG_E_TYPE, "unknown builtin target");
        if (dl != 0) return pkg_fail(PKG_E_DATA, "builtin must have no data");
        o->type = PKG_T_BUILTIN;
    } else return pkg_fail(PKG_E_UNSUPPORTED, "type");

    kv_copy(o->id, PKG_ID_MAX, id); kv_copy(o->name, PKG_NAME_MAX, name); kv_copy(o->version, PKG_VER_MAX, ver);
    kv_copy(o->author, PKG_AUTH_MAX, auth); kv_copy(o->desc, PKG_DESC_MAX, desc);
    o->accent = accent; o->data_off = dof; o->data_len = dl; o->size = len;
    return PKG_OK;
}

/* --------------------------------------------------------- locations + I/O */
static int pkg_dir_named(int parent, const char *name) {
    int d = parent < 0 ? -1 : fs_lookup(parent, name);
    return (d > 0 && fsn[d].dir) ? d : -1;
}
static int pkg_sys_dir(void)  { return pkg_dir_named(0, "System"); }
static int pkg_apps_dir(void) { return pkg_dir_named(pkg_sys_dir(), "Installed Applications"); }
static int pkg_is_protected(int node) { return node > 0 && (node == pkg_sys_dir() || node == pkg_apps_dir()); }
static int pkg_is_source_name(const char *n) { return ext_is(n, "powor") || ext_is(n, "appd"); }
static int pkg_is_installed_name(const char *n) { return ext_is(n, "power"); }

/* read + validate a package file; on success *dp points at its (pinned) bytes */
static int pkg_load(int node, PkgInfo *pi, const u8 **dp) {
    if (node <= 0 || node >= MAXNODES || !fsn[node].used || fsn[node].dir) return pkg_fail(PKG_E_NOTFOUND, "no such file");
    if (fsn[node].size < PKG_HDR || fsn[node].size > PKG_MAX_SIZE) return pkg_fail(PKG_E_SIZE, "size out of range");
    fs_pin = node;
    const u8 *d = fs_data(node);
    if (!d) { fs_pin = -1; return pkg_fail(PKG_E_READ, "read failed"); }
    int e = pkg_validate(d, fsn[node].size, pi);
    if (e) { fs_pin = -1; return e; }
    *dp = d;
    return PKG_OK;
}

/* ----------------------------------------------------------------- registry */
typedef struct { int node; u32 ver; PkgInfo info; } RegEntry;
static RegEntry reg[REG_MAX];
static int reg_n, reg_bad;
static u32 reg_stamp = 0xFFFFFFFFu;
static void (*pkg_close_hook)(const char *id);      /* UI: close windows of an app being removed */

static int reg_find_id(const char *id) {
    for (int i = 0; i < reg_n; i++) if (!memcmp(reg[i].info.id, id, (u32)slen(id) + 1)) return i;
    return -1;
}
static int reg_find_node(int node) {
    for (int i = 0; i < reg_n; i++) if (reg[i].node == node) return i;
    return -1;
}
/* Rebuild the list of installed apps from /System/Installed Applications.  Cheap when nothing changed. */
static void reg_refresh(int force) {
    static RegEntry old[REG_MAX];
    static int kids[MAXNODES];
    if (!force && reg_stamp == fs_stamp) return;
    reg_stamp = fs_stamp;
    memcpy(old, reg, sizeof reg);
    int on = reg_n;
    reg_n = 0; reg_bad = 0;
    int a = pkg_apps_dir();
    if (a < 0) return;
    int n = fs_children(a, kids);
    for (int i = 0; i < n; i++) {
        int k = kids[i];
        if (fsn[k].dir || !pkg_is_installed_name(fsn[k].name)) continue;
        if (reg_n >= REG_MAX) { reg_bad++; continue; }
        RegEntry e; memset(&e, 0, sizeof e);
        int cached = -1;
        for (int j = 0; j < on; j++) if (old[j].node == k && old[j].ver == fsn[k].ver) cached = j;
        if (cached >= 0) e = old[cached];
        else {
            PkgInfo pi; const u8 *d;
            int saved_pin = fs_pin;
            if (pkg_load(k, &pi, &d)) { reg_bad++; fs_pin = saved_pin; continue; }
            fs_pin = saved_pin;
            e.node = k; e.ver = fsn[k].ver; e.info = pi;
        }
        if (reg_find_id(e.info.id) >= 0) { reg_bad++; continue; }       /* duplicate id: first one wins */
        reg[reg_n++] = e;
    }
}

/* ---------------------------------------------------- one-time system setup */
static int pkg_system_init(void) {
    int created = 0;
    int s = fs_lookup(0, "System");
    if (s < 0) { s = fs_new(0, "System", 1); if (s < 0) return s; created = 1; }
    else if (!fsn[s].dir) return FS_ERR_EXISTS;
    int a = fs_lookup(s, "Installed Applications");
    if (a < 0) { a = fs_new(s, "Installed Applications", 1); if (a < 0) return a; created = 1; }
    else if (!fsn[a].dir) return FS_ERR_EXISTS;
    if (created) {                                   /* first run on this disk: provide the sample packages */
        int docs = fs_lookup(0, "Documents");
        for (u32 i = 0; docs > 0 && i < asset_count; i++) {
            if (!pkg_is_source_name(asset_table[i].name) || fs_lookup(docs, asset_table[i].name) >= 0) continue;
            int f = fs_new(docs, asset_table[i].name, 0);
            if (f > 0 && fs_write(f, asset_table[i].data, asset_table[i].size) < 0) fs_delete(f);
        }
    }
    reg_refresh(1);
    return 0;
}

/* ------------------------------------------------------------------ install */
/* Install the package file `src`.  Returns PKG_OK or an error (PKG_E_*, FS_ERR_*); on any error nothing is left behind. */
static int pkg_install(int src, PkgInfo *out) {
    PkgInfo pi; const u8 *d;
    int e = pkg_load(src, &pi, &d);                   /* validates; pins src */
    if (e) return e;
    u32 n = fsn[src].size;
    int apps = pkg_apps_dir();
    if (apps < 0) { fs_pin = -1; return pkg_fail(PKG_E_NODIR, "missing folder"); }
    reg_refresh(0);
    char fn[PKG_ID_MAX + 8], *p = put_str(fn, pi.id);
    p = put_str(p, ".power"); *p = 0;                 /* put_str() does not terminate */
    if (reg_find_id(pi.id) >= 0) { fs_pin = -1; return pkg_fail(PKG_E_DUP, pi.id); }
    if (fs_lookup(apps, fn) >= 0) { fs_pin = -1; return pkg_fail(PKG_E_NAMEEXISTS, fn); }
    if (reg_n >= REG_MAX) { fs_pin = -1; return pkg_fail(PKG_E_REGFULL, "limit is 32"); }
    int dst = fs_new(apps, fn, 0);
    if (dst < 0) { fs_pin = -1; return dst; }
    e = fs_write(dst, d, n);
    if (e) { fs_delete(dst); fs_pin = -1; reg_refresh(1); return e; }
    /* read-after-write: throw away the cached copy and compare what comes back from disk */
    fs_evict(dst);
    const u8 *v = fs_data(dst);
    if (!v || fsn[dst].size != n || memcmp(v, d, n)) {
        fs_delete(dst); fs_pin = -1; reg_refresh(1);
        return pkg_fail(PKG_E_VERIFY, "read-back mismatch");
    }
    fs_pin = -1;
    reg_refresh(1);
    if (reg_find_node(dst) < 0) { fs_delete(dst); reg_refresh(1); return pkg_fail(PKG_E_VERIFY, "not registered"); }
    if (out) *out = pi;
    return PKG_OK;
}

/* ---------------------------------------------------------------- uninstall */
static int pkg_uninstall(int node) {
    reg_refresh(0);
    int r = reg_find_node(node);
    if (r < 0 || fsn[node].parent != pkg_apps_dir()) return pkg_fail(PKG_E_NOTFOUND, "not in registry");
    char id[PKG_ID_MAX + 1]; scpy(id, reg[r].info.id, sizeof id);
    if (pkg_close_hook) pkg_close_hook(id);           /* close its windows first */
    int e = fs_delete(node);
    reg_refresh(1);
    if (fsn[node].used) return pkg_fail(PKG_E_NOTFOUND, "still present");
    return e;                                         /* 0, or FS_ERR_IO if the removal could not be persisted */
}

/* ------------------------------------------------------------- launch dispatch
 * One launcher per supported application type; registered by the UI.        */
typedef int (*PkgLauncher)(const PkgInfo *pi, const u8 *pkg);
static PkgLauncher pkg_launcher[PKG_NTYPES];

static int pkg_launch_node(int node) {
    PkgInfo pi; const u8 *d;
    int e = pkg_load(node, &pi, &d);                  /* always re-validate: the file may have changed on disk */
    if (e) return e;
    if (pi.type <= 0 || pi.type >= PKG_NTYPES || !pkg_launcher[pi.type]) { fs_pin = -1; return pkg_fail(PKG_E_UNSUPPORTED, "no handler"); }
    e = pkg_launcher[pi.type](&pi, d);
    fs_pin = -1;
    return e;
}
static int pkg_launch_reg(int i) {
    reg_refresh(0);
    if (i < 0 || i >= reg_n) return pkg_fail(PKG_E_NOTFOUND, "bad index");
    return pkg_launch_node(reg[i].node);
}
#endif

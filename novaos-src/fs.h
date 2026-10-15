/* fs.h - in-memory hierarchical file system (files + folders).
 * Nodes live in a fixed table; file contents are heap blocks.  Files that
 * were embedded at build time point straight into the kernel image ("rom")
 * and are copied on first write.                                           */
#ifndef NOVA_FS_H
#define NOVA_FS_H
#include "core.h"
#include "disk.h"

#define NAME_MAX  31
#define MAXNODES  1024
#define FS_ERR_NAME   (-1)
#define FS_ERR_EXISTS (-2)
#define FS_ERR_FULL   (-3)
#define FS_ERR_IO     (-4)
#define FS_ERR_NOMEM  (-5)

typedef struct { u8 used, dir, rom; int parent; char name[NAME_MAX + 1]; u8 *data; u32 size, cap, first, ver; } Node;      /* ver: RAM-only change stamp */
static Node fsn[MAXNODES];          /* index 0 is the root folder */
static u32 fs_stamp = 1;            /* bumped by every create/write/rename/delete (lets caches notice changes) */

struct AssetEnt { const char *name, *folder; const u8 *data; u32 size; };
extern const struct AssetEnt asset_table[];
extern const u32 asset_count;

/* ===================================================================
 *  On-disk format ("NovaFS", 512-byte sectors, 4 KB clusters)
 *    LBA 0              superblock  "NOVAFS01", version, geometry
 *    LBA 1 .. 128       node table  (1024 x 64 bytes: flags, parent, size, first cluster, name)
 *    then               FAT         (u32 per cluster: 0 free, 0xFFFFFFFF end, else next)
 *    then               data clusters (8 sectors each)
 *  Every change is written straight through: data -> FAT -> node entry.
 * ================================================================== */
#define NODE_SECS    (MAXNODES * 64 / 512)
#define CL_SECS      8
#define CL_BYTES     (CL_SECS * 512)
#define MAX_CLUSTERS 65536                 /* uses at most the first 256 MB of a disk */
#define FAT_END      0xFFFFFFFFu

static int disk_on;                        /* 1 when the file system is backed by the disk */
static int fs_state;                       /* 0 no disk, 1 mounted/active, 2 disk present but not NovaFS */
static u32 d_ncl, d_fat_lba, d_fat_secs, d_data_lba, fat_free, fat_hint = 1;
static u32 fat[MAX_CLUSTERS + 128];
static u8  fat_dirty[MAX_CLUSTERS / 128 + 2];
static u8  node_buf[MAXNODES * 64];
static u8  cl_buf[CL_BYTES];
static int fs_pin = -1;                    /* node whose data must not be evicted right now */

static void node_pack(int i, u8 *o) {
    const Node *n = &fsn[i];
    memset(o, 0, 64);
    o[0] = n->used; o[1] = n->dir;
    st32(o + 4, (u32)n->parent); st32(o + 8, n->size); st32(o + 12, n->first);
    memcpy(o + 16, n->name, 32);
}
static void node_unpack(int i, const u8 *p) {
    Node *n = &fsn[i];
    memset(n, 0, sizeof *n);
    n->used = p[0] ? 1 : 0; n->dir = p[1] ? 1 : 0;
    n->parent = (int)ld32(p + 4); n->size = ld32(p + 8); n->first = ld32(p + 12);
    memcpy(n->name, p + 16, 32); n->name[NAME_MAX] = 0;
}
static int node_flush(int i) {              /* write the sector holding node i */
    if (!disk_on) return 0;
    u8 buf[512];
    for (int k = 0; k < 8; k++) node_pack((i & ~7) + k, buf + k * 64);
    return disk_write(1 + (u32)i / 8, 1, buf);
}
static void fat_flush(void) {
    for (u32 s = 0; s < d_fat_secs; s++) if (fat_dirty[s]) {
        fat_dirty[s] = 0;
        disk_write(d_fat_lba + s, 1, &fat[s * 128]);
    }
}
static u32 cl_lba(u32 c) { return d_data_lba + (c - 1) * CL_SECS; }
static u32 fat_alloc(void) {
    for (u32 n = 0; n < d_ncl; n++) {
        u32 c = fat_hint + n; if (c > d_ncl) c -= d_ncl;
        if (!fat[c]) { fat[c] = FAT_END; fat_dirty[c / 128] = 1; fat_free--; fat_hint = c + 1; return c; }
    }
    return 0;
}
static void fat_free_chain(u32 c) {
    for (u32 guard = 0; c && c != FAT_END && c <= d_ncl && guard <= d_ncl; guard++) {
        u32 nx = fat[c];
        if (nx) fat_free++;
        fat[c] = 0; fat_dirty[c / 128] = 1;
        c = nx;
    }
}
static void disk_commit(void) { if (!disk_on) return; fat_flush(); disk_flush(); }

/* write a file's data to disk (new chain first, old chain freed afterwards) */
static int disk_store(int idx, const u8 *data, u32 len) {
    Node *n = &fsn[idx];
    u32 need = (len + CL_BYTES - 1) / CL_BYTES, first = 0, prev = 0;
    for (u32 i = 0; i < need; i++) {
        u32 c = fat_alloc();
        if (!c) { fat_free_chain(first); return FS_ERR_FULL; }
        if (prev) fat[prev] = c; else first = c;
        fat_dirty[prev / 128] = 1; prev = c;
    }
    u32 c = first;
    for (u32 off = 0; off < len && c && c != FAT_END; off += CL_BYTES, c = fat[c]) {
        u32 chunk = len - off < CL_BYTES ? len - off : CL_BYTES;
        int e;
        if (chunk == CL_BYTES) e = disk_write(cl_lba(c), CL_SECS, data + off);
        else { memset(cl_buf, 0, CL_BYTES); memcpy(cl_buf, data + off, chunk); e = disk_write(cl_lba(c), CL_SECS, cl_buf); }
        if (e) { fat_free_chain(first); return FS_ERR_IO; }
    }
    fat_flush();                                   /* new chain is durable ... */
    u32 old = n->first;
    n->first = first;
    if (node_flush(idx)) { n->first = old; fat_free_chain(first); return FS_ERR_IO; }
    fat_free_chain(old);                           /* ... only then release the old one */
    disk_commit();
    return 0;
}
/* read a file's data from disk into a fresh heap block */
static u8 *disk_load(Node *n) {
    u8 *buf = kmalloc(n->size ? n->size : 1);
    if (!buf) return 0;
    u32 c = n->first;
    for (u32 off = 0; off < n->size; off += CL_BYTES) {
        if (!c || c == FAT_END || c > d_ncl) { kfree(buf); return 0; }
        u32 chunk = n->size - off < CL_BYTES ? n->size - off : CL_BYTES;
        int e;
        if (chunk == CL_BYTES) e = disk_read(cl_lba(c), CL_SECS, buf + off);
        else { e = disk_read(cl_lba(c), CL_SECS, cl_buf); if (!e) memcpy(buf + off, cl_buf, chunk); }
        if (e) { kfree(buf); return 0; }
        c = fat[c];
    }
    return buf;
}
/* file contents (loads from disk on first use); NULL for empty files / errors */
static const u8 *fs_data(int idx) {
    Node *n = &fsn[idx];
    if (!n->data && n->size && disk_on && !n->dir) {
        u8 *p = disk_load(n);
        if (p) { n->data = p; n->cap = n->size; n->rom = 0; }
    }
    return n->data;
}
static void fs_reclaim(void) {              /* heap is full: drop clean cached file data */
    if (!disk_on) return;
    for (int i = 1; i < MAXNODES; i++) {
        Node *n = &fsn[i];
        if (n->used && !n->rom && n->data && i != fs_pin) { kfree(n->data); n->data = 0; n->cap = 0; }
    }
}

static int fs_format(void) {                /* lay out an empty NovaFS on the whole disk */
    u32 cap = ata.sectors > 524288 ? 524288 : ata.sectors;
    if (!ata.ok || cap < 2048) return -1;
    u32 ncl = (cap - 200) / CL_SECS, fsecs = 0, dlba = 0;
    for (int pass = 0; pass < 3; pass++) {
        if (ncl > MAX_CLUSTERS) ncl = MAX_CLUSTERS;
        fsecs = (ncl + 1 + 127) / 128;
        dlba = (1 + NODE_SECS + fsecs + 7) & ~7u;
        ncl = (cap - dlba) / CL_SECS;
    }
    if (ncl > MAX_CLUSTERS) ncl = MAX_CLUSTERS;
    d_ncl = ncl; d_fat_secs = (ncl + 1 + 127) / 128; d_fat_lba = 1 + NODE_SECS; d_data_lba = dlba;
    memset(fat, 0, sizeof fat); memset(fat_dirty, 0, sizeof fat_dirty);
    fat_free = ncl; fat_hint = 1;
    u8 sb[512]; memset(sb, 0, sizeof sb);
    memcpy(sb, "NOVAFS01", 8);
    st32(sb + 8, 1); st32(sb + 12, cap); st32(sb + 16, ncl); st32(sb + 20, 1);
    st32(sb + 24, d_fat_lba); st32(sb + 28, d_fat_secs); st32(sb + 32, d_data_lba);
    memset(node_buf, 0, sizeof node_buf);
    if (disk_write(1, NODE_SECS, node_buf) || disk_write(d_fat_lba, d_fat_secs, fat) || disk_write(0, 1, sb)) return -1;
    disk_flush();
    return 0;
}
static int fs_mount(void) {                 /* 1 if the disk holds a valid NovaFS (and load it) */
    u8 sb[512];
    if (disk_read(0, 1, sb) || memcmp(sb, "NOVAFS01", 8) || ld32(sb + 8) != 1) return 0;
    u32 ncl = ld32(sb + 16), fl = ld32(sb + 24), fs = ld32(sb + 28), dl = ld32(sb + 32);
    if (ld32(sb + 20) != 1 || ncl < 8 || ncl > MAX_CLUSTERS || fl != 1 + NODE_SECS ||
        fs != (ncl + 1 + 127) / 128 || dl < fl + fs || dl + ncl * CL_SECS > ata.sectors) return 0;
    if (disk_read(1, NODE_SECS, node_buf) || disk_read(fl, fs, fat)) return 0;
    memset(fsn, 0, sizeof fsn);
    for (int i = 0; i < MAXNODES; i++) node_unpack(i, node_buf + i * 64);
    if (!fsn[0].used || !fsn[0].dir) return 0;
    fsn[0].parent = -1;
    for (int i = 1; i < MAXNODES; i++) {
        Node *n = &fsn[i];
        if (!n->used) continue;
        if (n->parent < 0 || n->parent >= MAXNODES || !fsn[n->parent].used || !fsn[n->parent].dir || n->parent == i) { memset(n, 0, sizeof *n); continue; }
        if (n->dir) { n->size = 0; n->first = 0; }
        else if (n->size && (n->first == 0 || n->first > ncl)) n->size = 0;
    }
    d_ncl = ncl; d_fat_lba = fl; d_fat_secs = fs; d_data_lba = dl;
    memset(fat_dirty, 0, sizeof fat_dirty);
    fat_free = 0;
    for (u32 c = 1; c <= ncl; c++) if (!fat[c]) fat_free++;
    fat_hint = 1;
    disk_on = 1;
    return 1;
}

static int fs_valid_name(const char *s) {
    int n = slen(s);
    if (n < 1 || n > NAME_MAX) return 0;
    if (s[0] == ' ' || s[n - 1] == ' ') return 0;
    if (!scmp_ci(s, ".") || !scmp_ci(s, "..")) return 0;
    for (int i = 0; i < n; i++) {
        u8 c = (u8)s[i];
        if (c < 32 || c > 126 || c == '/' || c == '\\') return 0;
    }
    return 1;
}
static int fs_lookup(int parent, const char *name) {
    for (int i = 1; i < MAXNODES; i++)
        if (fsn[i].used && fsn[i].parent == parent && !scmp_ci(fsn[i].name, name)) return i;
    return -1;
}
static void fs_free_data(Node *n) {
    if (n->data && !n->rom) kfree(n->data);
    n->data = 0; n->size = n->cap = 0; n->rom = 0;
}
/* returns the new node index, or a negative FS_ERR_* code */
static int fs_new(int parent, const char *name, int dir) {
    if (!fs_valid_name(name)) return FS_ERR_NAME;
    if (fs_lookup(parent, name) >= 0) return FS_ERR_EXISTS;
    for (int i = 1; i < MAXNODES; i++) if (!fsn[i].used) {
        Node *n = &fsn[i];
        memset(n, 0, sizeof *n);
        n->used = 1; n->dir = (u8)dir; n->parent = parent; n->ver = ++fs_stamp;
        scpy(n->name, name, NAME_MAX + 1);
        if (disk_on) { if (node_flush(i)) { n->used = 0; return FS_ERR_IO; } disk_flush(); }
        return i;
    }
    return FS_ERR_FULL;
}
static int fs_delete(int idx) {                     /* recursive; the root can't be deleted. Returns 0 or FS_ERR_IO */
    if (idx <= 0 || !fsn[idx].used) return 0;
    int err = 0;
    u32 e0 = disk_err_count;
    if (fsn[idx].dir)
        for (int i = 1; i < MAXNODES; i++)
            if (fsn[i].used && fsn[i].parent == idx && fs_delete(i)) err = FS_ERR_IO;
    fs_free_data(&fsn[idx]);
    if (disk_on) fat_free_chain(fsn[idx].first);
    fsn[idx].used = 0; fsn[idx].first = 0; fs_stamp++;
    if (disk_on) { if (node_flush(idx)) err = FS_ERR_IO; disk_commit(); if (disk_err_count != e0) err = FS_ERR_IO; }
    return err;
}
static int fs_rename(int idx, const char *name) {
    if (!fs_valid_name(name)) return FS_ERR_NAME;
    int f = fs_lookup(fsn[idx].parent, name);
    if (f >= 0 && f != idx) return FS_ERR_EXISTS;
    char old[NAME_MAX + 1]; scpy(old, fsn[idx].name, sizeof old);
    scpy(fsn[idx].name, name, NAME_MAX + 1);
    if (disk_on) { if (node_flush(idx)) { scpy(fsn[idx].name, old, sizeof old); return FS_ERR_IO; } disk_flush(); }
    fsn[idx].ver = ++fs_stamp;
    return 0;
}
/* Replace a file's contents.  All-or-nothing: if the disk write (or an allocation) fails the file keeps its old
 * contents and an error is returned (FS_ERR_NOMEM / FS_ERR_FULL / FS_ERR_IO). */
static int fs_write(int idx, const u8 *data, u32 len) {
    Node *n = &fsn[idx];
    u8 *p = kmalloc(len ? len : 1);
    if (!p) return FS_ERR_NOMEM;
    if (len) memcpy(p, data, len);
    u8 *od = n->data; u32 os = n->size, oc = n->cap; u8 orom = n->rom;
    n->data = p; n->size = len; n->cap = len ? len : 1; n->rom = 0;
    if (disk_on) {
        int e = disk_store(idx, p, len);
        if (e) { n->data = od; n->size = os; n->cap = oc; n->rom = orom; kfree(p); return e; }
    }
    if (od && !orom) kfree(od);
    n->ver = ++fs_stamp;
    return 0;
}
/* drop the cached copy of a clean disk-backed file (it is re-read from disk on next use) */
static void fs_evict(int idx) {
    Node *n = &fsn[idx];
    if (disk_on && n->used && !n->dir && !n->rom && n->data) { kfree(n->data); n->data = 0; n->cap = 0; }
}
static void fs_path(int idx, char *buf, int max) {
    int stack[32], d = 0;
    while (idx > 0 && d < 32) { stack[d++] = idx; idx = fsn[idx].parent; }
    char *p = buf, *end = buf + max - 1;
    if (!d) { buf[0] = '/'; buf[1] = 0; return; }
    while (d-- > 0) {
        if (p < end) *p++ = '/';
        for (const char *s = fsn[stack[d]].name; *s && p < end; s++) *p++ = *s;
    }
    *p = 0;
}
/* "untitled.txt", "untitled 2.txt", ... */
static void fs_unique(int parent, const char *base, const char *ext, char *out) {
    char *p = put_str(out, base); p = put_str(p, ext); *p = 0;           /* put_str() does not terminate */
    for (u32 n = 2; fs_lookup(parent, out) >= 0 && n < 1000; n++) {
        p = put_str(out, base); *p++ = ' '; p = put_u32(p, n); p = put_str(p, ext); *p = 0;
    }
}
static int fs_less(int a, int b) {                  /* folders first, then by name */
    if (fsn[a].dir != fsn[b].dir) return fsn[a].dir > fsn[b].dir;
    return scmp_ci(fsn[a].name, fsn[b].name) < 0;
}
static int fs_children(int parent, int *out) {      /* sorted list of children */
    int n = 0;
    for (int i = 1; i < MAXNODES; i++)
        if (fsn[i].used && fsn[i].parent == parent) {
            int j = n++;
            while (j > 0 && fs_less(i, out[j - 1])) { out[j] = out[j - 1]; j--; }
            out[j] = i;
        }
    return n;
}
static int fs_alive(int idx) {                      /* node and all ancestors still exist */
    if (idx == 0) return 1;
    for (int d = 0; d < 40 && idx > 0; d++) { if (!fsn[idx].used) return 0; idx = fsn[idx].parent; }
    return 1;
}
static void fmt_size(char *out, u32 v) {
    char *p = out;
    if (v < 1024) { p = put_u32(p, v); p = put_str(p, " B"); }
    else if (v < 1024 * 1024) { u32 t = v * 10 / 1024; p = put_u32(p, t / 10); *p++ = '.'; p = put_u32(p, t % 10); p = put_str(p, " KB"); }
    else { u32 t = (v / 1024) * 10 / 1024; p = put_u32(p, t / 10); *p++ = '.'; p = put_u32(p, t % 10); p = put_str(p, " MB"); }
    *p = 0;
}

static const char welcome_txt[] =
    "Welcome to NovaOS 0.2!\n\n"
    "Files   - create, rename and delete files and folders\n"
    "          (toolbar, or F2 = rename, Del = delete,\n"
    "           Ctrl+N = new file, Ctrl+Shift+N = new folder).\n"
    "Notepad - edit text files; Ctrl+S saves, Ctrl+Shift+S = Save As.\n"
    "Photos  - view BMP, PNG and JPEG pictures, set the wallpaper.\n"
    "Display - change the screen resolution on the fly\n"
    "          (QEMU / Bochs / VirtualBox VGA).\n\n"
    "Files are saved to the hard disk (IDE/ATA) when one is attached;\n"
    "without a disk everything lives in RAM and resets on restart.\n";

static void fs_seed(void) {                 /* default folders + files (RAM) */
    memset(fsn, 0, sizeof fsn);
    fsn[0].used = 1; fsn[0].dir = 1; fsn[0].parent = -1;
    fs_new(0, "Desktop", 1);
    int docs = fs_new(0, "Documents", 1);
    fs_new(0, "Pictures", 1);
    int w = fs_new(docs, "Welcome.txt", 0);
    if (w > 0) fs_write(w, (const u8 *)welcome_txt, sizeof welcome_txt - 1);
    for (u32 i = 0; i < asset_count; i++) {
        int dir = fs_lookup(0, asset_table[i].folder);
        if (dir < 0) dir = fs_new(0, asset_table[i].folder, 1);
        if (dir < 0) continue;
        int f = fs_new(dir, asset_table[i].name, 0);
        if (f > 0) { fsn[f].data = (u8 *)asset_table[i].data; fsn[f].size = asset_table[i].size; fsn[f].rom = 1; }
    }
}
/* format the disk and write the current in-RAM tree onto it */
static int fs_attach(void) {
    disk_on = 0;
    if (fs_format()) return -1;
    disk_on = 1;
    int bad = 0;
    for (int i = 0; i < MAXNODES; i++) {
        if (!fsn[i].used) continue;
        fsn[i].first = 0;
        if (!fsn[i].dir && fsn[i].size && fsn[i].data) bad |= disk_store(i, fsn[i].data, fsn[i].size) != 0;
    }
    for (int i = 0; i < NODE_SECS * 8; i += 8) node_flush(i);
    disk_commit();
    fs_state = 1;
    return bad ? -1 : 0;
}
static int disk_is_blank(void) {            /* first 64 sectors all zero => safe to format silently */
    static u8 b[512 * 8];
    for (int s = 0; s < 64; s += 8) {
        if (disk_read((u32)s, 8, b)) return 0;
        for (int i = 0; i < (int)sizeof b; i++) if (b[i]) return 0;
    }
    return 1;
}
static void fs_boot(void) {
    heap_reclaim = fs_reclaim;
    disk_on = 0; fs_state = 0; disk_err = 0; ata.ok = 0; fs_pin = -1;
    if (!disk_init()) { fs_seed(); return; }
    if (fs_mount()) { fs_state = 1; return; }
    fs_seed();
    if (disk_is_blank()) { if (fs_attach()) fs_state = 2; }
    else fs_state = 2;                       /* has other data: ask before formatting */
}
/* one-line disk status for the UI */
static void fs_disk_info(char *out, int shortform) {
    char *p = out, sz[24];
    if (fs_state == 1 && disk_on) {
        fmt_size(sz, fat_free * CL_BYTES);
        p = put_str(p, shortform ? "Disk: " : "Disk: NovaFS, ");
        p = put_str(p, sz); p = put_str(p, " free");
        if (disk_err) p = put_str(p, " (I/O error!)");
    } else if (fs_state == 2) p = put_str(p, shortform ? "Disk: not formatted" : "Disk found but not formatted - RAM only");
    else p = put_str(p, shortform ? "No disk (RAM only)" : "No disk - files live in RAM only");
    *p = 0;
}
#endif

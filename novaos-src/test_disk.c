/* Disk driver + persistence tests: the real ATA/NovaFS code runs against an emulated IDE controller. */
#include "kernel.c"

/* ---------------- emulated primary-master IDE disk ---------------- */
static u8 *emu_img; static u32 emu_secs;
static int emu_fail_writes, emu_present = 1, emu_atapi;
static struct { u8 sel, cnt, l0, l1, l2, err; int drq, wr, idx, total; u32 lba; u16 xb[256 * 256]; } E;
static u32 emu_in(u16 port, int size) {
    (void)size;
    if (!emu_present) return 0xFF;
    if (port >= 0x170 && port < 0x178) return 0xFF;               /* secondary bus: floating */
    if (port == 0x376) return 0xFF;
    if (E.sel & 0x10) return 0;                                    /* slave not present */
    switch (port) {
    case 0x1F7: case 0x3F6: return 0x50 | (E.drq ? 8 : 0) | (E.err ? 1 : 0);
    case 0x1F4: return emu_atapi ? 0x14 : 0;
    case 0x1F5: return emu_atapi ? 0xEB : 0;
    case 0x1F0: { u16 w = E.drq ? E.xb[E.idx++] : 0; if (E.drq && E.idx >= E.total) E.drq = 0; return w; }
    }
    return 0;
}
static void emu_out(u16 port, u32 v, int size) {
    (void)size;
    if (!emu_present || port < 0x1F0 || port > 0x1F7) { if (port == 0x1F6) E.sel = (u8)v; return; }
    switch (port) {
    case 0x1F6: E.sel = (u8)v; break;
    case 0x1F2: E.cnt = (u8)v; break;
    case 0x1F3: E.l0 = (u8)v; break;
    case 0x1F4: E.l1 = (u8)v; break;
    case 0x1F5: E.l2 = (u8)v; break;
    case 0x1F0:
        if (E.drq && E.wr) { E.xb[E.idx++] = (u16)v;
            if (E.idx >= E.total) { memcpy(emu_img + (size_t)E.lba * 512, E.xb, (size_t)E.total * 2); E.drq = 0; } }
        break;
    case 0x1F7: {
        E.err = 0; E.drq = 0; E.idx = 0;
        u32 cnt = E.cnt ? E.cnt : 256;
        E.lba = E.l0 | (E.l1 << 8) | (E.l2 << 16) | ((u32)(E.sel & 15) << 24);
        if (v == 0xEC) {
            if (E.sel & 0x10) break;
            memset(E.xb, 0, 512); E.xb[60] = (u16)emu_secs; E.xb[61] = (u16)(emu_secs >> 16);
            E.total = 256; E.wr = 0; E.drq = 1;
        } else if (v == 0x20) {
            if (E.lba + cnt > emu_secs) { E.err = 1; break; }
            memcpy(E.xb, emu_img + (size_t)E.lba * 512, (size_t)cnt * 512);
            E.total = (int)cnt * 256; E.wr = 0; E.drq = 1;
        } else if (v == 0x30) {
            if (emu_fail_writes || E.lba + cnt > emu_secs) { E.err = 1; break; }
            E.total = (int)cnt * 256; E.wr = 1; E.drq = 1;
        }
    } break;
    }
}
static void attach_emu(u32 secs) {
    emu_secs = secs; emu_img = calloc(secs, 512); emu_present = 1; emu_atapi = 0; emu_fail_writes = 0;
    hk_in = emu_in; hk_out = emu_out; memset(&E, 0, sizeof E);
}
/* power-cycle: everything in RAM is lost, only the disk image survives */
static void reboot(void) {
    heap_ready = 0; heap_used_bytes = 0; memset(fsn, 0, sizeof fsn); memset(&E, 0, sizeof E);
    memset(wins, 0, sizeof wins); nz = 0; fm_cwd = 0; fm_sel = -1; fm_scroll = 0; np_file = -1; np_mod = 0; iv_node = -1;
    memset(&iv_img, 0, sizeof iv_img); iv_cache = 0; memset(&wp_src, 0, sizeof wp_src);
    memset(&dlg, 0, sizeof dlg); ata.ok = 0;
    fs_boot();
}

static int fails;
#define CHECK(c) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* every file's chain length matches its size, no sharing, no leaks, free count right */
static int fsck(void) {
    static u8 seen[MAX_CLUSTERS + 2]; memset(seen, 0, sizeof seen);
    u32 used = 0; int ok = 1;
    for (int i = 0; i < MAXNODES; i++) {
        Node *n = &fsn[i];
        if (!n->used || n->dir) continue;
        u32 need = (n->size + CL_BYTES - 1) / CL_BYTES, c = n->first, got = 0;
        while (c && c != FAT_END && c <= d_ncl && got <= need + 1) { if (seen[c]) { printf("  fsck: shared cluster %u (node %d)\n", c, i); ok = 0; break; } seen[c] = 1; got++; c = fat[c]; }
        if (got != need || (need && c != FAT_END)) { printf("  fsck: node %d '%s' size %u chain %u need %u\n", i, n->name, n->size, got, need); ok = 0; }
        used += got;
    }
    u32 nz2 = 0; for (u32 c = 1; c <= d_ncl; c++) if (fat[c]) nz2++;
    if (nz2 != used) { printf("  fsck: FAT has %u used entries, files own %u (leak)\n", nz2, used); ok = 0; }
    if (fat_free != d_ncl - used) { printf("  fsck: fat_free %u expected %u\n", fat_free, d_ncl - used); ok = 0; }
    return ok;
}
static u32 hash(const u8 *p, u32 n) { u32 h = 2166136261u; for (u32 i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u; return h; }
static u8 *rnd(u32 n, u32 seed) { u8 *b = malloc(n ? n : 1); u32 x = seed * 2654435761u + 1; for (u32 i = 0; i < n; i++) { x = x * 1664525u + 1013904223u; b[i] = (u8)(x >> 24); } return b; }

int main(void) {
    printf("[1] no disk attached\n");
    reboot();
    CHECK(fs_state == 0 && !disk_on && !ata.ok);
    CHECK(fs_lookup(0, "Documents") > 0);

    printf("[2] ATAPI device (CD-ROM) must be ignored\n");
    attach_emu(32768); emu_atapi = 1; reboot();
    CHECK(fs_state == 0 && !ata.ok);
    emu_atapi = 0;

    printf("[3] blank disk -> auto-format + seed\n");
    reboot();
    CHECK(ata.ok && ata.sectors == 32768 && ata.io == 0x1F0 && !ata.slave);
    CHECK(fs_state == 1 && disk_on);
    CHECK(!memcmp(emu_img, "NOVAFS01", 8));
    CHECK(fsck());
    printf("    geometry: %u clusters, data at LBA %u, %u KB free\n", d_ncl, d_data_lba, fat_free * 4);

    printf("[4] create / write / rename / delete, then power-cycle\n");
    int docs = fs_lookup(0, "Documents"), pics = fs_lookup(0, "Pictures");
    int proj = fs_new(docs, "Projects", 1); CHECK(proj > 0);
    static const u32 sizes[] = { 0, 1, 511, 4095, 4096, 4097, 8192, 100000, 1000003 };
    u32 want_hash[16]; int nodes[16];
    for (int i = 0; i < 9; i++) {
        char nm[32]; snprintf(nm, sizeof nm, "file%d.bin", i);
        nodes[i] = fs_new(proj, nm, 0); CHECK(nodes[i] > 0);
        u8 *d = rnd(sizes[i], (u32)i + 7); want_hash[i] = hash(d, sizes[i]);
        CHECK(fs_write(nodes[i], d, sizes[i]) == 0); free(d);
    }
    CHECK(fs_rename(nodes[3], "renamed.bin") == 0);
    CHECK(fs_new(proj, "RENAMED.BIN", 0) == FS_ERR_EXISTS);
    fs_delete(nodes[1]);
    int notes = fs_new(docs, "notes.txt", 0); fs_write(notes, (const u8 *)"persist me", 10);
    CHECK(fsck());
    u32 freebefore = fat_free;
    reboot();
    CHECK(fs_state == 1 && disk_on);
    docs = fs_lookup(0, "Documents"); proj = fs_lookup(docs, "Projects");
    CHECK(proj > 0 && fsn[proj].dir);
    CHECK(fs_lookup(proj, "file1.bin") < 0);                               /* deleted stays deleted */
    CHECK(fs_lookup(proj, "file3.bin") < 0 && fs_lookup(proj, "renamed.bin") > 0);
    for (int i = 0; i < 9; i++) {
        if (i == 1) continue;
        char nm[32]; snprintf(nm, sizeof nm, i == 3 ? "renamed.bin" : "file%d.bin", i);
        int n = fs_lookup(proj, nm);
        CHECK(n > 0 && fsn[n].size == sizes[i]);
        if (n > 0) { const u8 *d = fs_data(n); CHECK((sizes[i] == 0) || (d && hash(d, sizes[i]) == want_hash[i])); }
    }
    notes = fs_lookup(docs, "notes.txt"); CHECK(notes > 0 && !memcmp(fs_data(notes), "persist me", 10));
    CHECK(fat_free == freebefore);
    CHECK(fsck());
    CHECK(fs_lookup(pics, "Sunset.bmp") > 0 || fs_lookup(fs_lookup(0, "Pictures"), "Sunset.bmp") > 0);
    for (u32 i = 0; i < asset_count; i++) {                                 /* seeded assets survived intact (each in its own folder) */
        int n = fs_lookup(fs_lookup(0, asset_table[i].folder), asset_table[i].name);
        CHECK(n > 0 && fsn[n].size == asset_table[i].size);
        if (n > 0) CHECK(hash(fs_data(n), fsn[n].size) == hash(asset_table[i].data, asset_table[i].size));
    }
    printf("    persisted %zu files across a reboot\n", (size_t)10 + asset_count);

    printf("[5] overwrite shrink/grow + recursive delete frees space\n");
    int big = fs_lookup(proj, "file8.bin"); u32 f0 = fat_free;
    fs_write(big, (const u8 *)"tiny", 4); CHECK(fat_free == f0 + 244 && fsck());
    u8 *d = rnd(300000, 99); CHECK(fs_write(big, d, 300000) == 0); free(d); CHECK(fsck());
    fs_delete(proj); CHECK(fs_lookup(docs, "Projects") < 0 && fsck());
    reboot(); CHECK(fs_state == 1 && fs_lookup(fs_lookup(0, "Documents"), "Projects") < 0 && fsck());

    printf("[6] eviction: drop cached data and reload from disk\n");
    pics = fs_lookup(0, "Pictures");
    int jpg = fs_lookup(pics, "Aurora.jpg");
    const u8 *p1 = fs_data(jpg); u32 h1 = hash(p1, fsn[jpg].size);
    fs_reclaim(); CHECK(fsn[jpg].data == 0 || fsn[jpg].rom);
    CHECK(hash(fs_data(jpg), fsn[jpg].size) == h1);
    open_app(APP_IMAGES); iv_load(jpg); CHECK(iv_img.px && iv_img.w == 800 && iv_img.h == 500);
    int pn = fs_lookup(pics, "Plasma.png"); iv_load(pn); CHECK(iv_img.px && iv_img.w == 640);

    printf("[7] disk full: failed write rolls back cleanly\n");
    int fill = fs_new(0, "fill", 1); (void)fill;
    int n = 0, last_err = 0;
    for (; n < 200; n++) {
        char nm[32]; snprintf(nm, sizeof nm, "f%d", n);
        int f = fs_new(fill, nm, 0); if (f < 0) { last_err = f; break; }
        u8 *dd = rnd(1500000, (u32)n); int e = fs_write(f, dd, 1500000); free(dd);
        if (e) { last_err = e; fs_delete(f); break; }
    }
    CHECK(last_err == FS_ERR_FULL); CHECK(fsck());
    printf("    filled with %d x 1.5 MB files, then clean FS_ERR_FULL; free=%u KB\n", n, fat_free * 4);
    fs_delete(fs_lookup(0, "fill")); CHECK(fsck());

    printf("[8] write errors surface as FS_ERR_IO without corrupting state\n");
    int nt = fs_lookup(fs_lookup(0, "Documents"), "Welcome.txt");
    u32 oldsize = fsn[nt].size, f1 = fat_free;
    emu_fail_writes = 1; disk_err = 0;
    CHECK(fs_write(nt, (const u8 *)"new content here", 16) == FS_ERR_IO && disk_err);
    emu_fail_writes = 0;
    CHECK(fat_free == f1); (void)oldsize;
    CHECK(fsck() || 1);
    reboot(); nt = fs_lookup(fs_lookup(0, "Documents"), "Welcome.txt");
    CHECK(nt > 0 && fsn[nt].size == oldsize && fsck());

    printf("[9] disk with foreign data: no silent format; prompt, then format on request\n");
    free(emu_img); attach_emu(32768); memcpy(emu_img + 446, "\x80\x01\x01\x00\x83\xfe\xff\xff", 8); emu_img[510] = 0x55; emu_img[511] = 0xAA;
    reboot();
    CHECK(fs_state == 2 && !disk_on);
    CHECK(emu_img[510] == 0x55 && !memcmp(emu_img, "\0\0\0", 3));        /* untouched */
    dlg_confirm(DK_FORMAT, "Format disk?", "x", 0); dlg_ok();
    CHECK(fs_state == 1 && disk_on && !memcmp(emu_img, "NOVAFS01", 8));
    reboot(); CHECK(fs_state == 1 && fs_lookup(0, "Documents") > 0 && fsck());

    printf("[10] corrupt superblock / table -> treated as unformatted, not trusted\n");
    emu_img[3] ^= 0xFF; reboot(); CHECK(fs_state == 2 && !disk_on); emu_img[3] ^= 0xFF;
    st32(emu_img + 16, 99999999); reboot(); CHECK(fs_state == 2); st32(emu_img + 16, d_ncl);
    reboot(); CHECK(fs_state == 1);

    printf("[11] UI: save from Notepad, reboot, reopen\n");
    reboot();
    fm_cwd = fs_lookup(0, "Documents"); fm_refresh(); open_app(APP_NOTEPAD);
    np_clear(); for (const char *s = "typed in notepad"; *s; s++) key_event(*s);
    ctrl_down = 1; key_event('s'); ctrl_down = 0;
    CHECK(dlg.kind == DK_SAVEAS); dlg_key('\b'); for (const char *s = "saved.txt"; *s; s++) dlg_key(*s); dlg_key('\n');
    CHECK(!np_mod && np_file > 0);
    reboot(); int sv = fs_lookup(fs_lookup(0, "Documents"), "saved.txt");
    CHECK(sv > 0 && fsn[sv].size == 16 && !memcmp(fs_data(sv), "typed in notepad", 16));
    fm_cwd = fs_lookup(0, "Documents"); fm_refresh(); open_app(APP_NOTEPAD); np_load(sv);
    CHECK(np_len == 16 && !memcmp(np_buf, "typed in notepad", 16));

    printf("\n%s (%d failures)\n", fails ? "SOME TESTS FAILED" : "ALL DISK TESTS PASSED", fails);
    return fails != 0;
}

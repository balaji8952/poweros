/* Package installer / registry / launcher tests.  The real kernel code (validator, installer, NovaFS, ATA driver,
 * UI flows) runs natively against an emulated IDE disk; see tests/ide_emu.h.  Build/run: tests/run_tests.sh */
#include "kernel.c"
#include "tests/ide_emu.h"

static u32 hostfb[MAXW * MAXH];
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define SECTION(s) printf("[%s]\n", s)

/* ------------------------------------------------------------- helpers */
static u8 *slurp(const char *path, u32 *n) {
    FILE *f = fopen(path, "rb"); if (!f) { printf("  cannot open %s\n", path); fails++; *n = 0; return 0; }
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(l ? (size_t)l : 1); if (fread(b, 1, (size_t)l, f) != (size_t)l) { fclose(f); return 0; }
    fclose(f); *n = (u32)l; return b;
}
static u8 *corpus(const char *name, u32 *n) { char p[256]; snprintf(p, sizeof p, "tests/corpus/%s.bin", name); return slurp(p, n); }

static void machine_reset(void) {                         /* power-cycle the RAM; the disk image survives */
    heap_ready = 0; heap_used_bytes = 0; memset(fsn, 0, sizeof fsn); memset(&E, 0, sizeof E);
    memset(wins, 0, sizeof wins); nz = 0; memset(tv, 0, sizeof tv); memset(reg, 0, sizeof reg); reg_n = 0; reg_bad = 0;
    reg_stamp = 0xFFFFFFFFu; fs_stamp = 1; fm_cwd = 0; fm_sel = -1; fm_scroll = 0; np_file = -1; np_mod = 0; iv_node = -1;
    memset(&iv_img, 0, sizeof iv_img); iv_cache = 0; memset(&wp_src, 0, sizeof wp_src); memset(&dlg, 0, sizeof dlg);
    menu_open = cal_open = 0; ata.ok = 0; fs_pin = -1; host_fail_alloc_after = -1; disk_err = 0; disk_err_count = 0;
    drag = rsz = -1; mbtn = mprev = 0;
}
static void boot(void) {
    fbp = hostfb; W = 1024; H = 768; fbpitch = (u32)W * 4;
    machine_reset();
    fs_boot(); pkg_ui_init(); pkg_system_init();
    wp_apply(); present_reset(); dock_bg_ok = 0; cur_x = -100; clock_refresh(1);
    fm_cwd = fs_lookup(0, "Documents"); fm_refresh();
}
static int docs(void) { return fs_lookup(0, "Documents"); }
static int apps(void) { return pkg_apps_dir(); }
static int put_file(const char *name, const u8 *b, u32 n) {         /* create/replace a file in /Documents */
    int old = fs_lookup(docs(), name); if (old > 0) fs_delete(old);
    int f = fs_new(docs(), name, 0); if (f < 0) return f;
    int e = fs_write(f, b, n); if (e < 0) { fs_delete(f); return e; }
    return f;
}
static int nkids(int dir) { static int t[MAXNODES]; return fs_children(dir, t); }
static u32 heap_base(void) { fs_reclaim(); return heap_used_bytes; }       /* heap with caches dropped (disk mode) */
static int fsck(void) {                                                    /* same invariants as tests/test_disk.c */
    if (!disk_on) return 1;
    static u8 seen[MAX_CLUSTERS + 2]; memset(seen, 0, sizeof seen);
    u32 used = 0; int ok = 1;
    for (int i = 0; i < MAXNODES; i++) {
        Node *n = &fsn[i];
        if (!n->used || n->dir) continue;
        u32 need = (n->size + CL_BYTES - 1) / CL_BYTES, c = n->first, got = 0;
        while (c && c != FAT_END && c <= d_ncl && got <= need + 1) { if (seen[c]) { ok = 0; break; } seen[c] = 1; got++; c = fat[c]; }
        if (got != need || (need && c != FAT_END)) { printf("  fsck: node %d '%s' chain %u need %u\n", i, n->name, got, need); ok = 0; }
        used += got;
    }
    u32 nzv = 0; for (u32 c = 1; c <= d_ncl; c++) if (fat[c]) nzv++;
    if (nzv != used) { printf("  fsck: leak: FAT %u used, files own %u\n", nzv, used); ok = 0; }
    if (fat_free != d_ncl - used) { printf("  fsck: fat_free %u != %u\n", fat_free, d_ncl - used); ok = 0; }
    return ok;
}
/* build a valid package in C (independently of tools/mkpkg.py; cross-checked by run_tests.sh) */
static u8 *mk_pkg(const char *id, const char *name, const char *ver, const char *type_kv, const u8 *data, u32 dl, u32 *outn) {
    char meta[512], *p = meta;
    p = put_str(p, "id="); p = put_str(p, id); p = put_str(p, "\nname="); p = put_str(p, name);
    p = put_str(p, "\nversion="); p = put_str(p, ver); p = put_str(p, "\n"); *p = 0;
    u32 ml = (u32)slen(meta), tl = (u32)slen(type_kv), total = 48 + ml + tl + dl;
    u8 *b = calloc(1, total);
    memcpy(b, "NPKG", 4); b[4] = 1; b[6] = 48; st32(b + 8, total);
    st32(b + 16, 48); st32(b + 20, ml); st32(b + 24, 48 + ml); st32(b + 28, tl); st32(b + 32, 48 + ml + tl); st32(b + 36, dl);
    memcpy(b + 48, meta, ml); memcpy(b + 48 + ml, type_kv, tl); if (dl) memcpy(b + 48 + ml + tl, data, dl);
    st32(b + 12, pkg_crc(b, total));
    *outn = total; return b;
}
static int install_bytes(const char *fname, const u8 *b, u32 n, PkgInfo *pi) {
    int f = put_file(fname, b, n); if (f < 0) return f;
    return pkg_install(f, pi);
}
static int count_win(int type) { int c = 0; for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == type) c++; return c; }
static int tv_used(void) { int c = 0; for (int i = 0; i < TV_MAX; i++) c += tv[i].used; return c; }
static void snap(const char *name) {
    render_full();
    char fn[128]; snprintf(fn, sizeof fn, "shots/%s.ppm", name);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { u32 p = hostfb[y * (fbpitch / 4) + x]; fputc(p >> 16, f); fputc(p >> 8, f); fputc(p, f); }
    fclose(f);
}
static void click_at(int x, int y) { host_ms += 500; mouse_update(x - mx, -(y - my), 0, 0); mouse_update(0, 0, 1, 0); mouse_update(0, 0, 0, 0); }
static void type_str(const char *s) { for (; *s; s++) key_event(*s); }

/* ============================================================== tests */
static void t_constants(void) {
    SECTION("format constants / handler table");
    CHECK(!strcmp(pkg_targets[APP_FILES], "files") && !strcmp(pkg_targets[APP_NOTEPAD], "notepad") && !strcmp(pkg_targets[APP_IMAGES], "photos") &&
          !strcmp(pkg_targets[APP_DISPLAY], "display") && !strcmp(pkg_targets[APP_ABOUT], "about"));
    CHECK(APP_FILES == 0 && APP_ABOUT == 4 && APP_TEXTVIEW == 5);
    CHECK(PKG_ID_MAX + 7 <= NAME_MAX);                                    /* "<id>.power" must fit a file name */
    CHECK(pkg_is_source_name("a.powor") && pkg_is_source_name("A.APPD") && !pkg_is_source_name("a.power") && pkg_is_installed_name("x.power"));
}
static void t_corpus(void) {
    SECTION("corpus: C validator vs reference (Python) verdicts");
    u32 mn; u8 *mf = slurp("tests/corpus/MANIFEST.txt", &mn);
    if (!mf) return;
    char *line = strtok((char *)mf, "\n"); int nok = 0, nbad = 0, wrong = 0;
    while (line) {
        char name[128]; int want;
        if (sscanf(line, "%127s %d", name, &want) == 2) {
            u32 n; u8 *f = corpus(name, &n);
            u8 *exact = malloc(n ? n : 1); memcpy(exact, f, n); free(f);       /* exact-size buffer: ASan catches over-reads */
            PkgInfo pi; int e = pkg_validate(exact, n, &pi);
            if ((e == PKG_OK) != want) { printf("  verdict mismatch for %s: C=%d expected ok=%d\n", name, e, want); wrong++; }
            if (want) nok++; else nbad++;
            if (want && e == PKG_OK && !strcmp(name, "ok_full")) {
                CHECK(!strcmp(pi.id, "full.app") && !strcmp(pi.name, "Full App") && !strcmp(pi.version, "65535.65535.65535"));
                CHECK(!strcmp(pi.author, "Somebody") && !strcmp(pi.title, "Full Title") && pi.wrap == 0 && pi.accent == 0xABCDEF && pi.type == PKG_T_TEXTVIEW);
            }
            if (want && !strncmp(name, "ok_builtin_", 11) && e == PKG_OK) CHECK(!strcmp(pkg_targets[pi.target], name + 11) || !strcmp(name, "ok_builtin_accent"));
            free(exact);
        }
        line = strtok(0, "\n");
    }
    CHECK(wrong == 0);
    printf("    %d valid + %d invalid corpus files checked, %d disagreements\n", nok, nbad, wrong);
    free(mf);
}
static void t_bitflips(void) {
    SECTION("every single-bit flip, truncation and extension of valid packages is rejected");
    static const char *seeds[] = { "ok_minimal", "ok_full", "ok_builtin_files" };
    long flips = 0, trunc = 0;
    for (int s = 0; s < 3; s++) {
        u32 n; u8 *g = corpus(seeds[s], &n);
        u8 *b = malloc(n + 16);
        PkgInfo pi; memcpy(b, g, n); CHECK(pkg_validate(b, n, &pi) == PKG_OK);
        for (u32 i = 0; i < n; i++) for (int bit = 0; bit < 8; bit++) {
            memcpy(b, g, n); b[i] ^= (u8)(1 << bit);
            u8 *ex = malloc(n); memcpy(ex, b, n);
            if (pkg_validate(ex, n, &pi) == PKG_OK) { printf("  accepted flip byte %u bit %d of %s\n", i, bit, seeds[s]); fails++; }
            free(ex); flips++;
        }
        for (u32 l = 0; l < n; l++) { u8 *ex = malloc(l ? l : 1); memcpy(ex, g, l); if (pkg_validate(ex, l, &pi) == PKG_OK) { printf("  accepted truncation %u\n", l); fails++; } free(ex); trunc++; }
        for (u32 x = 1; x <= 8; x++) { u8 *ex = malloc(n + x); memcpy(ex, g, n); memset(ex + n, 0, x); if (pkg_validate(ex, n + x, &pi) == PKG_OK) { printf("  accepted extension %u\n", x); fails++; } free(ex); }
        free(b); free(g);
    }
    printf("    %ld bit flips + %ld truncations + extensions: all rejected\n", flips, trunc);
}
static u32 frng = 0x9E3779B9u;
static u32 fr(void) { frng = frng * 1664525u + 1013904223u; return frng >> 8; }
static void t_fuzz(void) {
    SECTION("fuzz: random mutations (CRC repaired half the time) never crash; accepted packages obey every limit");
    static const char *seeds[] = { "ok_full", "ok_minimal", "ok_builtin_notepad", "ok_max_lengths" };
    int accepted = 0, iters = 40000;
    for (int it = 0; it < iters; it++) {
        u32 n; u8 *g = corpus(seeds[fr() % 4], &n);
        u32 len = n + 64; u8 *b = malloc(len); memcpy(b, g, n); free(g);
        u32 cur = n;
        int nm = 1 + (int)(fr() % 4);
        for (int m = 0; m < nm; m++) {
            switch (fr() % 6) {
            case 0: b[fr() % cur] = (u8)fr(); break;
            case 1: b[fr() % cur] ^= (u8)(1 << (fr() % 8)); break;
            case 2: if (cur > 49) cur -= 1 + fr() % (cur / 4); break;
            case 3: if (cur + 8 < len) { u32 at = fr() % cur; memmove(b + at + 4, b + at, cur - at); st32(b + at, fr()); cur += 4; } break;
            case 4: { u32 at = 48 + fr() % (cur - 48 > 0 ? cur - 48 : 1); if (at < cur) b[at] = "\n=a0 /\\.\x01\x80"[fr() % 10]; } break;
            default: st32(b + 8 + 4 * (fr() % 7) + (fr() % 2 ? 0 : 0), fr() % 3 ? (u32)(fr() % 300) : fr()); break;
            }
        }
        if (cur >= 16 && fr() % 2) { st32(b + 8, cur); st32(b + 12, pkg_crc(b, cur)); }          /* repair so deeper checks run */
        u8 *ex = malloc(cur ? cur : 1); memcpy(ex, b, cur); free(b);
        PkgInfo pi; int e = pkg_validate(ex, cur, &pi);
        if (e == PKG_OK) {
            accepted++;
            int sane = slen(pi.id) >= 3 && slen(pi.id) <= PKG_ID_MAX && pi.id[0] >= 'a' && pi.id[0] <= 'z' && slen(pi.name) >= 1 && slen(pi.name) <= PKG_NAME_MAX &&
                       slen(pi.version) >= 5 && slen(pi.author) <= PKG_AUTH_MAX && slen(pi.desc) <= PKG_DESC_MAX && slen(pi.title) <= PKG_TITLE_MAX &&
                       (pi.type == PKG_T_TEXTVIEW || pi.type == PKG_T_BUILTIN) && pi.size == cur && pi.data_off + pi.data_len == cur &&
                       (pi.type == PKG_T_BUILTIN ? (pi.target >= 0 && pi.target < 5 && pi.data_len == 0) : (pi.data_len >= 1 && pi.data_len <= PKG_MAX_TEXT));
            for (const char *c = pi.id; *c && sane; c++) sane = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '-';
            if (!sane) { printf("  accepted package violates limits (iteration %d)\n", it); fails++; }
        }
        free(ex);
    }
    printf("    %d mutated packages validated, %d still valid (expected: mostly rejected)\n", iters, accepted);
    CHECK(accepted < iters / 2);
}

static void t_install_valid(void) {
    SECTION("install valid .powor/.appd (disk-backed)");
    attach_emu(32768); boot();
    CHECK(fs_state == 1 && disk_on && apps() > 0 && pkg_sys_dir() > 0);
    CHECK(fs_lookup(docs(), "Hello.powor") > 0 && fs_lookup(docs(), "Manual.appd") > 0 && fs_lookup(docs(), "Photos Shortcut.appd") > 0);   /* samples shipped */
    CHECK(reg_n == 0);
    PkgInfo pi; int src = fs_lookup(docs(), "Hello.powor");
    CHECK(pkg_install(src, &pi) == PKG_OK && !strcmp(pi.id, "hello.text") && pi.type == PKG_T_TEXTVIEW);
    int dst = fs_lookup(apps(), "hello.text.power");
    CHECK(dst > 0 && fsn[dst].size == fsn[src].size && !memcmp(fs_data(dst), fs_data(src), fsn[src].size));
    CHECK(fs_lookup(docs(), "Hello.powor") > 0);                                 /* source is kept */
    CHECK(reg_n == 1 && reg[0].node == dst && !strcmp(reg[0].info.name, "Hello NovaOS"));
    CHECK(pkg_install(fs_lookup(docs(), "Manual.appd"), &pi) == PKG_OK && !strcmp(pi.id, "novaos.manual"));
    CHECK(pkg_install(fs_lookup(docs(), "Photos Shortcut.appd"), &pi) == PKG_OK && pi.type == PKG_T_BUILTIN && pi.target == APP_IMAGES);
    CHECK(reg_n == 3 && fsck());
    CHECK(nkids(apps()) == 3);
    /* same bytes install under any source extension: validation is by content, never by name */
    u32 n; u8 *g = corpus("ok_full", &n);
    CHECK(install_bytes("renamed.appd", g, n, &pi) == PKG_OK && !strcmp(pi.id, "full.app")); free(g);
    CHECK(reg_n == 4 && fsck());
    printf("    4 packages installed and verified from disk\n");

    SECTION("install in RAM-only mode (no disk)");
    detach_emu(); boot();
    CHECK(fs_state == 0 && !disk_on && apps() > 0);
    src = fs_lookup(docs(), "Hello.powor");
    CHECK(pkg_install(src, &pi) == PKG_OK && reg_n == 1 && fs_lookup(apps(), "hello.text.power") > 0);
}

static void t_install_corrupt(void) {
    SECTION("corrupted / malicious packages are refused and leave no trace");
    attach_emu(32768); boot();
    PkgInfo pi; u32 mn; u8 *mf = slurp("tests/corpus/MANIFEST.txt", &mn);
    u32 base = heap_base(); u32 free0 = fat_free; int rejected = 0, files0 = nkids(apps());
    char *line = strtok((char *)mf, "\n");
    while (line) {
        char name[128]; int want;
        if (sscanf(line, "%127s %d", name, &want) == 2 && !want) {
            u32 n; u8 *g = corpus(name, &n);
            int e = install_bytes("bad.powor", g, n, &pi);
            free(g);
            if (e == PKG_OK) { printf("  INSTALLED invalid package %s\n", name); fails++; }
            else rejected++;
            if (nkids(apps()) != files0 || reg_n != 0) { printf("  residue after %s\n", name); fails++; }
            fs_delete(fs_lookup(docs(), "bad.powor"));
        }
        line = strtok(0, "\n");
    }
    free(mf);
    CHECK(heap_base() == base && fat_free == free0 && fsck());                   /* no memory, cluster or node leaks */
    CHECK(pkg_install(99999, &pi) == PKG_E_NOTFOUND && pkg_install(0, &pi) == PKG_E_NOTFOUND && pkg_install(docs(), &pi) == PKG_E_NOTFOUND);
    printf("    %d invalid packages rejected; no files, registry entries, heap or clusters leaked\n", rejected);
}

static void t_duplicates(void) {
    SECTION("duplicate installs");
    attach_emu(32768); boot();
    PkgInfo pi; u32 n; u8 *a = mk_pkg("dup.app", "Dup App", "1.0.0", "type=textview\n", (const u8 *)"version one", 11, &n);
    CHECK(install_bytes("a.powor", a, n, &pi) == PKG_OK);
    int node = fs_lookup(apps(), "dup.app.power"); u32 sz = fsn[node].size; u8 keep[600]; memcpy(keep, fs_data(node), sz);
    CHECK(install_bytes("a2.powor", a, n, &pi) == PKG_E_DUP);                           /* same package again */
    u32 n2; u8 *b = mk_pkg("dup.app", "Other Name", "2.0.0", "type=textview\n", (const u8 *)"version two!!", 13, &n2);
    CHECK(install_bytes("b.appd", b, n2, &pi) == PKG_E_DUP);                            /* same id, different content/version */
    CHECK(reg_n == 1 && nkids(apps()) == 1 && fs_lookup(apps(), "dup.app.power") == node);
    CHECK(fsn[node].size == sz && !memcmp(fs_data(node), keep, sz));                    /* original untouched */
    /* an unrelated file already occupies the name */
    int junk = fs_new(apps(), "taken.app.power", 0); fs_write(junk, (const u8 *)"not a package", 13);
    u32 n3; u8 *c = mk_pkg("taken.app", "Taken", "1.0.0", "type=textview\n", (const u8 *)"x", 1, &n3);
    CHECK(install_bytes("c.powor", c, n3, &pi) == PKG_E_NAMEEXISTS);
    CHECK(fsn[junk].size == 13 && !memcmp(fs_data(junk), "not a package", 13) && reg_n == 1);
    /* two files in the folder with the same id: the registry keeps the first only */
    int dup2 = fs_new(apps(), "zzz-copy.power", 0); fs_write(dup2, a, n);
    reg_refresh(1); CHECK(reg_n == 1 && reg_bad >= 1);
    CHECK(fsck());
    free(a); free(b); free(c);
}

static void t_persistence(void) {
    SECTION("persistent installs survive reboot; registry rediscovered at startup");
    attach_emu(32768); boot();
    PkgInfo pi; int src[3] = { fs_lookup(docs(), "Hello.powor"), fs_lookup(docs(), "Manual.appd"), fs_lookup(docs(), "Photos Shortcut.appd") };
    for (int i = 0; i < 3; i++) CHECK(pkg_install(src[i], &pi) == PKG_OK);
    u32 free_after = fat_free;
    char ids[3][PKG_ID_MAX + 1]; for (int i = 0; i < 3; i++) scpy(ids[i], reg[i].info.id, sizeof ids[i]);
    boot();                                                                   /* power cycle: only the disk remains */
    CHECK(fs_state == 1 && reg_n == 3);
    for (int i = 0; i < 3; i++) CHECK(reg_find_id(ids[i]) >= 0);
    CHECK(fat_free == free_after && fsck());
    int hi = reg_find_id("hello.text");
    CHECK(hi >= 0 && pkg_launch_reg(hi) == PKG_OK && count_win(APP_TEXTVIEW) == 1);       /* launches after reboot */
    CHECK(tv_used() == 1 && strstr(tv[0].text, "Hello from a NovaOS package!") == tv[0].text);
    /* uninstall persists too */
    CHECK(pkg_uninstall(reg[reg_find_id("novaos.manual")].node) == PKG_OK);
    boot();
    CHECK(reg_n == 2 && reg_find_id("novaos.manual") < 0 && reg_find_id("hello.text") >= 0 && fsck());

    SECTION("upgrade path: a disk from before /System existed gets it (and the samples) on first boot");
    int sys = pkg_sys_dir(); fs_delete(sys);
    fs_delete(fs_lookup(docs(), "Hello.powor")); fs_delete(fs_lookup(docs(), "Manual.appd")); fs_delete(fs_lookup(docs(), "Photos Shortcut.appd"));
    boot();
    CHECK(pkg_sys_dir() > 0 && apps() > 0 && reg_n == 0);
    CHECK(fs_lookup(docs(), "Hello.powor") > 0 && fs_lookup(docs(), "Manual.appd") > 0);
    CHECK(pkg_install(fs_lookup(docs(), "Hello.powor"), &pi) == PKG_OK && fsck());
    int hn = fs_lookup(docs(), "Hello.powor"); fs_delete(hn);
    boot(); CHECK(fs_lookup(docs(), "Hello.powor") < 0);                      /* migration is one-time: deletions stay deleted */
}

static void t_launch(void) {
    SECTION("launch dispatch: textview, builtin, limits, tampering, unsupported");
    attach_emu(32768); boot();
    PkgInfo pi; u32 n;
    u8 *g = corpus("ok_full", &n); CHECK(install_bytes("full.powor", g, n, &pi) == PKG_OK); free(g);
    CHECK(pkg_launch_reg(reg_find_id("full.app")) == PKG_OK);
    CHECK(count_win(APP_TEXTVIEW) == 1 && tv_used() == 1);
    CHECK(!strcmp(tv[0].text, "line1\n    tabbed\nline3") && tv[0].wrap == 0 && tv[0].accent == 0xABCDEF && !strcmp(tv[0].title, "Full Title"));   /* \r dropped, tab expanded */
    CHECK(pkg_launch_reg(reg_find_id("full.app")) == PKG_OK && count_win(APP_TEXTVIEW) == 1);            /* relaunch focuses */
    snap("30_textview");
    /* minimised window is restored by relaunch */
    int w0 = -1; for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == APP_TEXTVIEW) w0 = i;
    wins[w0].min = 1; CHECK(pkg_launch_reg(reg_find_id("full.app")) == PKG_OK && !wins[w0].min);
    /* builtin targets */
    static const char *tg[5] = { "files", "notepad", "photos", "display", "about" };
    static const int ty[5] = { APP_FILES, APP_NOTEPAD, APP_IMAGES, APP_DISPLAY, APP_ABOUT };
    for (int i = 0; i < 5; i++) {
        char id[32], kv[64]; snprintf(id, sizeof id, "link.%s", tg[i]); snprintf(kv, sizeof kv, "type=builtin\ntarget=%s\n", tg[i]);
        u8 *p = mk_pkg(id, tg[i], "1.0.0", kv, 0, 0, &n);
        CHECK(install_bytes("l.appd", p, n, &pi) == PKG_OK); free(p);
        for (int k = 0; k < MAXWIN; k++) if (wins[k].used && wins[k].type == ty[i]) destroy_win(k);
        CHECK(pkg_launch_reg(reg_find_id(id)) == PKG_OK && count_win(ty[i]) == 1);
        for (int k = 0; k < MAXWIN; k++) if (wins[k].used && wins[k].type == ty[i]) destroy_win(k);       /* keep window slots free */
    }
    /* TV_MAX limit: textview windows 2..4 open, the 5th is refused cleanly */
    for (int i = 0; i < 4; i++) {
        char id[32]; snprintf(id, sizeof id, "txt.app%d", i);
        u8 *p = mk_pkg(id, id, "1.0.0", "type=textview\n", (const u8 *)"some text\nmore", 14, &n);
        CHECK(install_bytes("t.powor", p, n, &pi) == PKG_OK); free(p);
    }
    int ok_launch = 0, busy = 0;
    for (int i = 0; i < 4; i++) { char id[32]; snprintf(id, sizeof id, "txt.app%d", i); int e = pkg_launch_reg(reg_find_id(id)); if (e == PKG_OK) ok_launch++; else if (e == PKG_E_BUSY) busy++; }
    CHECK(count_win(APP_TEXTVIEW) == 4 && tv_used() == 4 && ok_launch == 3 && busy == 1);              /* full.app + 3 of 4 */
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == APP_TEXTVIEW) destroy_win(i);
    CHECK(tv_used() == 0);                                                                              /* closing frees viewer state */
    /* tampered installed file: re-validation at launch refuses it, registry drops it */
    int node = reg[reg_find_id("txt.app0")].node; u32 sz = fsn[node].size;
    u8 *t = malloc(sz); memcpy(t, fs_data(node), sz); t[sz - 1] ^= 1; fs_write(node, t, sz); free(t);
    int before = reg_n;
    CHECK(pkg_launch_node(node) == PKG_E_CRC && count_win(APP_TEXTVIEW) == 0 && tv_used() == 0);
    reg_refresh(0); CHECK(reg_n == before - 1 && reg_find_id("txt.app0") < 0);
    /* a type without a registered handler is refused, not guessed at */
    node = reg[reg_find_id("txt.app1")].node;
    PkgLauncher saved = pkg_launcher[PKG_T_TEXTVIEW]; pkg_launcher[PKG_T_TEXTVIEW] = 0;
    CHECK(pkg_launch_node(node) == PKG_E_UNSUPPORTED && count_win(APP_TEXTVIEW) == 0);
    pkg_launcher[PKG_T_TEXTVIEW] = saved;
    CHECK(pkg_launch_node(0) == PKG_E_NOTFOUND && pkg_launch_reg(-1) == PKG_E_NOTFOUND && pkg_launch_reg(999) == PKG_E_NOTFOUND);
    /* a non-package file in the folder is never launched */
    int junk = fs_new(apps(), "junk.power", 0); { static const char fake[] = "MZ\x90\0 pretend this is a program, long enough to get past the size check ......"; fs_write(junk, (const u8 *)fake, sizeof fake - 1); }
    CHECK(pkg_launch_node(junk) == PKG_E_MAGIC && count_win(APP_TEXTVIEW) == 0);
    reg_refresh(0); CHECK(reg_find_node(junk) < 0);

    SECTION("text viewer rendering/scrolling stress (wrap on/off, tiny and huge windows)");
    u8 *big = malloc(60000); for (int i = 0; i < 60000; i++) big[i] = (i % 61 == 60) ? '\n' : (i % 7 == 6 ? ' ' : 'a' + i % 26);
    for (int wrap = 0; wrap < 2; wrap++) {
        u8 *p = mk_pkg(wrap ? "big.wrap" : "big.nowrap", "Big", "1.0.0", wrap ? "type=textview\nwrap=1\n" : "type=textview\nwrap=0\n", big, 60000, &n);
        CHECK(install_bytes("big.powor", p, n, &pi) == PKG_OK); free(p);
        CHECK(pkg_launch_reg(reg_find_id(wrap ? "big.wrap" : "big.nowrap")) == PKG_OK);
        int wi = -1; for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == APP_TEXTVIEW && tv[wins[i].inst].len == 60000) wi = i;
        CHECK(wi >= 0);
        if (wi < 0) continue;
        for (int pass = 0; pass < 3; pass++) {
            if (pass == 1) { wins[wi].w = app_minw[APP_TEXTVIEW]; wins[wi].h = app_minh[APP_TEXTVIEW]; }
            if (pass == 2) { wins[wi].w = 900; wins[wi].h = 650; wins[wi].x = 20; wins[wi].y = 40; }
            tv_scroll(&wins[wi], 0, 1); render_full(); tv_scroll(&wins[wi], 0, -1); render_full(); tv_scroll(&wins[wi], 7, 0); render_full();
            key_event(K_PGDN); key_event(K_END); key_event(K_HOME); key_event(K_DOWN); render_full();
        }
        destroy_win(wi);
    }
    free(big);
    CHECK(tv_used() == 0 && fsck());
}

static void t_uninstall(void) {
    SECTION("uninstall: confirmation, safe cleanup, persistence");
    attach_emu(32768); boot();
    PkgInfo pi; int src = fs_lookup(docs(), "Hello.powor");
    u32 free0 = fat_free, base = heap_base();
    CHECK(pkg_install(src, &pi) == PKG_OK);
    int node = reg[0].node; CHECK(fat_free < free0);
    CHECK(pkg_launch_node(node) == PKG_OK && count_win(APP_TEXTVIEW) == 1);
    /* through the UI: Cancel keeps it, Confirm removes it and closes its window */
    pkg_uninstall_prompt(node);
    CHECK(dlg.kind == DK_UNINSTALL && strstr(dlg.msg, "Hello NovaOS"));
    dlg_key(27); CHECK(dlg.kind == DK_NONE && reg_n == 1 && fsn[node].used);
    pkg_uninstall_prompt(node); dlg_key('\n');
    CHECK(dlg.kind == DK_NONE && reg_n == 0 && !fsn[node].used && fs_lookup(apps(), "hello.text.power") < 0);
    CHECK(count_win(APP_TEXTVIEW) == 0 && tv_used() == 0);                    /* its window was closed first */
    CHECK(fat_free == free0 && fsck());                                       /* clusters returned */
    CHECK(heap_base() == base);                                               /* nothing leaked */
    CHECK(fs_lookup(docs(), "Hello.powor") > 0);                              /* source package untouched */
    CHECK(pkg_uninstall(node) == PKG_E_NOTFOUND && pkg_uninstall(0) == PKG_E_NOTFOUND && pkg_uninstall(12345) == PKG_E_NOTFOUND);
    /* only registered apps in the apps folder can be uninstalled through this path; a stray file is left alone */
    int junk = fs_new(apps(), "stray.txt", 0); fs_write(junk, (const u8 *)"keep me", 7);
    CHECK(pkg_uninstall(junk) == PKG_E_NOTFOUND && fsn[junk].used);
    int d = fs_new(docs(), "outside.power", 0); fs_write(d, (const u8 *)"x", 1);
    CHECK(pkg_uninstall(d) == PKG_E_NOTFOUND && fsn[d].used);
    boot(); CHECK(reg_n == 0 && fs_lookup(apps(), "hello.text.power") < 0 && fsck());

    SECTION("uninstall when the disk write fails: reported honestly");
    CHECK(pkg_install(fs_lookup(docs(), "Hello.powor"), &pi) == PKG_OK);
    node = reg[0].node;
    emu_fail_writes = 1;
    int e = pkg_uninstall(node);
    emu_fail_writes = 0;
    CHECK(e == FS_ERR_IO && reg_n == 0);                                      /* gone from RAM, error surfaced */
    boot(); CHECK(reg_n == 1);                                                /* ...and the disk still has it: it reappears */
    CHECK(pkg_uninstall(reg[0].node) == PKG_OK); boot(); CHECK(reg_n == 0 && fsck());

    SECTION("system folders are protected in Files; deleting an installed app there means uninstall");
    pkg_install(fs_lookup(docs(), "Hello.powor"), &pi);
    open_app(APP_FILES); fm_cwd = 0; fm_refresh(); fm_sel = pkg_sys_dir(); fm_do_delete();
    CHECK(dlg.kind == DK_MSG && pkg_sys_dir() > 0); dlg_key('\n');
    fm_sel = pkg_sys_dir(); fm_do_rename(); CHECK(dlg.kind == DK_MSG); dlg_key('\n');
    fm_cwd = apps(); fm_refresh(); fm_sel = apps(); fm_do_delete(); CHECK(dlg.kind == DK_MSG && apps() > 0); dlg_key('\n');
    fm_cwd = apps(); fm_refresh(); fm_sel = reg[0].node; fm_do_delete();
    CHECK(dlg.kind == DK_UNINSTALL); dlg_key('\n'); CHECK(reg_n == 0);
}

static void t_failures(void) {
    SECTION("failed writes during install roll back completely");
    attach_emu(32768); boot();
    PkgInfo pi; int src = fs_lookup(docs(), "Hello.powor");
    u32 base = heap_base(), free0 = fat_free; int kids0 = nkids(apps());
    emu_fail_writes = 1; disk_err = 0;
    int e = pkg_install(src, &pi);
    emu_fail_writes = 0;
    CHECK(e == FS_ERR_IO && disk_err);
    CHECK(nkids(apps()) == kids0 && reg_n == 0 && fat_free == free0 && fsck() && heap_base() == base);
    boot(); CHECK(reg_n == 0 && nkids(apps()) == 0 && fsck());                 /* nothing half-written on disk either */
    CHECK(pkg_install(fs_lookup(docs(), "Hello.powor"), &pi) == PKG_OK);       /* and it works once the disk is healthy */
    pkg_uninstall(reg[0].node);

    SECTION("fs_write is all-or-nothing: a failed write (disk error or out of memory) leaves the old contents intact");
    {
        int f = fs_new(docs(), "atomic.txt", 0); CHECK(f > 0 && fs_write(f, (const u8 *)"old contents", 12) == 0);
        u32 fr = fat_free;
        emu_fail_writes = 1; int we = fs_write(f, (const u8 *)"brand new, much longer contents!", 32); emu_fail_writes = 0;
        CHECK(we == FS_ERR_IO && fsn[f].size == 12 && !memcmp(fs_data(f), "old contents", 12) && fat_free == fr);
        host_fail_alloc_after = 0; we = fs_write(f, (const u8 *)"another attempt", 15); host_fail_alloc_after = -1;
        CHECK(we == FS_ERR_NOMEM && fsn[f].size == 12 && !memcmp(fs_data(f), "old contents", 12));
        fs_evict(f); CHECK(fsn[f].size == 12 && !memcmp(fs_data(f), "old contents", 12));       /* and so does the disk copy */
        fs_delete(f);
    }

    SECTION("a disk that silently corrupts writes is caught by read-after-write verification");
    src = fs_lookup(docs(), "Hello.powor"); free0 = fat_free; base = heap_base();
    emu_corrupt_writes = 1;
    e = pkg_install(src, &pi);
    emu_corrupt_writes = 0;
    CHECK(e == PKG_E_VERIFY);
    CHECK(nkids(apps()) == 0 && reg_n == 0 && fat_free == free0 && fsck() && heap_base() == base);

    SECTION("disk full: install fails with FS_ERR_FULL, nothing left behind");
    {   boot();
        u32 n; u8 *txt = malloc(60000); memset(txt, 'q', 60000);
        u8 *p = mk_pkg("fits.app", "Fits", "1.0.0", "type=textview\n", txt, 60000, &n); free(txt);
        int sf = put_file("fits.powor", p, n); CHECK(sf > 0);
        int filler = fs_new(0, "filler", 1), c = 0;
        while (fat_free > 2) { char nm[32]; snprintf(nm, sizeof nm, "f%d", c++); int f = fs_new(filler, nm, 0); u32 want = (fat_free - 2 < 100 ? fat_free - 2 : 100) * CL_BYTES; u8 *b = calloc(1, want); int we = fs_write(f, b, want); free(b); if (we) { fs_delete(f); break; } }
        CHECK(fat_free <= 2 && fat_free < (n + CL_BYTES - 1) / CL_BYTES);
        u32 fr0 = fat_free; int kids0b = nkids(apps()); u32 base2 = heap_base();
        e = pkg_install(sf, &pi);
        CHECK(e == FS_ERR_FULL);
        CHECK(nkids(apps()) == kids0b && reg_n == 0 && fat_free == fr0 && fsck() && heap_base() == base2);
        free(p);
        fs_delete(filler); CHECK(pkg_install(sf, &pi) == PKG_OK && fsck());       /* works again once space is freed */
        pkg_uninstall(reg[0].node);
    }
    SECTION("allocation failure at every allocation point of install and launch");
    boot();
    src = fs_lookup(docs(), "Hello.powor"); int installed_at = -1, launched_at = -1;
    base = heap_base(); free0 = fat_free;
    for (int k = 0; k < 40; k++) {
        host_fail_alloc_after = k; e = pkg_install(src, &pi); host_fail_alloc_after = -1;
        if (e == PKG_OK) { installed_at = k; break; }
        if (!(nkids(apps()) == 0 && reg_n == 0 && fat_free == free0 && fsck())) { printf("  residue after install alloc-failure k=%d (err %d)\n", k, e); fails++; }
        CHECK(heap_base() == base);
    }
    CHECK(installed_at > 0);
    printf("    install succeeded once >= %d allocations were allowed; every earlier failure left no residue\n", installed_at);
    fs_reclaim(); base = heap_used_bytes;
    for (int k = 0; k < 40; k++) {
        host_fail_alloc_after = k; e = pkg_launch_node(reg[0].node); host_fail_alloc_after = -1;
        if (e == PKG_OK) { launched_at = k; break; }
        if (!(count_win(APP_TEXTVIEW) == 0 && tv_used() == 0)) { printf("  launch residue k=%d\n", k); fails++; }
        CHECK(heap_base() == base);
    }
    CHECK(launched_at > 0 && count_win(APP_TEXTVIEW) == 1);
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == APP_TEXTVIEW) destroy_win(i);
    pkg_uninstall(reg[0].node);

    SECTION("read failure at launch; node table full; registry full");
    CHECK(pkg_install(fs_lookup(docs(), "Hello.powor"), &pi) == PKG_OK);
    int node = reg[0].node; fs_evict(node);
    emu_fail_reads = 1; e = pkg_launch_node(node); emu_fail_reads = 0;
    CHECK(e == PKG_E_READ && count_win(APP_TEXTVIEW) == 0 && tv_used() == 0);
    CHECK(pkg_launch_node(node) == PKG_OK);
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == APP_TEXTVIEW) destroy_win(i);
    pkg_uninstall(node);
    {   /* node table full */
        int filler = fs_new(0, "nodes", 1), made = 0;
        for (;;) { char nm[32]; snprintf(nm, sizeof nm, "n%d", made); if (fs_new(filler, nm, 0) < 0) break; made++; }
        u32 fr0 = fat_free; base = heap_base();
        e = pkg_install(fs_lookup(docs(), "Hello.powor"), &pi);
        CHECK(e == FS_ERR_FULL && reg_n == 0 && fat_free == fr0 && fsck());
        CHECK(heap_base() == base);
        fs_delete(filler);
    }
    {   /* registry full: 32 apps, the 33rd is refused */
        int i;
        for (i = 0; i < REG_MAX; i++) {
            char id[32]; snprintf(id, sizeof id, "bulk.app%02d", i);
            u32 n; u8 *p = mk_pkg(id, id, "1.0.0", "type=textview\n", (const u8 *)"bulk", 4, &n);
            if (install_bytes("b.powor", p, n, &pi) != PKG_OK) { free(p); break; }
            free(p);
        }
        CHECK(i == REG_MAX && reg_n == REG_MAX);
        u32 n; u8 *p = mk_pkg("one.too.many", "Too Many", "1.0.0", "type=textview\n", (const u8 *)"x", 1, &n);
        CHECK(install_bytes("b.powor", p, n, &pi) == PKG_E_REGFULL && nkids(apps()) == REG_MAX && fsck());
        free(p);
        /* Start menu with a full registry: capped list + "More..." */
        SmItem it[SM_ITEMS_MAX]; int th, cnt = sm_build(it, &th), inst = 0, more = 0;
        for (int j = 0; j < cnt; j++) { inst += it[j].kind == SMK_INSTALLED; more += it[j].kind == SMK_MORE; }
        CHECK(inst == SM_MAX_VIS && more == 1 && MENUBAR_H + 2 + th <= H);
        menu_open = 1; snap("32_start_many"); menu_open = 0;
        boot(); CHECK(reg_n == REG_MAX);
    }
}

/* hover the dock item i until the magnified layout settles under the pointer, like a real user, then click it */
static void dock_click_item(int i) {
    DockGeo g; int xs[DOCK_MAXN], sz[DOCK_MAXN];
    for (int it = 0; it < 12; it++) {
        dock_geo(&g); dock_layout(&g, xs, sz);
        int tx = xs[i] + sz[i] / 2, ty = H - DK_BOT - sz[i] / 2;
        if (tx == mx && ty == my) break;
        mouse_update(tx - mx, -(ty - my), 0, 0);
    }
    host_ms += 500; mouse_update(0, 0, 1, 0); mouse_update(0, 0, 0, 0);
}
static int dock_covered(void) {           /* the saved dock strip must cover the dock for EVERY mouse position (else dock-only redraws leave stale pixels) */
    DockGeo g; int xs[DOCK_MAXN], sz[DOCK_MAXN], bad = 0, sx = mx, sy = my;
    dock_bg_ok = 0; dock_bg_save(1);
    int w = mini(DOCKBG_W, W - dock_bx);
    my = H - 30;
    for (mx = 0; mx < W; mx += 3) {
        dock_geo(&g); dock_layout(&g, xs, sz);
        int left = xs[0] - 14 - 1, right = xs[g.n - 1] + sz[g.n - 1] + 14 + 1;
        if (left < dock_bx || right > dock_bx + w) bad++;
        for (int i = 0; i < g.n; i++) if (xs[i] < 0 || xs[i] + sz[i] > W) bad++;
        for (int i = 1; i < g.n; i++) if (xs[i] < xs[i - 1] + sz[i - 1]) bad++;           /* no overlap */
    }
    mx = sx; my = sy;
    return bad == 0;
}
static void t_dock(void) {
    SECTION("dock: installed apps appear, launch, show running state, shrink to fit, and disappear when uninstalled");
    attach_emu(32768); boot();
    DockGeo g; dock_geo(&g);
    CHECK(g.n == DOCK_BUILTIN && g.sep == 0 && reg_n == 0);
    PkgInfo pi;
    CHECK(pkg_install(fs_lookup(docs(), "Hello.powor"), &pi) == PKG_OK);
    dock_geo(&g); CHECK(g.n == DOCK_BUILTIN + 1 && g.sep > 0 && !strcmp(dock_item_name(&g, 4), "Hello NovaOS"));
    CHECK(pkg_install(fs_lookup(docs(), "Photos Shortcut.appd"), &pi) == PKG_OK);
    dock_geo(&g); CHECK(g.n == 6 && !strcmp(dock_item_name(&g, 5), "Photo Album") && !strcmp(dock_item_name(&g, 0), "Files"));
    CHECK(!dock_item_running(&g, 4));
    /* click the installed icon in the dock with the real mouse path */
    CHECK(count_win(APP_TEXTVIEW) == 0);
    dock_click_item(4);
    CHECK(count_win(APP_TEXTVIEW) == 1 && dlg.kind == DK_NONE);
    dock_geo(&g); CHECK(dock_item_running(&g, 4));                                       /* running dot */
    snap("40_dock_installed");
    dock_click_item(4); CHECK(count_win(APP_TEXTVIEW) == 1);                                      /* second click focuses, no duplicate */
    /* the shortcut item opens the built-in app it points at */
    dock_click_item(5); CHECK(count_win(APP_IMAGES) == 1);
    /* hovering shows the tooltip: render must not crash and the dock-only fast path must equal a full render */
    mouse_update(0, 0, 0, 0); ui_frame(); snap("41_dock_tooltip");
    CHECK(dock_covered());
    /* uninstall removes it from the dock (and closes its window) */
    CHECK(pkg_uninstall(reg[reg_find_id("hello.text")].node) == PKG_OK);
    dock_geo(&g); CHECK(g.n == 5 && !strcmp(dock_item_name(&g, 4), "Photo Album") && count_win(APP_TEXTVIEW) == 0);
    /* survives a reboot (registry rebuilt from disk) */
    boot(); dock_geo(&g); CHECK(g.n == 5 && !strcmp(dock_item_name(&g, 4), "Photo Album"));

    SECTION("dock with many apps: capped at 8 installed, icons shrink to fit 800x600 / 1024x768 / 1920x1080");
    for (int i = 0; i < 10; i++) {
        char id[32], nm[32]; snprintf(id, sizeof id, "many.app%d", i); snprintf(nm, sizeof nm, "Many %d", i);
        u32 n; u8 *p = mk_pkg(id, nm, "1.0.0", "type=textview\naccent=E5484D\n", (const u8 *)"x", 1, &n);
        CHECK(install_bytes("m.powor", p, n, &pi) == PKG_OK); free(p);
    }
    CHECK(reg_n == 11);
    static const int res[3][2] = { {800, 600}, {1024, 768}, {1920, 1080} };
    for (int r = 0; r < 3; r++) {
        apply_mode(res[r][0], res[r][1], (u32)res[r][0] * 4);
        dock_geo(&g);
        CHECK(g.n == DOCK_MAXN && g.nb == DOCK_BUILTIN);                                  /* 4 built-in + 8 installed */
        CHECK(g.basetotal <= W - 80 && g.base >= 28 && g.base <= DK_BASE_MAX);
        if (r == 0) CHECK(g.base < DK_BASE_MAX);                                          /* 800 px wide: icons had to shrink */
        CHECK(dock_covered());
        char fn[32]; snprintf(fn, sizeof fn, "42_dock_many_%d", r); snap(fn);
    }
    apply_mode(1024, 768, 1024 * 4);
    /* every dock item is clickable and the last installed one is reachable */
    dock_geo(&g); dock_click_item(g.n - 1);
    CHECK(count_win(APP_TEXTVIEW) == 1 && dlg.kind == DK_NONE);
    /* an app that fails validation after install (tampered) shows an error from the dock, not a crash */
    int node = reg[0].node; u32 sz0 = fsn[node].size; u8 *t = malloc(sz0); memcpy(t, fs_data(node), sz0); t[sz0 - 1] ^= 1; fs_write(node, t, sz0); free(t);
    dock_geo(&g); CHECK(g.n == DOCK_MAXN - 0 || g.n == DOCK_MAXN);                        /* registry dropped it; another app fills the slot */
    CHECK(fsck());
}

/* Python <-> C disk interoperability: Python formats + fills build/pydisk.img; here the real kernel code mounts it, launches apps
 * from it, changes it, and dumps build/cdisk.img which tests/verify_cdisk.py then re-reads. */
static int load_image(const char *path) {
    u32 n; u8 *b = slurp(path, &n); if (!b || n < 1024 * 1024) { printf("  missing %s (run tests/run_tests.sh)\n", path); fails++; return 0; }
    free(emu_img); emu_img = b; emu_secs = n / 512; emu_present = 1; emu_fail_writes = emu_fail_reads = emu_corrupt_writes = 0;
    hk_in = emu_in; hk_out = emu_out; memset(&E, 0, sizeof E);
    return 1;
}
static void t_pydisk(void) {
    SECTION("disk written by tools/novadisk.py is mounted and used by the real kernel code; and vice versa");
    if (!load_image("build/pydisk.img")) return;
    boot();
    CHECK(fs_state == 1 && disk_on);                                           /* mounted as an existing NovaFS: not re-formatted */
    CHECK(reg_n == 3 && reg_find_id("hello.text") >= 0 && reg_find_id("novaos.manual") >= 0 && reg_find_id("photos.link") >= 0);
    CHECK(fsck());
    u32 n; u8 *big = slurp("build/pydisk_big.bin", &n);
    int deep = fs_lookup(fs_lookup(fs_lookup(docs(), "Projects"), "deep"), "big.bin");
    CHECK(deep > 0 && fsn[deep].size == 100000 && n == 100000 && !memcmp(fs_data(deep), big, n)); free(big);
    int notes = fs_lookup(docs(), "notes.txt"); CHECK(notes > 0 && fsn[notes].size == 18 && !memcmp(fs_data(notes), "hello from python\n", 18));
    int empty = fs_lookup(docs(), "empty.txt"); CHECK(empty > 0 && fsn[empty].size == 0);
    CHECK(pkg_launch_reg(reg_find_id("hello.text")) == PKG_OK && count_win(APP_TEXTVIEW) == 1 && strstr(tv[0].text, "Hello from a NovaOS package!"));
    DockGeo g; dock_geo(&g); CHECK(g.n == DOCK_BUILTIN + 3);                   /* the dock shows the Python-installed apps */
    /* now the kernel changes the image */
    PkgInfo pi; u8 *pk = corpus("ok_full", &n); CHECK(install_bytes("full.powor", pk, n, &pi) == PKG_OK); free(pk);
    CHECK(pkg_uninstall(reg[reg_find_id("photos.link")].node) == PKG_OK);
    int f1 = fs_new(docs(), "from_c.txt", 0); CHECK(f1 > 0 && fs_write(f1, (const u8 *)"written by the C kernel code\n", sizeof "written by the C kernel code\n" - 1) == 0);
    u8 *pat = malloc(300000); for (int i = 0; i < 300000; i++) pat[i] = (u8)(i * 7 + 3);
    int f2 = fs_new(docs(), "pattern.bin", 0); CHECK(f2 > 0 && fs_write(f2, pat, 300000) == 0); free(pat);
    int old = fs_lookup(docs(), "full.powor"); fs_delete(old);                 /* the source file is not needed any more */
    boot(); CHECK(fs_state == 1 && reg_n == 3 && fsck());
    FILE *f = fopen("build/cdisk.img", "wb"); if (f) { fwrite(emu_img, 1, (size_t)emu_secs * 512, f); fclose(f); }
    emu_present = 1;
}

/* The developer kit end to end: tests/test_sdk.py followed the Quick Start and produced build/sdkdisk.img (7 example apps).
 * The real kernel code mounts it, shows the apps in Start menu + dock, and launches each; displayed text must equal the sources. */
static void t_sdk(void) {
    SECTION("SDK Quick Start disk: kernel mounts it, lists, docks and launches all 7 example apps with the right content");
    if (!load_image("build/sdkdisk.img")) return;
    boot();
    CHECK(fs_state == 1 && reg_n == 7 && fsck());
    static const struct { const char *id, *dir, *file; int wrap; u32 accent; } ex[5] = {
        { "hello.world",     "hello-world",        "content.txt", 1, 0x2F6FED },
        { "daily.tips",      "daily-tips",         "tips.txt",    1, 0x28A745 },
        { "keys.reference",  "keyboard-shortcuts", "keys.txt",    0, 0xF59E0B },
        { "ascii.gallery",   "ascii-gallery",      "art.txt",     0, 0x8B5CF6 },
        { "pancake.recipe",  "pancakes",           "recipe.txt",  1, 0xE5484D } };
    for (int i = 0; i < 5; i++) {
        int r = reg_find_id(ex[i].id); CHECK(r >= 0);
        if (r < 0) continue;
        CHECK(pkg_launch_reg(r) == PKG_OK && count_win(APP_TEXTVIEW) == 1);
        char path[200]; snprintf(path, sizeof path, "build/sdk_stage/examples/%s/%s", ex[i].dir, ex[i].file);
        u32 n; u8 *src = slurp(path, &n);
        int slot = -1; for (int k2 = 0; k2 < TV_MAX; k2++) if (tv[k2].used && !strcmp(tv[k2].id, ex[i].id)) slot = k2;
        CHECK(slot >= 0 && src && (u32)tv[slot].len == n && !memcmp(tv[slot].text, src, n));
        CHECK(slot >= 0 && tv[slot].wrap == ex[i].wrap && tv[slot].accent == ex[i].accent);
        free(src);
        char fn[40]; snprintf(fn, sizeof fn, "50_sdk_%s", ex[i].dir); snap(fn);
        for (int w2 = 0; w2 < MAXWIN; w2++) if (wins[w2].used && wins[w2].type == APP_TEXTVIEW) destroy_win(w2);
    }
    CHECK(pkg_launch_reg(reg_find_id("quick.notes")) == PKG_OK && count_win(APP_NOTEPAD) == 1);
    CHECK(pkg_launch_reg(reg_find_id("my.files")) == PKG_OK && count_win(APP_FILES) == 1);
    for (int w2 = 0; w2 < MAXWIN; w2++) if (wins[w2].used) destroy_win(w2);
    DockGeo g; dock_geo(&g); CHECK(g.n == DOCK_BUILTIN + 7 && g.sep > 0);
    CHECK(dock_covered());
    SmItem it[SM_ITEMS_MAX]; int th, cnt = sm_build(it, &th), inst = 0; for (int j = 0; j < cnt; j++) inst += it[j].kind == SMK_INSTALLED;
    CHECK(inst == 7);
    menu_open = 1; snap("51_sdk_start_menu"); menu_open = 0;
    /* a user-style install from inside NovaOS of a package that novadisk put in /Documents (docs/02 "prefer installing from inside NovaOS") */
    PkgInfo pi; u32 n; u8 *tiny = slurp("build/sdk_stage/tiny.powor", &n);
    CHECK(tiny && install_bytes("tiny.powor", tiny, n, &pi) == PKG_OK && !strcmp(pi.id, "tiny.app")); free(tiny);
    dock_geo(&g); CHECK(g.n == DOCK_BUILTIN + 8);
    snap("52_sdk_dock_8");
}

static void t_ui_flows(void) {
    SECTION("UI flows: Files -> install dialog -> Start menu -> launch -> x -> uninstall");
    attach_emu(32768); boot();
    open_app(APP_FILES); fm_go(docs());
    int hello = fs_lookup(docs(), "Hello.powor");
    fm_open_node(hello);                                                         /* double-click on the package */
    CHECK(dlg.kind == DK_INSTALL && strstr(dlg.msg, "Hello NovaOS") && strstr(dlg.msg, "1.0.0") && strstr(dlg.msg, "textview"));
    snap("33_install_dialog");
    dlg_key(27); CHECK(dlg.kind == DK_NONE && reg_n == 0);                       /* Cancel installs nothing */
    fm_open_node(hello); dlg_key('\n');                                          /* Install */
    CHECK(dlg.kind == DK_MSG && strstr(dlg.msg, "Installed") && reg_n == 1);
    dlg_key('\n'); CHECK(dlg.kind == DK_NONE);
    fm_open_node(hello);                                                         /* duplicate: error, no confirm */
    CHECK(dlg.kind == DK_MSG && strstr(dlg.msg, "already installed")); dlg_key('\n');
    u32 n; u8 *g = corpus("bad_crc_data_flip", &n); int bad = put_file("evil.powor", g, n); free(g);
    fm_open_node(bad); CHECK(dlg.kind == DK_MSG && strstr(dlg.msg, "corrupted") && reg_n == 1); snap("34_error_dialog"); dlg_key('\n');
    g = corpus("bad_type_unknown_elf", &n); bad = put_file("prog.appd", g, n); free(g);
    fm_open_node(bad); CHECK(dlg.kind == DK_MSG && strstr(dlg.msg, "not supported") && reg_n == 1); dlg_key('\n');
    /* open the installed .power in Files: launches it */
    fm_go(apps()); fm_open_node(reg[0].node);
    CHECK(count_win(APP_TEXTVIEW) == 1 && dlg.kind == DK_NONE);
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == APP_TEXTVIEW) destroy_win(i);
    /* a second app so the menu has two rows */
    PkgInfo pi; CHECK(pkg_install(fs_lookup(docs(), "Photos Shortcut.appd"), &pi) == PKG_OK);
    /* Start menu: click Start, click the installed row */
    click_at(20, 13); CHECK(menu_open);
    SmItem it[SM_ITEMS_MAX]; int th, cnt = sm_build(it, &th), row = -1, hdr = 0;
    for (int j = 0; j < cnt; j++) { if (it[j].kind == SMK_INSTALLED && !strcmp(reg[it[j].arg].info.id, "hello.text")) row = j; hdr += it[j].kind == SMK_HEADER; }
    CHECK(row >= 0 && hdr == 2);
    snap("31_start_menu");
    click_at(100, it[row].y + 18);
    CHECK(!menu_open && count_win(APP_TEXTVIEW) == 1);
    snap("35_app_running");
    /* the remove (x) button */
    click_at(20, 13); cnt = sm_build(it, &th);
    for (int j = 0; j < cnt; j++) if (it[j].kind == SMK_INSTALLED && !strcmp(reg[it[j].arg].info.id, "hello.text")) row = j;
    mouse_update(0, 0, 0, 0); mouse_update(6 + SM_W - 40 + 12 - mx, -(it[row].y + 18 - my), 0, 0);
    snap("36_start_hover_x");
    click_at(6 + SM_W - 40 + 12, it[row].y + 18);
    CHECK(dlg.kind == DK_UNINSTALL && !menu_open && reg_n == 2);
    snap("37_uninstall_dialog");
    dlg_key(27); CHECK(reg_n == 2);                                              /* cancel */
    click_at(6 + SM_W - 40 + 12, it[row].y + 18);                                /* menu is closed now: this click hits the desktop */
    click_at(20, 13); cnt = sm_build(it, &th);
    for (int j = 0; j < cnt; j++) if (it[j].kind == SMK_INSTALLED && !strcmp(reg[it[j].arg].info.id, "hello.text")) row = j;
    click_at(6 + SM_W - 40 + 12, it[row].y + 18); CHECK(dlg.kind == DK_UNINSTALL);
    dlg_key('\n');
    CHECK(dlg.kind == DK_NONE && reg_n == 1 && reg_find_id("hello.text") < 0 && count_win(APP_TEXTVIEW) == 0);
    click_at(20, 13); cnt = sm_build(it, &th); int inst = 0; for (int j = 0; j < cnt; j++) inst += it[j].kind == SMK_INSTALLED; CHECK(inst == 1);
    snap("38_start_after_uninstall");
    key_event(27); menu_open = 0;
    /* builtin shortcut launches Photos from the menu */
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == APP_IMAGES) destroy_win(i);
    click_at(20, 13); cnt = sm_build(it, &th);
    for (int j = 0; j < cnt; j++) if (it[j].kind == SMK_INSTALLED) row = j;
    click_at(100, it[row].y + 18); CHECK(count_win(APP_IMAGES) == 1);
    /* empty registry shows the hint */
    pkg_uninstall(reg[0].node); click_at(20, 13); cnt = sm_build(it, &th);
    int hint = 0; for (int j = 0; j < cnt; j++) hint += it[j].kind == SMK_HEADER && it[j].arg == 2; CHECK(hint == 1);
    snap("39_start_empty");
    key_event(27);
    CHECK(fsck());
    /* the cross-language check: write a C-built package for tests/run_tests.sh to validate with the Python tool */
    u8 *p = mk_pkg("cbuilt.app", "C Built", "3.4.5", "type=textview\ntitle=From C\n", (const u8 *)"made by the C test", 18, &n);
    FILE *f = fopen("build/cbuilt.powor", "wb"); if (f) { fwrite(p, 1, n, f); fclose(f); } free(p);
}

int main(void) {
    t_constants();
    t_corpus();
    t_bitflips();
    t_fuzz();
    t_install_valid();
    t_install_corrupt();
    t_duplicates();
    t_persistence();
    t_launch();
    t_uninstall();
    t_failures();
    t_dock();
    t_pydisk();
    t_sdk();
    t_ui_flows();
    printf("\n%d checks, %d failures: %s\n", checks, fails, fails ? "SOME PACKAGE TESTS FAILED" : "ALL PACKAGE TESTS PASSED");
    return fails != 0;
}

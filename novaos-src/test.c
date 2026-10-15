/* Host-side test harness: runs the real kernel UI code on a fake framebuffer. */
#include "kernel.c"

static u32 hostfb[MAXW * MAXH];
static void snap(const char *name) {
    render();
    char fn[128]; snprintf(fn, sizeof fn, "shots/%s.ppm", name);
    FILE *f = fopen(fn, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        u32 p = hostfb[y * (fbpitch / 4) + x];
        fputc(p >> 16, f); fputc(p >> 8, f); fputc(p, f);
    }
    fclose(f);
}
static void move_to(int x, int y) { mouse_update(x - mx, -(y - my), mbtn, 0); }
static void click(int x, int y) { host_ms += 1000; move_to(x, y); mouse_update(0, 0, 1, 0); mouse_update(0, 0, 0, 0); }
static __attribute__((unused)) void dclick(int x, int y) { host_ms += 1000; move_to(x, y); mouse_update(0,0,1,0); mouse_update(0,0,0,0); host_ms += 100; mouse_update(0,0,1,0); mouse_update(0,0,0,0); }
static void type(const char *s) { for (; *s; s++) key_event(*s); }
static void boot(void) {
    memset(wins, 0, sizeof wins); nz = 0;
    fbp = hostfb; W = 1024; H = 768; fbpitch = W * 4;
    fs_boot(); wp_apply(); present_reset(); clock_refresh(1);
    fm_cwd = fs_lookup(0, "Documents"); fm_refresh();
    open_app(APP_FILES);
    open_notepad_file(fs_lookup(fm_cwd, "Welcome.txt"));
    mx = W / 2; my = H / 2;
}
static int decode_file(const char *path, const char *out) {
    FILE *f = fopen(path, "rb"); if (!f) return 2;
    static u8 buf[1 << 24]; u32 n = fread(buf, 1, sizeof buf, f); fclose(f);
    Img im; const char *err = "";
    if (!image_decode(buf, n, &im, &err)) { printf("FAIL %s: %s\n", path, err); return 1; }
    FILE *o = fopen(out, "wb"); fprintf(o, "P6\n%d %d\n255\n", im.w, im.h);
    for (int i = 0; i < im.w * im.h; i++) { fputc(im.px[i] >> 16, o); fputc(im.px[i] >> 8, o); fputc(im.px[i], o); }
    fclose(o); img_free(&im);
    return 0;
}
#define CHECK(c) do { if (!(c)) { printf("CHECK FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(int argc, char **argv) {
    if (argc >= 4 && !strcmp(argv[1], "decode")) return decode_file(argv[2], argv[3]);
    int fails = 0;
    boot();
    snap("01_boot");

    /* ---- file system unit tests ---- */
    int a = fs_new(0, "t", 1);
    CHECK(a > 0);
    CHECK(fs_new(0, "T", 1) == FS_ERR_EXISTS);
    CHECK(fs_new(0, "bad/name", 0) == FS_ERR_NAME);
    CHECK(fs_new(0, "", 0) == FS_ERR_NAME);
    int b = fs_new(a, "x.txt", 0); CHECK(b > 0);
    CHECK(fs_write(b, (const u8 *)"hello", 5) == 0 && fsn[b].size == 5 && !memcmp(fsn[b].data, "hello", 5));
    CHECK(fs_write(b, (const u8 *)"hi", 2) == 0 && fsn[b].size == 2);
    CHECK(fs_rename(b, "y.txt") == 0 && fs_lookup(a, "y.txt") == b);
    int c2 = fs_new(a, "z.txt", 0); CHECK(fs_rename(c2, "Y.TXT") == FS_ERR_EXISTS);
    u32 before = heap_used_bytes;
    fs_delete(a);
    CHECK(!fsn[a].used && !fsn[b].used && !fsn[c2].used && heap_used_bytes < before);
    char nm[40]; fs_unique(0, "Documents", "", nm); CHECK(!strcmp(nm, "Documents 2"));

    /* ---- dock hover ---- */
    move_to(W / 2 - 30, H - 50);
    snap("02_dock_hover");

    /* ---- new folder via toolbar ---- */
    open_app(APP_FILES);
    Win *w = &wins[find_win(APP_FILES)];
    FmL L; fm_layout(w, &L);
    click(L.bx[2] + 10, L.by + 8);                   /* New Folder */
    CHECK(dlg.kind == DK_NEWFILE + 1);
    snap("03_dialog");
    type("Projects"); key_event('\n');
    CHECK(fs_lookup(fm_cwd, "Projects") > 0);
    CHECK(fsn[fm_sel].dir);
    /* duplicate name -> message */
    open_app(APP_FILES); click(L.bx[2] + 10, L.by + 8); type("projects"); key_event('\n');
    CHECK(dlg.kind == DK_MSG); snap("04_dup_error"); key_event('\n');
    /* new file + notepad */
    open_app(APP_FILES); click(L.bx[1] + 10, L.by + 8); type("notes.txt"); key_event('\n');
    int nf = fs_lookup(fm_cwd, "notes.txt"); CHECK(nf > 0);
    fm_open_node(nf);
    CHECK(np_file == nf);
    type("Hello from NovaOS!"); key_event('\n'); type("second line");
    CHECK(np_mod);
    ctrl_down = 1; key_event('s'); ctrl_down = 0;
    CHECK(!np_mod && fsn[nf].size == 30);
    CHECK(!memcmp(fsn[nf].data, "Hello from NovaOS!\nsecond line", 30));
    snap("05_notepad_saved");
    /* rename via F2 */
    open_app(APP_FILES); fm_sel = nf; key_event(K_F2);
    CHECK(dlg.kind == DK_RENAME); type("renamed.txt"); key_event('\n');
    CHECK(!strcmp(fsn[nf].name, "renamed.txt"));
    /* delete with confirm; cancel first */
    key_event(K_DEL); CHECK(dlg.kind == DK_DELETE); key_event(27); CHECK(fsn[nf].used);
    key_event(K_DEL); key_event('\n'); CHECK(!fsn[nf].used);
    CHECK(np_file == -1 && np_mod);          /* open doc survives as untitled */
    /* save as */
    open_app(APP_NOTEPAD); ctrl_down = 1; shift_down = 1; key_event('S'); ctrl_down = shift_down = 0;
    CHECK(dlg.kind == DK_SAVEAS); key_event('\b'); type("kept.txt"); key_event('\n');
    CHECK(fs_lookup(fm_cwd, "kept.txt") > 0 && !np_mod);
    snap("06_after_ops");

    /* ---- images ---- */
    fm_go(fs_lookup(0, "Pictures"));
    for (int i = 0; i < fm_n; i++) {
        open_image_file(fm_list[i]);
        printf("image %-14s %s %dx%d err='%s'\n", fsn[fm_list[i]].name, iv_img.px ? "ok" : "FAIL", iv_img.w, iv_img.h, iv_err);
        CHECK(iv_img.px);
        char fn[64]; snprintf(fn, sizeof fn, "07_img_%d", i); snap(fn);
    }
    iv_step(1); iv_step(-1);
    Win *iw = &wins[find_win(APP_IMAGES)];
    int cx, cy, cw, ch; content_rect(iw, &cx, &cy, &cw, &ch);
    click(cx + cw - 70, cy + 18);                    /* Set wallpaper */
    CHECK(wp_src.px);
    snap("08_wallpaper");
    /* corrupt / non-image data must not crash */
    int bad = fs_new(fm_cwd, "broken.png", 0); fs_write(bad, (const u8 *)"\x89PNG\r\n\x1a\nxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 44);
    open_image_file(bad); CHECK(!iv_img.px && *iv_err); snap("09_bad_image");

    /* ---- resolution changes ---- */
    apply_mode(1280, 720, 1280 * 4); snap("10_1280x720");
    apply_mode(1920, 1080, 1920 * 4); toggle_max(find_win(APP_FILES)); snap("11_1920x1080_max");
    apply_mode(800, 600, 800 * 4); snap("12_800x600");
    CHECK(wins[find_win(APP_FILES)].x + wins[find_win(APP_FILES)].w <= W);
    apply_mode(1024, 768, 1024 * 4);
    open_app(APP_DISPLAY); open_app(APP_ABOUT); snap("13_display_about");
    bga_ver = 0xB0C5; dp_sel = 3; open_app(APP_DISPLAY); wins[find_win(APP_DISPLAY)].x = 300; wins[find_win(APP_DISPLAY)].y = 120; snap("14_display_live");
    printf("heap used: %u KB, fails: %d\n", heap_used_bytes / 1024, fails);
    return fails != 0;
}

/* Fuzz test: incremental redraws (cursor-only / dock-only / diff present) must always equal a full redraw. */
#include "kernel.c"
static u32 hostfb[MAXW * MAXH];
static u32 rng_s = 12345;
static u32 rnd(void) { rng_s = rng_s * 1664525u + 1013904223u; return rng_s >> 8; }
static int paths[3];
static void boot(int w, int h) {
    memset(wins, 0, sizeof wins); nz = 0; memset(&dlg, 0, sizeof dlg); menu_open = cal_open = 0;
    fbp = hostfb; W = w; H = h; fbpitch = (u32)W * 4;
    fs_boot(); pkg_ui_init(); pkg_system_init(); wp_apply(); present_reset(); clock_refresh(1);
    fm_cwd = fs_lookup(0, "Documents"); fm_refresh();
    { PkgInfo pi; static const char *s[3] = { "Hello.powor", "Manual.appd", "Photos Shortcut.appd" };       /* installed apps appear in the dock */
      for (int i = 0; i < 3; i++) pkg_install(fs_lookup(fm_cwd, s[i]), &pi); }
    open_app(APP_FILES); open_notepad_file(fs_lookup(fm_cwd, "Welcome.txt"));
    mx = W / 2; my = H / 2; dock_bg_ok = 0; cur_x = -100;
    dirty = 1; ui_frame();
}
static u32 refbuf[MAXW * MAXH];
static int check(const char *what, int step) {
    for (int y = 0; y < H; y++) memcpy(refbuf + y * W, (u8 *)hostfb + (u32)y * fbpitch, (u32)W * 4);   /* what the user sees now */
    int ox = cur_x, oy = cur_y;
    present_reset(); dock_bg_ok = 0; render_full();                                                     /* from-scratch reference */
    int bad = 0, fx = -1, fy = -1;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        if (refbuf[y * W + x] != hostfb[y * (fbpitch / 4) + x]) { if (!bad) { fx = x; fy = y; } bad++; }
    if (bad) { printf("MISMATCH step %d after %s: %d px differ, first at (%d,%d); mouse (%d,%d) prev cursor (%d,%d)\n", step, what, bad, fx, fy, mx, my, ox, oy); return 1; }
    return 0;
}
int main(int argc, char **argv) {
    int steps = argc > 1 ? atoi(argv[1]) : 800, fails = 0; if (argc > 2) rng_s = (u32)atoi(argv[2]);
    static const int sizes[][2] = { {1024, 768}, {800, 600}, {1280, 720} };
    for (int cfg = 0; cfg < 3 && fails < 3; cfg++) {
        boot(sizes[cfg][0], sizes[cfg][1]);
        printf("== %dx%d, %d random steps\n", W, H, steps);
        for (int st = 0; st < steps && fails < 3; st++) {
            const char *what = "move";
            int r = rnd() % 100, batch = 1 + rnd() % 3;
            host_ms += 37;
            if (r < 70) {                                   /* pointer motion, often in small steps, sometimes jumps */
                for (int b = 0; b < batch; b++) {
                    int big = rnd() % 6 == 0;
                    int dx = big ? (int)(rnd() % 600) - 300 : (int)(rnd() % 21) - 10;
                    int dy = big ? (int)(rnd() % 400) - 200 : (int)(rnd() % 21) - 10;
                    mouse_update(dx, dy, mbtn, 0);
                }
            } else if (r < 78) {                            /* drift into the dock / menu bar on purpose */
                int tx = (rnd() % 2) ? W / 2 + (int)(rnd() % 700) - 350 : (int)(rnd() % W), ty = (rnd() % 2) ? H - 1 - (int)(rnd() % 120) : (int)(rnd() % 30);
                mouse_update(tx - mx, -(ty - my), mbtn, 0); what = "warp";
            } else if (r < 90) {                            /* click */
                host_ms += 300;
                mouse_update(0, 0, 1, 0); mouse_update(0, 0, 0, 0); what = "click";
            } else if (r < 94) { mouse_update(0, 0, mbtn, (int)(rnd() % 5) - 2); what = "wheel"; }
            else if (r < 97) {                              /* drag: press, move, release */
                mouse_update(0, 0, 1, 0);
                for (int b = 0; b < 3; b++) mouse_update((int)(rnd() % 40) - 20, (int)(rnd() % 40) - 20, 1, 0);
                mouse_update(0, 0, 0, 0); what = "drag";
            } else {                                        /* keys */
                static const int ks[] = { 'a', 'b', ' ', '\n', '\b', K_DOWN, K_UP, K_LEFT, K_F2, 27, 'x' };
                key_event(ks[rnd() % 11]); dirty = 1; what = "key";
            }
            if (rnd() % 25 == 0) { dt_wd = (dt_wd + 1) % 7; t_m = (t_m + 1) % 60; dirty = 1; }
            if (rnd() % 30 == 0) {                          /* the dock grows and shrinks as apps are installed / removed */
                static int serial; PkgInfo pi;
                if (reg_n < 11 && rnd() % 3) {
                    char id[32], nm[32]; snprintf(id, sizeof id, "fz.app%d", serial); snprintf(nm, sizeof nm, "Fuzz %d", serial++);
                    u8 txt[] = "fuzz"; u32 n = 48 + 100; u8 *p = calloc(1, n + 64); char meta[128]; snprintf(meta, sizeof meta, "id=%s\nname=%s\nversion=1.0.0\n", id, nm);
                    u32 ml = (u32)strlen(meta), tl = 14; memcpy(p, "NPKG", 4); p[4] = 1; p[6] = 48; n = 48 + ml + tl + 4; st32(p + 8, n); st32(p + 16, 48); st32(p + 20, ml);
                    st32(p + 24, 48 + ml); st32(p + 28, tl); st32(p + 32, 48 + ml + tl); st32(p + 36, 4); memcpy(p + 48, meta, ml); memcpy(p + 48 + ml, "type=textview\n", tl); memcpy(p + 48 + ml + tl, txt, 4);
                    st32(p + 12, pkg_crc(p, n));
                    int sf = fs_new(fs_lookup(0, "Documents"), id, 0); if (sf > 0) { fs_write(sf, p, n); pkg_install(sf, &pi); fs_delete(sf); }
                    free(p);
                } else if (reg_n > 0) pkg_uninstall(reg[rnd() % reg_n].node);
                dirty = 1; what = "install/uninstall";
            }
            int path = dirty ? 0 : dirty_dock ? 1 : dirty_cursor ? 2 : -1;
            if (path >= 0) paths[path]++;
            ui_frame();
            fails += check(what, st);
        }
    }
    printf("redraw paths used: full=%d dock-only=%d cursor-only=%d\n", paths[0], paths[1], paths[2]);
    printf(fails ? "FAILED\n" : "ALL FAST-PATH CHECKS PASSED\n");
    return fails != 0;
}

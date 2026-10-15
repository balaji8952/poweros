/* ui_base.h - theme, window table, wallpaper, modal dialogs, resolution change */
#ifndef NOVA_UIBASE_H
#define NOVA_UIBASE_H
#include "core.h"
#include "hw.h"
#include "gfx.h"
#include "image.h"
#include "fs.h"
#include "pkg.h"

/* ---------------------------------------------------------------- theme */
#define C_WIN   0xf4f4f7
#define C_BAR   0xe6e7ec
#define C_LINE  0xc7cad3
#define C_TXT   0x1c1f27
#define C_DIM   0x6b7080
#define C_ACC   0x2f6fed
#define C_FIELD 0xffffff

#define MENUBAR_H    26
#define TITLE_H      30
#define DOCK_RESERVE 92
#define MAXWIN       8

/* APP_FILES..APP_ABOUT must stay in this order: pkg_targets[] (pkg.h) indexes them.
 * APP_TEXTVIEW is the window type of installed "textview" packages (several may be open: Win.inst). */
enum { APP_FILES, APP_NOTEPAD, APP_IMAGES, APP_DISPLAY, APP_ABOUT, APP_TEXTVIEW, NAPPS };
static const char *app_names[NAPPS] = { "Files", "Notepad", "Photos", "Display", "About", "App" };
static const int app_dw[NAPPS]  = { 640, 600, 640, 470, 380, 560 };
static const int app_dh[NAPPS]  = { 420, 440, 480, 370, 270, 420 };
static const int app_minw[NAPPS] = { 460, 380, 340, 470, 380, 320 };
static const int app_minh[NAPPS] = { 300, 240, 260, 370, 270, 200 };
static const int app_resizable[NAPPS] = { 1, 1, 1, 0, 0, 1 };

typedef struct { int used, x, y, w, h, type, min, max, rx, ry, rw, rh, inst; } Win;
static Win wins[MAXWIN];
static int zo[MAXWIN], nz;                       /* z-order, bottom -> top */
static int mx = 512, my = 384, mbtn, mprev;
static int drag = -1, drag_ox, drag_oy;
static int rsz = -1, rsz_ox, rsz_oy;
static int menu_open, cal_open;                  /* Start menu / calendar popup */
static int ctrl_down, shift_down, caps_on;
static int dirty = 1;                            /* needs a full re-render */
static int dirty_dock, dirty_cursor;             /* cheaper partial updates */
static int cur_x = -100, cur_y = -100;           /* where the cursor is on screen now */
static int dock_bg_ok;                           /* dock strip background saved? */

/* Hover is only honoured for the thing that is really under the mouse:
 * hover_win = topmost window under the pointer, -2 dialog, -3 menus/dock. */
static int hover_win = -1, cur_draw_win = -1;
static int hov_in(int x, int y, int w, int h) { return hover_win == cur_draw_win && inrect(mx, my, x, y, w, h); }
static int button_hot(int x, int y, int w, int h) { return hov_in(x, y, w, h); }

/* ---- cached clock / date (the RTC is slow to read, so never per frame) ---- */
static int t_h, t_m, dt_y = 2026, dt_m = 1, dt_d = 1, dt_wd, t_valid;
static u32 t_last;
static int day_of_week(int y, int m, int d) {                    /* 0 = Sunday */
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
static int days_in_month(int y, int m) {
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ? 29 : dm[m - 1];
}
static void clock_refresh(int force) {
    u32 n = now_ms();
    if (!force && t_valid && n - t_last < 1000) return;
    t_last = n;
    int h, m, y, mo, d;
    get_time(&h, &m); get_date(&y, &mo, &d);
    if (!t_valid || h != t_h || m != t_m || d != dt_d) dirty = 1;
    t_h = h; t_m = m; dt_y = y; dt_m = mo; dt_d = d; dt_wd = day_of_week(y, mo, d); t_valid = 1;
}
static int cal_vy, cal_vm;                       /* month shown in the calendar */
static void btn(int x, int y, int w, int h, const char *s, int en) {
    int hov = en && button_hot(x, y, w, h);
    fill_rect(x, y + 1, w, h, 0xd9dbe1);                       /* soft drop */
    fill_rect(x, y, w, h, 0xa9adb8);
    fill_vgrad(x + 1, y + 1, w - 2, h - 2, hov ? 0xeef4ff : 0xffffff, hov ? 0xd3e2ff : 0xececf1);
    draw_text(x + (w - slen(s) * 8) / 2, y + (h - 16) / 2, s, en ? C_TXT : 0xa3a7b3);
}

/* ------------------------------------------------------------ wallpaper */
static Img wp_src;                    /* user picture, or px==0 for the default */

static void make_default_wallpaper(void) {
    int gx = W * 7 / 10, gy = H * 35 / 100;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int r = 14 + y * 50 / H, g = 18 + y * 14 / H, b = 48 + y * 66 / H;
            int dx = x - gx, dy = y - gy;
            int glow = 110 - (dx * dx + dy * dy) / 2400;
            if (glow < 0) glow = 0;
            r += glow * 6 / 10; g += glow * 3 / 10; b += glow;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            wall[y * W + x] = (r << 16) | (g << 8) | b;
        }
    }
}
static void wp_apply(void) {
    if (!wp_src.px) { make_default_wallpaper(); return; }
    int sx0 = 0, sy0 = 0, sw = wp_src.w, sh = wp_src.h;      /* "cover": fill the screen, crop the rest */
    if (wp_src.w * H > wp_src.h * W) { sw = wp_src.h * W / H; sx0 = (wp_src.w - sw) / 2; }
    else                             { sh = wp_src.w * H / W; sy0 = (wp_src.h - sh) / 2; }
    scale_box(&wp_src, sx0, sy0, sw, sh, wall, W, H, W);
}
static int wp_set(const Img *im) {
    u32 *copy = kmalloc((u32)im->w * im->h * 4);
    if (!copy) return 0;
    memcpy(copy, im->px, (u32)im->w * im->h * 4);
    img_free(&wp_src);
    wp_src.px = copy; wp_src.w = im->w; wp_src.h = im->h;
    wp_apply();
    return 1;
}

/* ---------------------------------------------------------- window table */
static int find_win(int type) {
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == type) return i;
    return -1;
}
static int top_visible(void) {
    for (int k = nz - 1; k >= 0; k--) if (!wins[zo[k]].min) return zo[k];
    return -1;
}
static void focus_win(int i) {
    int k, n = 0;
    for (k = 0; k < nz; k++) if (zo[k] != i) zo[n++] = zo[k];
    zo[n++] = i; nz = n;
}
static void win_fit_max(Win *w) { w->x = 0; w->y = MENUBAR_H; w->w = W; w->h = H - MENUBAR_H - DOCK_RESERVE; }
static void win_clamp(Win *w) {
    if (w->max) { win_fit_max(w); return; }
    int maxw = W - 8, maxh = H - MENUBAR_H - DOCK_RESERVE;
    if (w->w > maxw) w->w = maxw;
    if (w->h > maxh) w->h = maxh;
    if (w->w < 200) w->w = 200;
    if (w->h < TITLE_H + 60) w->h = TITLE_H + 60;
    w->x = clampi(w->x, 0, maxi(0, W - w->w));
    w->y = clampi(w->y, MENUBAR_H, maxi(MENUBAR_H, H - DOCK_RESERVE - w->h));
}
static void toggle_max(int i);
static void iv_release(void);          /* apps.h: free the viewer's image */
static void tv_release(int inst);      /* apps.h: free a text-viewer instance */

static void destroy_win(int i) {
    int n = 0;
    for (int k = 0; k < nz; k++) if (zo[k] != i) zo[n++] = zo[k];
    nz = n;
    if (wins[i].type == APP_IMAGES) iv_release();
    if (wins[i].type == APP_TEXTVIEW) tv_release(wins[i].inst);
    wins[i].used = 0;
}
/* create a window of `type`; `inst` selects per-window state for multi-instance types */
static int open_win_inst(int type, int inst) {
    static int casc;
    for (int i = 0; i < MAXWIN; i++) if (!wins[i].used) {
        Win *w = &wins[i];
        memset(w, 0, sizeof *w);
        w->used = 1; w->type = type; w->inst = inst;
        w->w = app_dw[type]; w->h = app_dh[type];
        w->x = (W - w->w) / 2 + (casc % 5) * 28 - 56;
        w->y = MENUBAR_H + 30 + (casc % 5) * 24;
        casc++;
        win_clamp(w);
        zo[nz++] = i; focus_win(i);
        return i;
    }
    return -1;
}
static void open_app(int type) {
    int ex = find_win(type);
    if (ex >= 0) { wins[ex].min = 0; focus_win(ex); return; }
    open_win_inst(type, 0);
}

/* ---------------------------------------------------------- modal dialogs */
enum { DK_NONE, DK_NEWFILE, DK_NEWDIR, DK_RENAME, DK_SAVEAS, DK_DELETE, DK_DISCARD, DK_MSG, DK_FORMAT, DK_INSTALL, DK_UNINSTALL };
enum { PA_NONE, PA_OPEN, PA_NEW, PA_CLOSE };
typedef struct {
    int kind; char buf[NAME_MAX + 2]; int len, fresh, target, pa, pa_arg;
    char msg[120]; char title[40];
} Dlg;
static Dlg dlg;

static void dlg_text(int kind, const char *title, const char *msg, const char *prefill, int target) {
    memset(&dlg, 0, sizeof dlg);
    dlg.kind = kind; dlg.target = target; dlg.fresh = 1;
    scpy(dlg.title, title, sizeof dlg.title); scpy(dlg.msg, msg, sizeof dlg.msg);
    scpy(dlg.buf, prefill, NAME_MAX + 1); dlg.len = slen(dlg.buf);
}
static void dlg_confirm(int kind, const char *title, const char *msg, int target) {
    memset(&dlg, 0, sizeof dlg);
    dlg.kind = kind; dlg.target = target;
    scpy(dlg.title, title, sizeof dlg.title); scpy(dlg.msg, msg, sizeof dlg.msg);
}
static void dlg_message(const char *msg) {
    memset(&dlg, 0, sizeof dlg);
    dlg.kind = DK_MSG;
    scpy(dlg.title, "NovaOS", sizeof dlg.title); scpy(dlg.msg, msg, sizeof dlg.msg);
}
static int dlg_has_input(void) { return dlg.kind >= DK_NEWFILE && dlg.kind <= DK_SAVEAS; }
static void dlg_ok(void);              /* apps.h */

/* --------------------------------------------------------- screen resolution */
static void apply_mode(int nw, int nh, u32 pitch) {
    W = nw; H = nh; fbpitch = pitch;
    memset(back, 0, (u32)W * H * 4);
    present_reset(); dock_bg_ok = 0; cur_x = cur_y = -100;
    wp_apply();
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used) win_clamp(&wins[i]);
    mx = clampi(mx, 0, W - 1); my = clampi(my, 0, H - 1);
    dirty = 1;
}
/* switch the video mode at runtime (needs a Bochs/QEMU/VirtualBox VGA) */
static int change_resolution(int nw, int nh) {
    if (nw > MAXW || nh > MAXH || nw < 640 || nh < 480) return 0;
    int ow = W, oh = H;
    u32 pitch = bga_set(nw, nh);
    if (!pitch) { if (bga_ver) bga_set(ow, oh); return 0; }
    apply_mode(nw, nh, pitch);
    return 1;
}
#endif

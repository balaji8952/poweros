/* shell.h - window chrome, menu bar, macOS-style dock, dialogs, input, render */
#ifndef NOVA_SHELL_H
#define NOVA_SHELL_H
#include "apps.h"

#define K_LEFT  0x81
#define K_RIGHT 0x82
#define K_UP    0x83
#define K_DOWN  0x84
#define K_HOME  0x85
#define K_END   0x86
#define K_DEL   0x87
#define K_F2    0x88
#define K_PGUP  0x89
#define K_PGDN  0x8A

/* ------------------------------------------------------------ system */
static void sys_restart(void) {
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) {}
    outb(0x64, 0xFE);
#ifndef NOVA_HOST
    for (;;) __asm__ volatile("hlt");
#endif
}
static void sys_shutdown(void) {
    outw(0x604, 0x2000);   /* QEMU / newer Bochs */
    outw(0xB004, 0x2000);  /* old Bochs/QEMU */
    outw(0x4004, 0x3400);  /* VirtualBox */
    outw(0x600, 0x34);
    dst = back;
    fill_rect(0, 0, W, H, 0x000000);
    const char *m = "It is now safe to turn off your computer.";
    draw_text((W - slen(m) * 8) / 2, H / 2 - 8, m, 0xffa500);
    present();
#ifndef NOVA_HOST
    for (;;) __asm__ volatile("cli; hlt");
#endif
}

/* ------------------------------------------------------- window actions */
static void toggle_max(int i) {
    Win *w = &wins[i];
    if (!app_resizable[w->type]) return;
    if (!w->max) { w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h; w->max = 1; win_fit_max(w); }
    else { w->max = 0; w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh; win_clamp(w); }
}
static void request_close(int i) {
    if (wins[i].type == APP_NOTEPAD && np_mod) {
        char m[120], *p = m;
        p = put_str(p, "Discard unsaved changes to \""); p = put_str(p, np_file >= 0 ? fsn[np_file].name : "Untitled");
        p = put_str(p, "\"?"); *p = 0;
        dlg_confirm(DK_DISCARD, "Unsaved changes", m, 0);
        dlg.pa = PA_CLOSE;
        return;
    }
    destroy_win(i);
}
static const char *win_title(const Win *w, char *buf) {
    char *p = buf;
    switch (w->type) {
    case APP_NOTEPAD:
        p = put_str(p, np_file >= 0 && fsn[np_file].used ? fsn[np_file].name : "Untitled");
        if (np_mod) p = put_str(p, " *");
        p = put_str(p, " - Notepad"); *p = 0; return buf;
    case APP_IMAGES:
        if (iv_node >= 0 && fsn[iv_node].used) { p = put_str(p, fsn[iv_node].name); p = put_str(p, " - Photos"); *p = 0; return buf; }
        return "Photos";
    case APP_FILES: {
        char path[48]; fs_path(fm_cwd, path, sizeof path);
        p = put_str(p, fm_cwd > 0 ? fsn[fm_cwd].name : "Files"); p = put_str(p, " - Files"); *p = 0; (void)path; return buf; }
    case APP_DISPLAY: return "Display";
    case APP_TEXTVIEW: if (tv[w->inst].used) return tv[w->inst].title; return "App";
    default: return "About NovaOS";
    }
}

/* -------------------------------------------------------- window chrome */
static u32 corner_sv[4][64];
static void corner_io(int x, int y, int w, int h, int save) {
    const int r = 8;
    for (int c = 0; c < 4; c++) {
        int ox = (c & 1) ? x + w - r : x, oy = (c & 2) ? y + h - r : y;
        for (int j = 0; j < r; j++) for (int i = 0; i < r; i++) {
            int px = ox + i, py = oy + j;
            if (px < 0 || py < 0 || px >= W || py >= H) continue;
            if (save) corner_sv[c][j * r + i] = dst[py * W + px];
            else {
                int row = (c & 2) ? h - r + j : j;
                int ins = rr_inset(r, row, h);
                int outside = (c & 1) ? (i >= r - ins) : (i < ins);
                if (outside) dst[py * W + px] = corner_sv[c][j * r + i];
            }
        }
    }
}
static void tl_glyph(int cx, int cy, int kind, u32 c) {
    if (kind == 0) { for (int k = -2; k <= 2; k++) { fill_rect(cx + k, cy + k, 1, 1, c); fill_rect(cx + k, cy - k, 1, 1, c); } }
    else if (kind == 1) fill_rect(cx - 3, cy, 7, 1, c);
    else { fill_rect(cx - 3, cy, 7, 1, c); fill_rect(cx, cy - 3, 1, 7, c); }
}
static void draw_window(int i, int active) {
    Win *w = &wins[i];
    int round = !w->max;
    if (round) {
        for (int k = 1; k <= 6; k++)                          /* soft shadow */
            for (int e = 0; e < 4; e++) {
                int a = 40 - k * 6;
                if (e == 0) fill_rect_a(w->x - k + 4, w->y + w->h + k - 1 + 2, w->w + 2 * k - 8, 1, 0, a);
                else if (e == 1) fill_rect_a(w->x - k, w->y + 6, 1, w->h - 4 + k, 0, a);
                else if (e == 2) fill_rect_a(w->x + w->w + k - 1, w->y + 6, 1, w->h - 4 + k, 0, a);
            }
        corner_io(w->x, w->y, w->w, w->h, 1);
    }
    fill_rect(w->x, w->y, w->w, w->h, C_WIN);
    if (active) fill_vgrad(w->x, w->y, w->w, TITLE_H, 0xf1f1f5, 0xd8d9df);
    else        fill_rect(w->x, w->y, w->w, TITLE_H, 0xe9e9ed);
    fill_rect(w->x, w->y + TITLE_H - 1, w->w, 1, active ? 0xa9acb6 : 0xc4c6cd);
    int grp = hov_in(w->x + 8, w->y + 8, 66, 14);
    u32 col[3] = { 0xff5f57, 0xfebc2e, 0x28c840 };
    for (int b = 0; b < 3; b++) {
        int cx = w->x + 18 + b * 20, cy = w->y + 15;
        int enabled = b < 2 || app_resizable[w->type];
        fill_circle(cx, cy, 6, active && enabled ? col[b] : 0xc9cad0);
        if (grp && active && enabled) tl_glyph(cx, cy, b == 2 ? 2 : b, 0x4a2a22);
    }
    char tb[96]; const char *t = win_title(w, tb);
    int tw = mini(slen(t) * 8, w->w - 160);
    draw_text_fit(w->x + (w->w - tw) / 2, w->y + 7, t, tw, active ? C_TXT : 0x8a8e9a);
    frame_rect(w->x, w->y, w->w, w->h, active ? 0x8e929e : 0xb4b7c0);
    switch (w->type) {
    case APP_FILES:   draw_files(w);   break;
    case APP_NOTEPAD: draw_notepad(w); break;
    case APP_IMAGES:  draw_images(w);  break;
    case APP_DISPLAY: draw_display(w); break;
    case APP_TEXTVIEW: draw_textview(w); break;
    default:          draw_about(w);   break;
    }
    if (app_resizable[w->type] && !w->max)                    /* resize grip */
        for (int k = 0; k < 3; k++) fill_rect(w->x + w->w - 5 - k * 4, w->y + w->h - 3, 1, 1, 0x7c808c),
                                    fill_rect(w->x + w->w - 3, w->y + w->h - 5 - k * 4, 1, 1, 0x7c808c);
    if (round) corner_io(w->x, w->y, w->w, w->h, 0);
}

/* ------------------------------------------------- menu bar, Start menu, calendar */
#define START_W 84
static const char *wday_name[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *month_name[12] = { "January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December" };
static void draw_dock_icon(int app, int x, int y, int s);

#define SM_W 296
#define SM_MAX_VIS 8                                 /* installed apps listed before "More..." */
static const int sm_app[5] = { APP_FILES, APP_NOTEPAD, APP_IMAGES, APP_DISPLAY, APP_ABOUT };
static const char *sm_name[5] = { "Files", "Notepad", "Photos", "Display Settings", "About NovaOS" };
enum { SMK_BUILTIN, SMK_INSTALLED, SMK_HEADER, SMK_SEP, SMK_MORE, SMK_RESTART, SMK_SHUTDOWN };
typedef struct { int kind, arg, y, h; } SmItem;
#define SM_ITEMS_MAX (5 + SM_MAX_VIS + 8)
/* Lay out the Start menu: built-in apps, then the Installed section (from the registry), then power. */
static int sm_build(SmItem *it, int *total_h) {
    reg_refresh(0);
    int n = 0, y = MENUBAR_H + 2 + 10;
    it[n].kind = SMK_HEADER; it[n].arg = 0; it[n].y = y; it[n].h = 22; n++; y += 22;
    for (int i = 0; i < 5; i++) { it[n].kind = SMK_BUILTIN; it[n].arg = i; it[n].y = y; it[n].h = 38; n++; y += 38; }
    it[n].kind = SMK_HEADER; it[n].arg = 1; it[n].y = y + 6; it[n].h = 24; n++; y += 30;
    int vis = mini(reg_n, SM_MAX_VIS);
    for (;;) {                                         /* shrink the list on small screens */
        int more = reg_n > vis;
        int need = y + (reg_n ? vis * 36 : 26) + (more ? 30 : 0) + 14 + 2 * 32 + 8;
        if (need <= H - 6 || vis <= 1) break;
        vis--;
    }
    if (!reg_n) { it[n].kind = SMK_HEADER; it[n].arg = 2; it[n].y = y; it[n].h = 26; n++; y += 26; }
    for (int i = 0; i < vis; i++) { it[n].kind = SMK_INSTALLED; it[n].arg = i; it[n].y = y; it[n].h = 36; n++; y += 36; }
    if (reg_n > vis) { it[n].kind = SMK_MORE; it[n].arg = 0; it[n].y = y; it[n].h = 30; n++; y += 30; }
    it[n].kind = SMK_SEP; it[n].arg = 0; it[n].y = y; it[n].h = 14; n++; y += 14;
    it[n].kind = SMK_RESTART;  it[n].arg = 0; it[n].y = y; it[n].h = 32; n++; y += 32;
    it[n].kind = SMK_SHUTDOWN; it[n].arg = 0; it[n].y = y; it[n].h = 32; n++; y += 32;
    *total_h = y + 8 - (MENUBAR_H + 2);
    return n;
}
static int sm_hit(const SmItem *it, int n, int px, int py) {
    for (int i = 0; i < n; i++) {
        int k = it[i].kind;
        if (k == SMK_HEADER || k == SMK_SEP) continue;
        if (inrect(px, py, 12, it[i].y, SM_W - 12, it[i].h)) return i;
    }
    return -1;
}
static int sm_x_button(const SmItem *i, int px, int py) {        /* the little "remove" button on an installed row */
    return i->kind == SMK_INSTALLED && inrect(px, py, 6 + SM_W - 40, i->y + 6, 24, 24);
}
static void start_activate(const SmItem *i, int px, int py) {
    menu_open = 0;
    switch (i->kind) {
    case SMK_BUILTIN: open_app(sm_app[i->arg]); break;
    case SMK_INSTALLED:
        if (i->arg < 0 || i->arg >= reg_n) break;
        if (sm_x_button(i, px, py)) { pkg_uninstall_prompt(reg[i->arg].node); break; }
        { int e = pkg_launch_reg(i->arg); if (e) pkg_error_dialog(e); }
        break;
    case SMK_MORE: { int a = pkg_apps_dir(); if (a > 0) { fm_go(a); open_app(APP_FILES); } } break;
    case SMK_RESTART: sys_restart(); break;
    case SMK_SHUTDOWN: sys_shutdown(); break;
    default: break;
    }
}
static void draw_menubar(void) {
    fill_rect_a(0, 0, W, MENUBAR_H, 0x0d1016, 215);
    fill_rect_a(0, MENUBAR_H - 1, W, 1, 0xffffff, 28);
    if (menu_open || hov_in(4, 2, START_W - 4, MENUBAR_H - 4)) rrect(4, 2, START_W - 4, MENUBAR_H - 4, 5, 0xffffff, 0xffffff, 60);
    fill_circle(19, 13, 8, C_ACC);
    draw_char(15, 5, 'N', 0xffffff);
    draw_text_bold(34, 5, "Start", 0xffffff);
    int act = top_visible();
    draw_text_bold(START_W + 18, 5, act < 0 ? "NovaOS" : (wins[act].type == APP_TEXTVIEW && tv[wins[act].inst].used) ? tv[wins[act].inst].name : app_names[wins[act].type], 0xe8ebf4);
    /* date + time, right aligned: "Mon 12 Oct  14:05" */
    char t[32], *p = t;
    for (int i = 0; i < 3; i++) *p++ = wday_name[dt_wd][i];
    *p++ = ' '; p = put_u32(p, dt_d); *p++ = ' ';
    for (int i = 0; i < 3; i++) *p++ = month_name[dt_m - 1][i];
    *p++ = ' '; *p++ = ' ';
    *p++ = '0' + t_h / 10; *p++ = '0' + t_h % 10; *p++ = ':'; *p++ = '0' + t_m / 10; *p++ = '0' + t_m % 10; *p = 0;
    int tw = slen(t) * 8;
    if (cal_open || hov_in(W - tw - 18, 2, tw + 14, MENUBAR_H - 4)) rrect(W - tw - 18, 2, tw + 14, MENUBAR_H - 4, 5, 0xffffff, 0xffffff, 60);
    draw_text(W - tw - 11, 5, t, 0xe8ebf4);
}
static void sm_icon(int x, int y, int sz, u32 accent, char letter) {   /* tile for an installed app */
    rrect(x, y, sz, sz, 7, blend(accent, 0xFFFFFF, 80), accent, 256);
    draw_char(x + (sz - 8) / 2, y + (sz - 16) / 2, letter >= 'a' && letter <= 'z' ? letter - 32 : letter, 0xFFFFFF);
}
static int sm_app_running(const PkgInfo *p) {
    if (p->type == PKG_T_TEXTVIEW) for (int i = 0; i < TV_MAX; i++) if (tv[i].used && !memcmp(tv[i].id, p->id, (u32)slen(p->id) + 1)) return 1;
    return 0;
}
static void draw_menu(void) {                      /* the Start menu */
    SmItem it[SM_ITEMS_MAX]; int th, n = sm_build(it, &th);
    int x = 6, y0 = MENUBAR_H + 2;
    rrect(x - 1, y0 - 1, SM_W + 2, th + 2, 11, 0x4a5064, 0x4a5064, 256);
    rrect(x, y0, SM_W, th, 10, 0x1e2230, 0x171a25, 256);
    for (int i = 0; i < n; i++) {
        const SmItem *m = &it[i];
        int y = m->y;
        if (m->kind == SMK_HEADER) {
            if (m->arg == 2) draw_text(x + 16, y + 4, "No installed applications", 0x5d6579);
            else draw_text(x + 16, y + 6, m->arg == 0 ? "APPLICATIONS" : "INSTALLED", 0x7d869c);
            continue;
        }
        if (m->kind == SMK_SEP) { fill_rect(x + 14, y + 6, SM_W - 28, 1, 0x3a4157); continue; }
        int hot = inrect(mx, my, x + 6, y, SM_W - 12, m->h);
        if (hot) rrect(x + 6, y, SM_W - 12, m->h - 2, 7, C_ACC, C_ACC, 256);
        switch (m->kind) {
        case SMK_BUILTIN:
            draw_dock_icon(sm_app[m->arg], x + 16, y + 4, 28);
            draw_text(x + 54, y + 9, sm_name[m->arg], 0xffffff);
            if (find_win(sm_app[m->arg]) >= 0) fill_circle(x + SM_W - 22, y + 17, 2, hot ? 0xffffff : 0x7d869c);
            break;
        case SMK_INSTALLED: {
            const PkgInfo *p = &reg[m->arg].info;
            sm_icon(x + 16, y + 4, 28, p->accent, p->name[0]);
            draw_text_fit(x + 54, y + 10, p->name, SM_W - 54 - 62, 0xffffff);
            if (sm_app_running(p)) fill_circle(x + SM_W - 48, y + 17, 2, hot ? 0xffffff : 0x7d869c);
            if (hot) {                                                  /* uninstall button */
                int xb = sm_x_button(m, mx, my);
                rrect(x + SM_W - 40, y + 6, 24, 24, 6, xb ? 0xe5484d : 0x2f6fed, xb ? 0xc23a3f : 0x2f6fed, 256);
                draw_text(x + SM_W - 40 + 8, y + 10, "x", 0xffffff);
            }
            break; }
        case SMK_MORE: draw_text(x + 54, y + 6, "More...", 0xffffff); break;
        case SMK_RESTART:
            fill_circle(x + 30, y + 14, 6, 0xfebc2e); draw_text(x + 54, y + 6, "Restart", 0xffffff); break;
        case SMK_SHUTDOWN:
            fill_circle(x + 30, y + 14, 6, 0xff5f57); draw_text(x + 54, y + 6, "Shut Down", 0xffffff); break;
        default: break;
        }
    }
}
#define CAL_W 252
#define CAL_H 292
static int cal_x(void) { return W - CAL_W - 6; }
static void cal_open_now(void) { cal_open = 1; menu_open = 0; cal_vy = dt_y; cal_vm = dt_m; }
static void draw_calendar(void) {
    int x = cal_x(), y = MENUBAR_H + 2;
    rrect(x - 1, y - 1, CAL_W + 2, CAL_H + 2, 11, 0x4a5064, 0x4a5064, 256);
    rrect(x, y, CAL_W, CAL_H, 10, 0x1e2230, 0x171a25, 256);
    char l[40], *p = l;
    p = put_str(p, wday_name[dt_wd]); p = put_str(p, ", "); p = put_u32(p, dt_d); *p++ = ' ';
    p = put_str(p, month_name[dt_m - 1]); *p++ = ' '; p = put_u32(p, dt_y); *p = 0;
    draw_text_bold(x + (CAL_W - slen(l) * 8) / 2, y + 14, l, 0xffffff);
    btn(x + 10, y + 42, 30, 24, "<", 1);
    btn(x + CAL_W - 40, y + 42, 30, 24, ">", 1);
    p = l; p = put_str(p, month_name[cal_vm - 1]); *p++ = ' '; p = put_u32(p, cal_vy); *p = 0;
    draw_text(x + (CAL_W - slen(l) * 8) / 2, y + 46, l, 0xe8ebf4);
    static const char *wd[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
    for (int i = 0; i < 7; i++) draw_text(x + 7 + i * 34 + 9, y + 78, wd[i], 0x7d869c);
    int first = day_of_week(cal_vy, cal_vm, 1), dim = days_in_month(cal_vy, cal_vm);
    for (int d = 1; d <= dim; d++) {
        int idx = first + d - 1, cx = x + 7 + (idx % 7) * 34 + 17, cy = y + 112 + (idx / 7) * 28;
        int today = d == dt_d && cal_vm == dt_m && cal_vy == dt_y;
        if (today) fill_circle(cx, cy, 13, C_ACC);
        char n[4], *q = put_u32(n, d); *q = 0;
        draw_text(cx - slen(n) * 4, cy - 8, n, today ? 0xffffff : ((idx % 7) == 0 ? 0xff8a80 : 0xe8ebf4));
    }
}
static void cal_click(void) {
    int x = cal_x(), y = MENUBAR_H + 2;
    if (inrect(mx, my, x + 10, y + 42, 30, 24)) { if (--cal_vm < 1) { cal_vm = 12; cal_vy--; } }
    else if (inrect(mx, my, x + CAL_W - 40, y + 42, 30, 24)) { if (++cal_vm > 12) { cal_vm = 1; cal_vy++; } }
}

/* ----------------------------------------------------------------- dock */
#define DOCK_BUILTIN       4
#define DOCK_MAX_INSTALLED 8                         /* more installed apps are reachable from the Start menu */
#define DOCK_MAXN          (DOCK_BUILTIN + DOCK_MAX_INSTALLED)
#define DK_BASE_MAX 52
#define DK_GAP  10
#define DK_SEP  14                                   /* extra space + divider between built-in and installed apps */
#define DK_BOT  24                                   /* icon bottoms sit this far above the screen edge */
static const int dock_app[DOCK_BUILTIN] = { APP_FILES, APP_NOTEPAD, APP_IMAGES, APP_DISPLAY };

/* The dock is built from the built-in apps plus the registry of installed apps; icons shrink to fit narrow screens. */
typedef struct { int n, nb, base, maxsz, radius, basetotal, x0, sep; } DockGeo;
static void dock_geo(DockGeo *g) {
    reg_refresh(0);
    int ni = mini(reg_n, DOCK_MAX_INSTALLED);
    g->nb = DOCK_BUILTIN; g->n = DOCK_BUILTIN + ni; g->sep = ni ? DK_SEP : 0;
    int base = DK_BASE_MAX;                           /* shrink until it fits, with room for the hover magnification */
    for (; base > 28; base--) {
        int need = g->n * base + (g->n - 1) * DK_GAP + g->sep;
        if (need <= 800 && need + base * 11 / 5 + 28 <= W - 16) break;
    }
    g->base = base; g->maxsz = base * 165 / 100; g->radius = base * 5 / 2;
    g->basetotal = g->n * base + (g->n - 1) * DK_GAP + g->sep;
    g->x0 = (W - g->basetotal) / 2;
}
static int in_dock_zone(int x, int y) {
    if (dlg.kind || y < H - 130) return 0;
    DockGeo g; dock_geo(&g);
    return x >= g.x0 - 40 && x < g.x0 + g.basetotal + 40;
}
static void dock_layout(const DockGeo *g, int *xs, int *sz) {
    int zone = in_dock_zone(mx, my), total = (g->n - 1) * DK_GAP + g->sep;
    for (int i = 0; i < g->n; i++) {
        int s = g->base;
        if (zone) {
            int left = g->x0 + i * (g->base + DK_GAP) + (i >= g->nb ? g->sep : 0);
            int cx = left + g->base / 2, d = mx > cx ? mx - cx : cx - mx;
            if (d < g->radius) { int t = d * 256 / g->radius, wgt = 256 - t * t / 256; s = g->base + (g->maxsz - g->base) * wgt / 256; }
        }
        sz[i] = s; total += s;
    }
    int x = (W - total) / 2;
    for (int i = 0; i < g->n; i++) { xs[i] = x; x += sz[i] + DK_GAP + ((i == g->nb - 1) ? g->sep : 0); }
}
static int dock_hit(const DockGeo *g, const int *xs, const int *sz) {
    for (int i = 0; i < g->n; i++)
        if (inrect(mx, my, xs[i] - DK_GAP / 2, H - DK_BOT - sz[i], sz[i] + DK_GAP, sz[i] + DK_BOT)) return i;
    return -1;
}
/* icons are authored on a 64x64 grid and scaled */
static int ic_x, ic_y, ic_s;
static int ic_c(int v) { return v * ic_s / 64; }
static void ic_r(int x, int y, int w, int h, u32 c) { fill_rect(ic_x + ic_c(x), ic_y + ic_c(y), maxi(1, ic_c(x + w) - ic_c(x)), maxi(1, ic_c(y + h) - ic_c(y)), c); }
static void ic_rr(int x, int y, int w, int h, int r, u32 c1, u32 c2) { rrect(ic_x + ic_c(x), ic_y + ic_c(y), maxi(2, ic_c(x + w) - ic_c(x)), maxi(2, ic_c(y + h) - ic_c(y)), maxi(1, ic_c(r)), c1, c2, 256); }
static void ic_o(int cx, int cy, int r, u32 c) { fill_circle(ic_x + ic_c(cx), ic_y + ic_c(cy), maxi(1, ic_c(r)), c); }
static void draw_dock_icon(int app, int x, int y, int s) {
    ic_x = x; ic_y = y; ic_s = s;
    if (app == APP_FILES) {
        ic_rr(0, 0, 64, 64, 14, 0x62b8ff, 0x1f64dc);
        ic_rr(10, 14, 24, 14, 4, 0xd8ebff, 0xbcd8ff);
        ic_rr(8, 22, 48, 32, 5, 0xffffff, 0xd2e3ff);
        ic_r(8, 30, 48, 1, 0xb5cdf3);
    } else if (app == APP_NOTEPAD) {
        ic_rr(0, 0, 64, 64, 14, 0xffffff, 0xdde0e8);
        ic_rr(0, 0, 64, 18, 14, 0xffd24a, 0xffb400);
        ic_r(0, 10, 64, 8, 0xffb400);
        for (int k = 0; k < 4; k++) ic_r(12, 28 + k * 8, 40, 3, 0xb3b9c8);
    } else if (app == APP_IMAGES) {
        ic_rr(0, 0, 64, 64, 14, 0x8fd2ff, 0x3f86f5);
        ic_o(44, 20, 8, 0xffe066);
        for (int k = 0; k < 22; k++) ic_r(20 - k, 30 + k, 2 * k + 1, 1, 0x6b7ea6);
        ic_rr(0, 46, 64, 18, 14, 0x55d386, 0x2a9d55);
        ic_r(0, 46, 64, 8, 0x55d386);
    } else if (app == APP_ABOUT) {
        ic_rr(0, 0, 64, 64, 14, 0x5d86f0, 0x27479a);
        ic_o(32, 32, 20, 0xffffff);
        ic_r(29, 28, 6, 18, 0x27479a); ic_r(29, 18, 6, 6, 0x27479a);
    } else {
        ic_rr(0, 0, 64, 64, 14, 0x7b8294, 0x30343f);
        ic_rr(10, 14, 44, 30, 4, 0xe4e8ef, 0xb8bfcb);
        ic_rr(13, 17, 38, 23, 2, 0x7ad0ff, 0x2f78f0);
        ic_r(29, 44, 6, 6, 0xc3c8d2);
        ic_rr(21, 50, 22, 4, 2, 0xd0d4dc, 0xd0d4dc);
    }
}
/* an integer-scaled copy of a font glyph (used for the letter on installed-app tiles) */
static void draw_char_scaled(int x, int y, char ch, int sc, u32 c) {
    if (ch < 32 || ch > 126) return;
    const u8 *gl = font[(u8)ch];
    for (int r = 0; r < 16; r++) for (int k = 0; k < 8; k++)
        if (gl[r] & (0x80 >> k)) fill_rect(x + k * sc, y + r * sc, sc, sc, c);
}
static void draw_installed_icon(const PkgInfo *p, int x, int y, int s) {
    rrect(x, y, s, s, maxi(3, s * 14 / 64), blend(p->accent, 0xFFFFFF, 90), p->accent, 256);
    int sc = maxi(1, s / 26);
    char l = p->name[0]; if (l >= 'a' && l <= 'z') l -= 32;
    draw_char_scaled(x + (s - 8 * sc) / 2, y + (s - 16 * sc) / 2, l, sc, 0xFFFFFF);
}
static const char *dock_item_name(const DockGeo *g, int i) { return i < g->nb ? app_names[dock_app[i]] : reg[i - g->nb].info.name; }
static int dock_item_running(const DockGeo *g, int i) { return i < g->nb ? find_win(dock_app[i]) >= 0 : sm_app_running(&reg[i - g->nb].info); }
static void draw_dock(void) {
    DockGeo g; dock_geo(&g);
    int xs[DOCK_MAXN], sz[DOCK_MAXN]; dock_layout(&g, xs, sz);
    int px = xs[g.n - 1] + sz[g.n - 1] + 14, x0 = xs[0] - 14, ph = 78, py = H - 8 - ph;
    rrect(x0 - 1, py - 1, px - x0 + 2, ph + 2, 23, 0xffffff, 0xffffff, 50);
    rrect(x0, py, px - x0, ph, 22, 0xe6ecfb, 0xc5d0ea, 96);
    if (g.sep) fill_rect_a(xs[g.nb] - (DK_GAP + g.sep) / 2, H - DK_BOT - g.base, 1, g.base, 0x5d6579, 150);   /* divider */
    int hot = dlg.kind ? -1 : dock_hit(&g, xs, sz);
    for (int i = 0; i < g.n; i++) {
        if (i < g.nb) draw_dock_icon(dock_app[i], xs[i], H - DK_BOT - sz[i], sz[i]);
        else draw_installed_icon(&reg[i - g.nb].info, xs[i], H - DK_BOT - sz[i], sz[i]);
        if (dock_item_running(&g, i)) fill_circle(xs[i] + sz[i] / 2, H - 14, 2, 0xffffff);
    }
    if (hot >= 0) {
        const char *n = dock_item_name(&g, hot);
        int tw = slen(n) * 8 + 20, tx = xs[hot] + sz[hot] / 2 - tw / 2, ty = H - DK_BOT - sz[hot] - 34;
        tx = clampi(tx, 4, W - tw - 4);
        rrect(tx, ty, tw, 24, 8, 0x1c1f28, 0x1c1f28, 225);
        draw_text(tx + 10, ty + 4, n, 0xffffff);
    }
}
static void dock_activate(const DockGeo *g, int i) {
    if (i >= g->nb) { int e = pkg_launch_reg(i - g->nb); if (e) pkg_error_dialog(e); return; }
    int app = dock_app[i], wi = find_win(app);
    if (wi >= 0) { wins[wi].min = 0; focus_win(wi); }
    else open_app(app);
}

/* -------------------------------------------------------------- dialogs */
typedef struct { int x, y, w, h; } Box;
static Box dlg_box(void) { Box b; b.w = 420; b.h = dlg_has_input() ? 168 : 138; b.x = (W - b.w) / 2; b.y = (H - b.h) / 2 - 20; return b; }
static void draw_dialog(void) {
    for (int i = 0; i < W * H; i++) { u32 q = (back[i] >> 2) & 0x3F3F3F; back[i] = q * 3; }   /* dim to 75% */
    Box b = dlg_box();
    rrect(b.x - 1, b.y - 1, b.w + 2, b.h + 2, 11, 0x80848f, 0x80848f, 256);
    rrect(b.x, b.y, b.w, b.h, 10, 0xf7f7fa, 0xe9eaef, 256);
    draw_text_bold(b.x + 20, b.y + 16, dlg.title, C_TXT);
    int ml = slen(dlg.msg);                                   /* message, wrapped to two lines */
    int per = (b.w - 40) / 8;
    if (ml <= per) draw_text(b.x + 20, b.y + 42, dlg.msg, C_DIM);
    else {
        int cut = per; while (cut > 0 && dlg.msg[cut] != ' ') cut--;
        if (cut <= 0) cut = per;
        draw_text_n(b.x + 20, b.y + 42, dlg.msg, cut, C_DIM);
        draw_text_fit(b.x + 20, b.y + 60, dlg.msg + cut + (dlg.msg[cut] == ' '), b.w - 40, C_DIM);
    }
    if (dlg_has_input()) {
        fill_rect(b.x + 20, b.y + 70, b.w - 40, 30, 0x8a8f9c);
        fill_rect(b.x + 21, b.y + 71, b.w - 42, 28, 0xffffff);
        int tw = dlg.len * 8;
        if (dlg.fresh && dlg.len) fill_rect(b.x + 26, b.y + 76, tw + 2, 18, 0xb8d0ff);
        draw_text(b.x + 27, b.y + 77, dlg.buf, C_TXT);
        fill_rect(b.x + 27 + tw, b.y + 77, 2, 16, C_ACC);
    }
    const char *okl = dlg.kind == DK_DELETE ? "Delete" : dlg.kind == DK_MSG ? "OK" : dlg.kind == DK_DISCARD ? "Discard" : dlg.kind == DK_FORMAT ? "Format" : dlg.kind == DK_INSTALL ? "Install" : dlg.kind == DK_UNINSTALL ? "Uninstall" :
                      dlg.kind == DK_SAVEAS ? "Save" : dlg.kind == DK_RENAME ? "Rename" : "Create";
    btn(b.x + b.w - 110, b.y + b.h - 42, 90, 28, okl, 1);
    if (dlg.kind != DK_MSG) btn(b.x + b.w - 210, b.y + b.h - 42, 90, 28, "Cancel", 1);
}
static void dlg_click(void) {
    Box b = dlg_box();
    if (inrect(mx, my, b.x + b.w - 110, b.y + b.h - 42, 90, 28)) dlg_ok();
    else if (dlg.kind != DK_MSG && inrect(mx, my, b.x + b.w - 210, b.y + b.h - 42, 90, 28)) dlg.kind = DK_NONE;
}
static void dlg_key(int k) {
    if (k == 27) { dlg.kind = DK_NONE; return; }
    if (k == '\n') { dlg_ok(); return; }
    if (!dlg_has_input()) return;
    if (k == '\b') { if (dlg.fresh) dlg.len = 0; else if (dlg.len > 0) dlg.len--; dlg.buf[dlg.len] = 0; dlg.fresh = 0; }
    else if (k >= 32 && k < 127 && !ctrl_down) {
        if (dlg.fresh) dlg.len = 0;
        dlg.fresh = 0;
        if (dlg.len < NAME_MAX) { dlg.buf[dlg.len++] = (char)k; dlg.buf[dlg.len] = 0; }
    }
}

/* --------------------------------------------------------------- cursor
 * The cursor is never drawn into the scene buffer.  It is written straight to
 * the screen (and the "shown" copy), and erased by restoring from the scene,
 * so moving the mouse over static content costs almost nothing.             */
static const char *cursor_bmp[19] = {
    "#", "##", "#.#", "#..#", "#...#", "#....#", "#.....#", "#......#", "#.......#",
    "#........#", "#.....#####", "#..#..#", "#.# #..#", "##  #..#", "#    #..#",
    "     #..#", "      #..#", "      #..#", "       ##" };
static void screen_px(int x, int y, u32 c) {
    shown[y * W + x] = c;
    *(u32 *)((u8 *)fbp + (u32)y * fbpitch + (u32)x * 4) = c;
}
static void cursor_erase(void) {
    for (int j = 0; j < 19; j++) {
        const char *row = cursor_bmp[j];
        for (int i = 0; row[i]; i++) {
            int x = cur_x + i, y = cur_y + j;
            if (row[i] == ' ' || x < 0 || y < 0 || x >= W || y >= H) continue;
            screen_px(x, y, back[y * W + x]);
        }
    }
}
static void cursor_draw(void) {
    for (int j = 0; j < 19; j++)
        for (int i = 0; cursor_bmp[j][i]; i++) {
            char c = cursor_bmp[j][i];
            int x = mx + i, y = my + j;
            if (c == ' ' || x >= W || y >= H) continue;
            screen_px(x, y, c == '#' ? 0x000000 : 0xFFFFFF);
        }
    cur_x = mx; cur_y = my;
}
static void cursor_move(void) { cursor_erase(); cursor_draw(); }

/* --------------------------------------------------------------- render */
static void compute_hover(void) {
    hover_win = -1;
    if (dlg.kind) { hover_win = -2; return; }
    if (menu_open || cal_open || my < MENUBAR_H || in_dock_zone(mx, my)) { hover_win = -3; return; }
    for (int k = nz - 1; k >= 0; k--) {
        Win *w = &wins[zo[k]];
        if (!w->min && inrect(mx, my, w->x, w->y, w->w, w->h)) { hover_win = zo[k]; return; }
    }
}
#define DOCKBG_W 1000
#define DOCKBG_H 160
static u32 dock_bg[DOCKBG_W * DOCKBG_H];           /* scene behind the dock, for dock-only updates */
static int dock_bx, dock_by;
static void dock_bg_save(int save) {
    dock_bx = (W - DOCKBG_W) / 2; if (dock_bx < 0) dock_bx = 0;
    dock_by = H - DOCKBG_H;
    int w = mini(DOCKBG_W, W - dock_bx);
    for (int j = 0; j < DOCKBG_H; j++) {
        if (dock_by + j < 0) continue;
        if (save) memcpy(dock_bg + j * DOCKBG_W, back + (dock_by + j) * W + dock_bx, (u32)w * 4);
        else      memcpy(back + (dock_by + j) * W + dock_bx, dock_bg + j * DOCKBG_W, (u32)w * 4);
    }
    dock_bg_ok = save ? 1 : dock_bg_ok;
}
static void render_full(void) {
    dst = back;
    compute_hover();
    copy32(back, wall, (u32)W * H);
    int act = top_visible();
    for (int k = 0; k < nz; k++) { int i = zo[k]; if (!wins[i].min) { cur_draw_win = i; draw_window(i, i == act); } }
    cur_draw_win = -3;
    draw_menubar();
    dock_bg_save(1);
    draw_dock();
    if (menu_open) draw_menu();
    if (cal_open) draw_calendar();
    if (dlg.kind) { cur_draw_win = -2; draw_dialog(); }
    present_rect(0, 0, W, H);                      /* only changed pixels reach the screen */
    cursor_draw();
}
static __attribute__((unused)) void render(void) { render_full(); }
/* hovering the dock only changes the dock: redraw just that strip */
static void render_dock(void) {
    if (!dock_bg_ok) { render_full(); return; }
    cursor_erase();
    dst = back;
    compute_hover();
    dock_bg_save(0);
    cur_draw_win = -3;
    draw_dock();
    present_rect(dock_bx, dock_by, dock_bx + DOCKBG_W, H);
    cursor_draw();
}
/* what does the pointer being at (x,y) affect?  0 = nothing, 1 = dock only, 2 = needs a full redraw */
static int hot_kind(int x, int y) {
    if (dlg.kind) { Box b = dlg_box(); return inrect(x, y, b.x, b.y, b.w, b.h) ? 2 : 0; }
    if (menu_open || cal_open || y < MENUBAR_H) return 2;
    if (in_dock_zone(x, y)) return 1;
    for (int k = nz - 1; k >= 0; k--) {
        Win *w = &wins[zo[k]];
        if (w->min || !inrect(x, y, w->x, w->y, w->w, w->h)) continue;
        if (y < w->y + TITLE_H) return x < w->x + 76 ? 2 : 0;       /* traffic-light hover */
        int cx, cy, cw, ch; content_rect(w, &cx, &cy, &cw, &ch);
        switch (w->type) {
        case APP_FILES: { FmL L; fm_layout(w, &L); return (y < cy + 36 || (L.sbw && x < cx + L.sbw && y >= cy + 60)) ? 2 : 0; }
        case APP_NOTEPAD: case APP_IMAGES: return y < cy + 36 ? 2 : 0;
        case APP_DISPLAY: return 2;
        default: return 0;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- input */
static void on_click(void) {
    if (dlg.kind) { dlg_click(); return; }
    if (menu_open) {
        SmItem it[SM_ITEMS_MAX]; int th, n = sm_build(it, &th);
        int h = sm_hit(it, n, mx, my);
        if (h >= 0) { start_activate(&it[h], mx, my); return; }
        if (inrect(mx, my, 6, MENUBAR_H + 2, SM_W, th)) return;
        if (my >= MENUBAR_H) { menu_open = 0; return; }
    }
    if (cal_open) {
        if (inrect(mx, my, cal_x(), MENUBAR_H + 2, CAL_W, CAL_H)) { cal_click(); return; }
        if (my >= MENUBAR_H) { cal_open = 0; return; }
    }
    if (my < MENUBAR_H) {
        if (mx < START_W) { menu_open = !menu_open; cal_open = 0; }
        else if (mx >= W - 190) { if (cal_open) cal_open = 0; else cal_open_now(); }
        else { menu_open = 0; cal_open = 0; }
        return;
    }
    { DockGeo g; dock_geo(&g);
      int xs[DOCK_MAXN], sz[DOCK_MAXN]; dock_layout(&g, xs, sz);
      int h = dock_hit(&g, xs, sz);
      if (h >= 0) { dock_activate(&g, h); return; }
      if (inrect(mx, my, xs[0] - 14, H - 86, xs[g.n - 1] + sz[g.n - 1] - xs[0] + 28, 78)) return; }
    for (int k = nz - 1; k >= 0; k--) {
        int i = zo[k]; Win *w = &wins[i];
        if (w->min || !inrect(mx, my, w->x, w->y, w->w, w->h)) continue;
        focus_win(i);
        if (my < w->y + TITLE_H) {
            int ty = w->y + 15, lx = w->x + 18;
            static u32 tclick_t; static int tclick_w = -1;
            if (inrect(mx, my, lx - 8, ty - 8, 16, 16)) request_close(i);
            else if (inrect(mx, my, lx + 12, ty - 8, 16, 16)) w->min = 1;
            else if (inrect(mx, my, lx + 32, ty - 8, 16, 16)) toggle_max(i);
            else {
                u32 t = now_ms();
                if (tclick_w == i && t - tclick_t < 400) { toggle_max(i); tclick_w = -1; }
                else {
                    tclick_w = i; tclick_t = t;
                    if (!w->max) { drag = i; drag_ox = mx - w->x; drag_oy = my - w->y; }
                }
            }
        } else if (app_resizable[w->type] && !w->max && inrect(mx, my, w->x + w->w - 18, w->y + w->h - 18, 18, 18)) {
            rsz = i; rsz_ox = w->x + w->w - mx; rsz_oy = w->y + w->h - my;
        } else switch (w->type) {
            case APP_FILES:   files_click(w);   break;
            case APP_NOTEPAD: notepad_click(w); break;
            case APP_IMAGES:  images_click(w);  break;
            case APP_DISPLAY: display_click(w); break;
            default: break;
        }
        return;
    }
}
static void on_wheel(int z) {
    for (int k = nz - 1; k >= 0; k--) {
        Win *w = &wins[zo[k]];
        if (w->min || !inrect(mx, my, w->x, w->y, w->w, w->h)) continue;
        if (w->type == APP_FILES) { fm_scroll += z * 3; fm_scroll_clamp(); }
        else if (w->type == APP_NOTEPAD) { int r, c; np_rowcol(np_len, &r, &c); np_scroll = clampi(np_scroll + z * 3, 0, maxi(0, r)); }
        else if (w->type == APP_TEXTVIEW) tv_scroll(w, z * 3, 0);
        return;
    }
}
/* one decoded mouse packet (dx, dy in PS/2 orientation: dy>0 is up) */
static void mouse_update(int dx, int dy, int btnmask, int wheel) {
    int ox = mx, oy = my;
    mx = clampi(mx + dx, 0, W - 1);
    my = clampi(my - dy, 0, H - 1);
    mbtn = btnmask & 1;
    int clicked = mbtn != mprev;
    if (mbtn && !mprev) on_click();
    if (!mbtn) { drag = -1; rsz = -1; }
    int dragging = (drag >= 0 || rsz >= 0) && mbtn;
    if (drag >= 0 && mbtn) {
        Win *w = &wins[drag];
        w->x = clampi(mx - drag_ox, 60 - w->w, W - 60);
        w->y = clampi(my - drag_oy, MENUBAR_H, H - 60);
    }
    if (rsz >= 0 && mbtn) {
        Win *w = &wins[rsz];
        w->w = clampi(mx + rsz_ox - w->x, app_minw[w->type], maxi(app_minw[w->type], W - w->x));
        w->h = clampi(my + rsz_oy - w->y, app_minh[w->type], maxi(app_minh[w->type], H - w->y));
    }
    if (wheel) on_wheel(wheel);
    mprev = mbtn;
    if (clicked || dragging || wheel) dirty = 1;
    else if (mx != ox || my != oy) {                /* plain pointer motion: pick the cheapest redraw */
        int k1 = hot_kind(ox, oy), k2 = hot_kind(mx, my);
        if (k1 == 2 || k2 == 2) dirty = 1;
        else if (k1 == 1 || k2 == 1) dirty_dock = 1;
        else dirty_cursor = 1;
    }
}
static int mpi; static u8 mpk[4];
static void mouse_byte(u8 d) {
    if (mpi == 0 && !(d & 8)) return;           /* resync */
    mpk[mpi++] = d;
    if (mpi < mouse_pkt) return;
    mpi = 0;
    if (mpk[0] & 0xC0) return;                  /* overflow */
    int dx = mpk[1] - ((mpk[0] << 4) & 0x100);
    int dy = mpk[2] - ((mpk[0] << 3) & 0x100);
    int z = 0;
    if (mouse_pkt == 4) { z = mpk[3] & 15; if (z & 8) z -= 16; }
    mouse_update(dx, dy, mpk[0], z);
}
/* run whatever redraw is pending (called from the main loop) */
static void ui_frame(void) {
    if (dirty) { dirty = dirty_dock = dirty_cursor = 0; render_full(); }
    else if (dirty_dock) { dirty_dock = dirty_cursor = 0; render_dock(); }
    else if (dirty_cursor) { dirty_cursor = 0; cursor_move(); }
}

static const char sc_norm[] =  "\0\033" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char sc_shift[] = "\0\033" "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static void files_key(int k) {
    int pos = -1;
    for (int i = 0; i < fm_n; i++) if (fm_list[i] == fm_sel) pos = i;
    if (ctrl_down && (k == 'n' || k == 'N')) { if (shift_down) fm_do_newdir(); else fm_do_newfile(); return; }
    switch (k) {
    case K_UP:   if (fm_n) fm_sel = fm_list[pos <= 0 ? 0 : pos - 1]; break;
    case K_DOWN: if (fm_n) fm_sel = fm_list[pos < 0 ? 0 : mini(pos + 1, fm_n - 1)]; break;
    case K_PGUP: if (fm_n) fm_sel = fm_list[maxi(0, (pos < 0 ? 0 : pos) - fm_rows())]; break;
    case K_PGDN: if (fm_n) fm_sel = fm_list[mini(fm_n - 1, (pos < 0 ? 0 : pos) + fm_rows())]; break;
    case K_HOME: if (fm_n) fm_sel = fm_list[0]; break;
    case K_END:  if (fm_n) fm_sel = fm_list[fm_n - 1]; break;
    case '\n':   if (fm_sel >= 0) fm_open_node(fm_sel); break;
    case '\b':   fm_up(); break;
    case K_DEL:  fm_do_delete(); break;
    case K_F2:   fm_do_rename(); break;
    default: break;
    }
    fm_ensure_visible();
}
static void key_event(int k) {
    if (dlg.kind) { dlg_key(k); return; }
    if (k == 27) { menu_open = cal_open = 0; dirty = 1; return; }
    int act = top_visible();
    if (act < 0) return;
    int type = wins[act].type;
    if (type == APP_FILES) files_key(k);
    else if (type == APP_NOTEPAD) {
        if (ctrl_down) {
            if (k == 's' || k == 'S') { if (shift_down) np_save_as(); else np_save(); }
            else if (k == 'o' || k == 'O') { open_app(APP_FILES); }
            else if (k == 'n' || k == 'N') np_request(PA_NEW, 0);
            return;
        }
        int r, c;
        switch (k) {
        case '\b': if (np_cur > 0) { memmove(np_buf + np_cur - 1, np_buf + np_cur, np_len - np_cur); np_cur--; np_len--; np_touch(); } break;
        case K_DEL: if (np_cur < np_len) { memmove(np_buf + np_cur, np_buf + np_cur + 1, np_len - np_cur - 1); np_len--; np_touch(); } break;
        case K_LEFT:  if (np_cur > 0) np_cur--; np_follow = 1; break;
        case K_RIGHT: if (np_cur < np_len) np_cur++; np_follow = 1; break;
        case K_UP:    np_rowcol(np_cur, &r, &c); np_cur = r == 0 ? 0 : np_index_at(r - 1, c); np_follow = 1; break;
        case K_DOWN:  np_rowcol(np_cur, &r, &c); np_cur = np_index_at(r + 1, c); np_follow = 1; break;
        case K_PGUP:  np_rowcol(np_cur, &r, &c); np_cur = np_index_at(maxi(0, r - np_rows), c); np_follow = 1; break;
        case K_PGDN:  np_rowcol(np_cur, &r, &c); np_cur = np_index_at(r + np_rows, c); np_follow = 1; break;
        case K_HOME:  np_rowcol(np_cur, &r, &c); np_cur = np_index_at(r, 0); np_follow = 1; break;
        case K_END:   np_rowcol(np_cur, &r, &c); np_cur = np_index_at(r, 100000); np_follow = 1; break;
        case '\t': for (int i = 0; i < 4; i++) np_insert(' '); break;
        case '\n': np_insert('\n'); break;
        default: if (k >= 32 && k < 127) np_insert((char)k);
        }
    } else if (type == APP_TEXTVIEW) {
        Win *w = &wins[act];
        if (k == K_UP) tv_scroll(w, -1, 0); else if (k == K_DOWN) tv_scroll(w, 1, 0);
        else if (k == K_PGUP) tv_scroll(w, -10, 0); else if (k == K_PGDN || k == ' ') tv_scroll(w, 10, 0);
        else if (k == K_HOME) tv_scroll(w, 0, -1); else if (k == K_END) tv_scroll(w, 0, 1);
    } else if (type == APP_IMAGES) {
        if (k == K_LEFT) iv_step(-1);
        if (k == K_RIGHT) iv_step(1);
    }
}
static int kext;
static void key_byte(u8 d) {
    if (d == 0xE0) { kext = 1; return; }
    int brk = d & 0x80, sc = d & 0x7F;
    int ext = kext; kext = 0;
    if (!ext && (sc == 0x2A || sc == 0x36)) { shift_down = !brk; return; }
    if (sc == 0x1D) { ctrl_down = !brk; return; }
    if (brk) return;
    int k = 0;
    if (ext) {
        switch (sc) {
            case 0x4B: k = K_LEFT; break;  case 0x4D: k = K_RIGHT; break;
            case 0x48: k = K_UP; break;    case 0x50: k = K_DOWN; break;
            case 0x47: k = K_HOME; break;  case 0x4F: k = K_END; break;
            case 0x53: k = K_DEL; break;   case 0x49: k = K_PGUP; break;
            case 0x51: k = K_PGDN; break;
        }
    } else if (sc == 0x3A) { caps_on = !caps_on; return; }
    else if (sc == 0x3C) k = K_F2;
    else if (sc < (int)sizeof(sc_norm) - 1) {
        k = (u8)(shift_down ? sc_shift[sc] : sc_norm[sc]);
        if (caps_on) {
            if (k >= 'a' && k <= 'z') k -= 32;
            else if (k >= 'A' && k <= 'Z' && shift_down) k += 32;
        }
    }
    if (k) { key_event(k); dirty = 1; }
}
#endif

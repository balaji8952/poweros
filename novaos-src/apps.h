/* apps.h - Files, Notepad, Photos, Display settings, About */
#ifndef NOVA_APPS_H
#define NOVA_APPS_H
#include "ui_base.h"

/* common: content rectangle of a window (below the title bar) */
static void content_rect(const Win *w, int *cx, int *cy, int *cw, int *ch) {
    *cx = w->x + 1; *cy = w->y + TITLE_H; *cw = w->w - 2; *ch = w->h - TITLE_H - 1;
}

static void open_notepad_file(int idx);
static void open_image_file(int idx);
static void pkg_prepare_install(int node);       /* package install flow (below) */
static void pkg_error_dialog(int err);
static void pkg_uninstall_prompt(int node);

/* ===================================================================== FILES */
#define FM_ROW 24
static int fm_cwd, fm_sel = -1, fm_scroll, fm_n;
static int fm_list[MAXNODES];
static u32 fm_click_t; static int fm_click_node = -1;
static const int fm_bw[5] = { 78, 90, 66, 62, 52 };
static const char *fm_bl[5] = { "New File", "New Folder", "Rename", "Delete", "Open" };

typedef struct { int cx, cy, cw, ch, sbw, lx, ly, lw, lh, rows, by, bh, bx[6]; } FmL;
static void fm_layout(const Win *w, FmL *L) {
    content_rect(w, &L->cx, &L->cy, &L->cw, &L->ch);
    L->by = L->cy + 6; L->bh = 24;
    int x = L->cx + 8;
    L->bx[0] = x; x += 40;
    for (int i = 0; i < 5; i++) { L->bx[i + 1] = x; x += fm_bw[i] + 6; }
    L->sbw = L->cw >= 470 ? 124 : 0;
    L->lx = L->cx + L->sbw; L->lw = L->cw - L->sbw;
    L->ly = L->cy + 36 + 24 + 22;
    L->lh = L->cy + L->ch - 22 - L->ly;
    L->rows = maxi(1, L->lh / FM_ROW);
}
static void fm_refresh(void) {
    if (!fs_alive(fm_cwd) || !fsn[fm_cwd].dir) fm_cwd = 0;
    fm_n = fs_children(fm_cwd, fm_list);
    if (fm_sel >= 0) {
        int found = 0;
        for (int i = 0; i < fm_n; i++) if (fm_list[i] == fm_sel) found = 1;
        if (!found) fm_sel = -1;
    }
    if (fm_scroll > fm_n - 1) fm_scroll = maxi(0, fm_n - 1);
}
static int fm_rows(void) {
    int wi = find_win(APP_FILES);
    if (wi < 0) return 10;
    FmL L; fm_layout(&wins[wi], &L);
    return L.rows;
}
static void fm_scroll_clamp(void) { fm_scroll = clampi(fm_scroll, 0, maxi(0, fm_n - fm_rows())); }
static void fm_ensure_visible(void) {
    int rows = fm_rows();
    for (int i = 0; i < fm_n; i++) if (fm_list[i] == fm_sel) {
        if (i < fm_scroll) fm_scroll = i;
        if (i >= fm_scroll + rows) fm_scroll = i - rows + 1;
    }
    fm_scroll_clamp();
}
static void fm_go(int idx) { fm_cwd = idx; fm_sel = -1; fm_scroll = 0; fm_refresh(); }
static void fm_up(void) {
    if (fm_cwd <= 0) return;
    int from = fm_cwd;
    fm_go(fsn[fm_cwd].parent);
    fm_sel = from; fm_ensure_visible();
}
static void fm_open_node(int idx) {
    if (fsn[idx].dir) fm_go(idx);
    else if (pkg_is_source_name(fsn[idx].name)) pkg_prepare_install(idx);          /* .powor / .appd */
    else if (pkg_is_installed_name(fsn[idx].name)) { int e = pkg_launch_node(idx); if (e) pkg_error_dialog(e); }
    else if (is_image_name(fsn[idx].name)) open_image_file(idx);
    else open_notepad_file(idx);
}
static void fm_do_newfile(void) {
    char nm[40]; fs_unique(fm_cwd, "untitled", ".txt", nm);
    dlg_text(DK_NEWFILE, "New File", "Name for the new file:", nm, 0);
}
static void fm_do_newdir(void) {
    char nm[40]; fs_unique(fm_cwd, "New Folder", "", nm);
    dlg_text(DK_NEWDIR, "New Folder", "Name for the new folder:", nm, 0);
}
static void fm_do_rename(void) {
    if (fm_sel < 0) return;
    if (pkg_is_protected(fm_sel)) { dlg_message("This is a system folder and can't be renamed."); return; }
    dlg_text(DK_RENAME, "Rename", "New name:", fsn[fm_sel].name, fm_sel);
}
static void fm_do_delete(void) {
    if (fm_sel < 0) return;
    if (pkg_is_protected(fm_sel)) { dlg_message("This is a system folder and can't be deleted."); return; }
    reg_refresh(0);
    if (reg_find_node(fm_sel) >= 0) { pkg_uninstall_prompt(fm_sel); return; }       /* installed app: proper uninstall */
    char m[120], *p = m;
    p = put_str(p, "Delete \""); p = put_str(p, fsn[fm_sel].name); p = put_str(p, "\"");
    if (fsn[fm_sel].dir) p = put_str(p, " and everything in it");
    p = put_str(p, "? This can't be undone."); *p = 0;
    dlg_confirm(DK_DELETE, "Delete", m, fm_sel);
}
static void fm_draw_icon(int x, int y, const Node *n) {
    if (n->dir) {
        fill_rect(x + 1, y + 2, 6, 3, 0xdba02a);
        fill_rect(x + 1, y + 4, 14, 10, 0xffc83d);
        fill_rect(x + 1, y + 6, 14, 8, 0xffd966);
    } else if (pkg_is_source_name(n->name) || pkg_is_installed_name(n->name)) {     /* package: a little box */
        fill_rect(x + 1, y + 4, 14, 11, 0x8a5a2b);
        fill_rect(x + 2, y + 5, 12, 9, 0xd9a15b);
        fill_rect(x + 1, y + 4, 14, 3, 0xb9803e);
        fill_rect(x + 7, y + 4, 2, 11, 0xf3e3c3);
    } else if (is_image_name(n->name)) {
        fill_rect(x + 1, y + 2, 14, 12, 0xffffff);
        fill_rect(x + 2, y + 3, 12, 10, 0x6cb7ff);
        fill_rect(x + 10, y + 4, 3, 3, 0xffe066);
        fill_rect(x + 2, y + 10, 12, 3, 0x3fa66a);
    } else {
        fill_rect(x + 3, y + 1, 10, 14, 0x9aa0ad);
        fill_rect(x + 4, y + 2, 8, 12, 0xffffff);
        for (int k = 0; k < 4; k++) fill_rect(x + 5, y + 4 + k * 3, 6, 1, 0xaab0bd);
    }
}
static const char *fm_kind(const Node *n) {
    if (!n->dir && pkg_is_source_name(n->name)) return "Package";
    if (!n->dir && pkg_is_installed_name(n->name)) return "App";
    return n->dir ? "Folder" : (is_image_name(n->name) ? "Image" : (ext_is(n->name, "txt") ? "Text" : "File"));
}
static const char *fm_places[4] = { "Computer", "Desktop", "Documents", "Pictures" };

static void draw_files(Win *w) {
    FmL L; fm_layout(w, &L);
    fm_refresh();
    fm_scroll_clamp();
    fill_rect(L.cx, L.cy, L.cw, L.ch, C_WIN);
    /* toolbar */
    fill_rect(L.cx, L.cy, L.cw, 36, C_BAR);
    fill_rect(L.cx, L.cy + 35, L.cw, 1, C_LINE);
    btn(L.bx[0], L.by, 30, L.bh, "<", fm_cwd > 0);
    for (int i = 0; i < 5; i++) btn(L.bx[i + 1], L.by, fm_bw[i], L.bh, fm_bl[i], i < 2 || fm_sel >= 0);
    /* path bar */
    fill_rect(L.cx, L.cy + 36, L.cw, 24, 0xfafafc);
    fill_rect(L.cx, L.cy + 59, L.cw, 1, C_LINE);
    char path[96]; fs_path(fm_cwd, path, sizeof path);
    draw_text_fit(L.cx + 10, L.cy + 40, path, L.cw - 20, C_DIM);
    /* sidebar */
    if (L.sbw) {
        fill_rect(L.cx, L.cy + 60, L.sbw, L.ch - 60 - 22, 0xe9eaf0);
        fill_rect(L.cx + L.sbw - 1, L.cy + 60, 1, L.ch - 60 - 22, C_LINE);
        draw_text(L.cx + 10, L.cy + 68, "PLACES", 0x8a90a0);
        for (int k = 0; k < 4; k++) {
            int y = L.cy + 90 + k * 26;
            int tgt = k == 0 ? 0 : fs_lookup(0, fm_places[k]);
            int cur = tgt >= 0 && tgt == fm_cwd;
            if (cur) { rrect(L.cx + 6, y, L.sbw - 14, 22, 6, 0xcfdcf7, 0xcfdcf7, 256); }
            else if (hov_in(L.cx + 6, y, L.sbw - 14, 22)) rrect(L.cx + 6, y, L.sbw - 14, 22, 6, 0xdde0e8, 0xdde0e8, 256);
            Node tmp; memset(&tmp, 0, sizeof tmp); tmp.dir = 1;
            fm_draw_icon(L.cx + 12, y + 3, &tmp);
            draw_text_fit(L.cx + 34, y + 3, fm_places[k], L.sbw - 44, tgt >= 0 ? C_TXT : 0xa3a7b3);
        }
    }
    /* column header */
    int kx = L.lx + L.lw - 76, sx = kx - 84, showk = L.lw >= 320;
    fill_rect(L.lx, L.cy + 60, L.lw, 22, 0xeceef3);
    fill_rect(L.lx, L.cy + 81, L.lw, 1, C_LINE);
    draw_text(L.lx + 32, L.cy + 63, "Name", C_DIM);
    if (showk) { draw_text(sx, L.cy + 63, "Size", C_DIM); draw_text(kx, L.cy + 63, "Kind", C_DIM); }
    /* rows */
    fill_rect(L.lx, L.ly, L.lw, L.lh, 0xffffff);
    int namew = (showk ? sx : L.lx + L.lw - 14) - (L.lx + 32) - 8;
    for (int r = 0; r < L.rows; r++) {
        int idx = fm_scroll + r;
        if (idx >= fm_n) break;
        const Node *n = &fsn[fm_list[idx]];
        int y = L.ly + r * FM_ROW, sel = fm_list[idx] == fm_sel;
        if (sel) fill_rect(L.lx, y, L.lw, FM_ROW, C_ACC);
        else if (r & 1) fill_rect(L.lx, y, L.lw, FM_ROW, 0xf7f8fb);
        u32 tc = sel ? 0xffffff : C_TXT, dc = sel ? 0xdbe6ff : C_DIM;
        fm_draw_icon(L.lx + 10, y + 4, n);
        draw_text_fit(L.lx + 32, y + 4, n->name, namew, tc);
        if (showk) {
            char sz[20];
            if (n->dir) { sz[0] = '-'; sz[1] = 0; } else fmt_size(sz, n->size);
            draw_text(sx, y + 4, sz, dc);
            draw_text(kx, y + 4, fm_kind(n), dc);
        }
    }
    if (!fm_n) draw_text(L.lx + (L.lw - 20 * 8) / 2, L.ly + 30, "This folder is empty", 0xa3a7b3);
    if (fm_n > L.rows) {                                   /* scroll bar */
        int th = maxi(20, L.lh * L.rows / fm_n);
        int ty = L.ly + (L.lh - th) * fm_scroll / maxi(1, fm_n - L.rows);
        rrect(L.lx + L.lw - 8, ty, 5, th, 2, 0xb7bbc7, 0xb7bbc7, 256);
    }
    /* status bar */
    int sy = L.cy + L.ch - 22;
    fill_rect(L.cx, sy, L.cw, 22, C_BAR);
    fill_rect(L.cx, sy, L.cw, 1, C_LINE);
    char sb[96], *p = sb;
    p = put_u32(p, fm_n); p = put_str(p, fm_n == 1 ? " item" : " items");
    if (fm_sel >= 0) { p = put_str(p, "   |   "); p = put_str(p, fsn[fm_sel].name); }
    *p = 0;
    char di[48]; fs_disk_info(di, 1);
    int dw = slen(di) * 8;
    if (L.cw > dw + 200) { draw_text(L.cx + L.cw - dw - 10, sy + 3, di, disk_err ? 0xc9423a : C_DIM); }
    draw_text_fit(L.cx + 10, sy + 3, sb, L.cw - dw - 30 > 0 ? L.cw - dw - 30 : L.cw - 20, C_DIM);
}
static void files_click(Win *w) {
    FmL L; fm_layout(w, &L);
    if (my >= L.by && my < L.by + L.bh) {
        if (inrect(mx, my, L.bx[0], L.by, 30, L.bh)) { fm_up(); return; }
        for (int i = 0; i < 5; i++) if (inrect(mx, my, L.bx[i + 1], L.by, fm_bw[i], L.bh)) {
            if (i == 0) fm_do_newfile();
            else if (i == 1) fm_do_newdir();
            else if (fm_sel >= 0) { if (i == 2) fm_do_rename(); else if (i == 3) fm_do_delete(); else fm_open_node(fm_sel); }
            return;
        }
        return;
    }
    if (L.sbw && mx >= L.cx && mx < L.cx + L.sbw && my >= L.cy + 90) {
        for (int k = 0; k < 4; k++) if (inrect(mx, my, L.cx + 6, L.cy + 90 + k * 26, L.sbw - 14, 22)) {
            int tgt = k == 0 ? 0 : fs_lookup(0, fm_places[k]);
            if (tgt >= 0) fm_go(tgt);
        }
        return;
    }
    if (inrect(mx, my, L.lx, L.ly, L.lw, L.lh)) {
        int idx = fm_scroll + (my - L.ly) / FM_ROW;
        if (idx < fm_n) {
            int node = fm_list[idx];
            u32 t = now_ms();
            if (node == fm_click_node && t - fm_click_t < 450) { fm_click_node = -1; fm_open_node(node); return; }
            fm_sel = node; fm_click_node = node; fm_click_t = t;
        } else { fm_sel = -1; fm_click_node = -1; }
    }
}
static void files_key(int k);       /* needs key codes, defined in shell.h */

/* =================================================================== NOTEPAD */
#define MAXTXT 65536
static char np_buf[MAXTXT];
static int np_len, np_cur, np_scroll, np_follow = 1;
static int np_file = -1, np_mod, np_ro;
static int np_cols = 60, np_rows = 20, np_tx, np_ty;
static const char *np_msg = "";

typedef struct { int cx, cy, cw, ch, tx, ty, tw, th; } NpL;
static void np_layout(const Win *w, NpL *L) {
    content_rect(w, &L->cx, &L->cy, &L->cw, &L->ch);
    L->tx = L->cx + 6; L->ty = L->cy + 40;
    L->tw = L->cw - 12; L->th = L->ch - 40 - 28 - 6;
}
static void np_rowcol(int idx, int *rr, int *cc) {
    int r = 0, c = 0;
    for (int i = 0; i < idx; i++) {
        if (np_buf[i] == '\n') { r++; c = 0; }
        else { if (c >= np_cols) { r++; c = 0; } c++; }
    }
    if (c >= np_cols && (idx >= np_len || np_buf[idx] != '\n')) { r++; c = 0; }
    *rr = r; *cc = c;
}
static int np_index_at(int tr, int tc) {
    int r = 0, c = 0, best = -1;
    if (tc < 0) tc = 0;
    for (int i = 0; i <= np_len; i++) {
        int pr = r, pc = c;
        if (c >= np_cols && (i >= np_len || np_buf[i] != '\n')) { pr = r + 1; pc = 0; }
        if (pr == tr && pc <= tc) best = i;
        if (pr > tr) break;
        if (i < np_len) {
            if (np_buf[i] == '\n') { r++; c = 0; }
            else { r = pr; c = pc + 1; }
        }
    }
    if (best < 0) return tr < 0 ? 0 : np_len;
    return best;
}
static void np_clear(void);
static void np_touch(void) { np_mod = 1; np_follow = 1; np_msg = ""; }
static void np_insert(char ch) {
    if (np_len >= MAXTXT - 1) return;
    memmove(np_buf + np_cur + 1, np_buf + np_cur, np_len - np_cur);
    np_buf[np_cur++] = ch; np_len++; np_touch();
}
static void np_load(int idx) {
    Node *n = &fsn[idx];
    int len = 0, bad = 0; u32 i = 0;
    const u8 *src = fs_data(idx);
    if (n->size && !src) { np_clear(); np_msg = "Could not read file from disk"; return; }
    for (; i < n->size && len < MAXTXT - 1; i++) {
        u8 c = src[i];
        if (c == '\r') continue;
        if (c == '\t') { for (int k = 0; k < 4 && len < MAXTXT - 1; k++) np_buf[len++] = ' '; }
        else if (c == '\n' || (c >= 32 && c < 127)) np_buf[len++] = (char)c;
        else { np_buf[len++] = '?'; bad = 1; }
    }
    np_len = len; np_cur = 0; np_scroll = 0; np_follow = 1;
    np_file = idx; np_mod = 0;
    np_ro = bad || i < n->size;
    np_msg = i < n->size ? "Truncated to 64 KB - read only" : (bad ? "Binary data - read only (use Save As)" : "");
}
static void np_clear(void) { np_len = np_cur = np_scroll = 0; np_file = -1; np_mod = 0; np_ro = 0; np_follow = 1; np_msg = "New file"; }
static void np_save_as(void) {
    char nm[40], m[120], *p = m, path[64];
    if (np_file >= 0 && fsn[np_file].used) scpy(nm, fsn[np_file].name, sizeof nm);
    else fs_unique(fm_cwd, "untitled", ".txt", nm);
    fs_path(fm_cwd, path, sizeof path);
    p = put_str(p, "Save in "); p = put_str(p, path); p = put_str(p, " as:"); *p = 0;
    dlg_text(DK_SAVEAS, "Save As", m, nm, 0);
}
static void np_save(void) {
    if (np_file < 0 || !fsn[np_file].used) { np_file = -1; np_save_as(); return; }
    if (np_ro) { np_msg = "Read-only file - use Save As"; return; }
    int we = fs_write(np_file, (const u8 *)np_buf, (u32)np_len);
    if (we < 0) { np_msg = we == FS_ERR_IO ? "Disk write failed!" : we == FS_ERR_NOMEM ? "Out of memory - not saved!" : "No space left - not saved!"; return; }
    np_mod = 0; np_msg = "Saved"; fm_refresh();
}
static void np_pending(int pa, int arg) {
    if (pa == PA_OPEN) np_load(arg);
    else if (pa == PA_NEW) np_clear();
}
static void np_request(int pa, int arg) {
    if (np_mod) {
        char m[120], *p = m;
        p = put_str(p, "Discard unsaved changes to \""); p = put_str(p, np_file >= 0 ? fsn[np_file].name : "Untitled");
        p = put_str(p, "\"?"); *p = 0;
        dlg_confirm(DK_DISCARD, "Unsaved changes", m, 0);
        dlg.pa = pa; dlg.pa_arg = arg;
    } else np_pending(pa, arg);
}
static void open_notepad_file(int idx) { open_app(APP_NOTEPAD); np_request(PA_OPEN, idx); }

static void draw_notepad(Win *w) {
    NpL L; np_layout(w, &L);
    fill_rect(L.cx, L.cy, L.cw, L.ch, C_WIN);
    fill_rect(L.cx, L.cy, L.cw, 36, C_BAR);
    fill_rect(L.cx, L.cy + 35, L.cw, 1, C_LINE);
    btn(L.cx + 8,   L.cy + 6, 52, 24, "New", 1);
    btn(L.cx + 66,  L.cy + 6, 56, 24, "Open", 1);
    btn(L.cx + 128, L.cy + 6, 56, 24, "Save", 1);
    btn(L.cx + 190, L.cy + 6, 80, 24, "Save As", 1);
    if (L.cw > 520) {
        char path[64] = "Untitled";
        if (np_file >= 0 && fsn[np_file].used) fs_path(np_file, path, sizeof path);
        draw_text_fit(L.cx + 284, L.cy + 10, path, L.cw - 294, C_DIM);
    }
    np_tx = L.tx; np_ty = L.ty;
    np_cols = maxi(4, (L.tw - 8) / 8); np_rows = maxi(1, (L.th - 8) / 16);
    frame_rect(L.tx - 1, L.ty - 1, L.tw + 2, L.th + 2, 0xa9adb8);
    fill_rect(L.tx, L.ty, L.tw, L.th, 0xffffff);

    int cr, cc, total, tc_;
    np_rowcol(np_cur, &cr, &cc);
    np_rowcol(np_len, &total, &tc_);
    if (np_follow) {
        if (cr < np_scroll) np_scroll = cr;
        if (cr >= np_scroll + np_rows) np_scroll = cr - np_rows + 1;
        np_follow = 0;
    }
    np_scroll = clampi(np_scroll, 0, maxi(0, total));
    int r = 0, c = 0;
    for (int i = 0; i < np_len; i++) {
        char k = np_buf[i];
        if (k == '\n') { r++; c = 0; continue; }
        if (c >= np_cols) { r++; c = 0; }
        if (r >= np_scroll + np_rows) break;
        if (r >= np_scroll) draw_char(L.tx + 4 + c * 8, L.ty + 4 + (r - np_scroll) * 16, k, 0x15181f);
        c++;
    }
    if (cr >= np_scroll && cr < np_scroll + np_rows)
        fill_rect(L.tx + 4 + cc * 8, L.ty + 4 + (cr - np_scroll) * 16, 2, 16, C_ACC);

    int sy = L.cy + L.ch - 28;
    fill_rect(L.cx, sy, L.cw, 28, C_BAR);
    fill_rect(L.cx, sy, L.cw, 1, C_LINE);
    char sb[120], *p = sb;
    p = put_str(p, "Ln "); p = put_u32(p, cr + 1);
    p = put_str(p, ", Col "); p = put_u32(p, cc + 1);
    p = put_str(p, "   "); p = put_u32(p, np_len);
    p = put_str(p, " chars"); if (np_mod) p = put_str(p, "   (edited)");
    if (*np_msg) { p = put_str(p, "   "); p = put_str(p, np_msg); }
    *p = 0;
    draw_text_fit(L.cx + 10, sy + 6, sb, L.cw - 20, C_DIM);
}
static void notepad_click(Win *w) {
    NpL L; np_layout(w, &L);
    if (inrect(mx, my, L.cx + 8, L.cy + 6, 52, 24)) np_request(PA_NEW, 0);
    else if (inrect(mx, my, L.cx + 66, L.cy + 6, 56, 24)) { open_app(APP_FILES); np_msg = "Double-click a file in Files to open it"; }
    else if (inrect(mx, my, L.cx + 128, L.cy + 6, 56, 24)) np_save();
    else if (inrect(mx, my, L.cx + 190, L.cy + 6, 80, 24)) np_save_as();
    else if (inrect(mx, my, L.tx, L.ty, L.tw, L.th)) {
        int r = (my - L.ty - 4) / 16 + np_scroll, c = (mx - L.tx - 4 + 4) / 8;
        if (my - L.ty - 4 < 0) r = np_scroll;
        np_cur = np_index_at(r, c); np_follow = 1;
    }
}

/* ===================================================================== PHOTOS */
static int iv_node = -1, iv_pos, iv_cnt;
static Img iv_img;
static const char *iv_err = "";
static u32 *iv_cache; static int iv_cw, iv_ch;

static void iv_drop_cache(void) { if (iv_cache) kfree(iv_cache); iv_cache = 0; iv_cw = iv_ch = 0; }
static void iv_release(void) { iv_drop_cache(); img_free(&iv_img); }
static int iv_siblings(int *out) {                 /* sorted image files in the same folder */
    static int tmp[MAXNODES];
    if (iv_node < 0 || !fsn[iv_node].used) return 0;
    int n = fs_children(fsn[iv_node].parent, tmp), m = 0;
    for (int i = 0; i < n; i++) if (!fsn[tmp[i]].dir && is_image_name(fsn[tmp[i]].name)) out[m++] = tmp[i];
    return m;
}
static void iv_scan(void) {
    static int list[MAXNODES];
    iv_cnt = iv_siblings(list); iv_pos = 0;
    for (int i = 0; i < iv_cnt; i++) if (list[i] == iv_node) iv_pos = i;
}
static void iv_load(int idx) {
    iv_release();
    iv_node = idx; iv_err = "";
    const char *e = "Could not open image";
    Node *n = &fsn[idx];
    fs_pin = idx;                                   /* keep the source bytes while decoding */
    const u8 *src = n->size ? fs_data(idx) : 0;
    if (!n->size) iv_err = "File is empty";
    else if (!src) iv_err = "Could not read file from disk";
    else if (!image_decode(src, n->size, &iv_img, &e)) { iv_err = e; iv_img.px = 0; }
    fs_pin = -1;
    iv_scan();
}
static void iv_step(int dir) {
    static int list[MAXNODES];
    int n = iv_siblings(list);
    if (n < 2) return;
    int pos = 0;
    for (int i = 0; i < n; i++) if (list[i] == iv_node) pos = i;
    iv_load(list[(pos + dir + n) % n]);
}
static void open_image_file(int idx) { open_app(APP_IMAGES); iv_load(idx); }

static void draw_images(Win *w) {
    int cx, cy, cw, ch; content_rect(w, &cx, &cy, &cw, &ch);
    fill_rect(cx, cy, cw, ch, 0x14161c);
    fill_rect(cx, cy, cw, 36, C_BAR);
    fill_rect(cx, cy + 35, cw, 1, C_LINE);
    int have = iv_node >= 0 && fsn[iv_node].used && iv_img.px;
    btn(cx + 8,  cy + 6, 30, 24, "<", iv_cnt > 1);
    btn(cx + 44, cy + 6, 30, 24, ">", iv_cnt > 1);
    if (cw >= 520) btn(cx + cw - 144, cy + 6, 136, 24, "Set wallpaper", have);
    else           btn(cx + cw - 38,  cy + 6, 30, 24, "W", have);
    char cap[96], *p = cap;
    if (have) {
        p = put_str(p, fsn[iv_node].name); p = put_str(p, "  "); p = put_u32(p, iv_img.w); p = put_str(p, "x"); p = put_u32(p, iv_img.h);
        if (iv_cnt > 1) { p = put_str(p, "  ("); p = put_u32(p, iv_pos + 1); p = put_str(p, "/"); p = put_u32(p, iv_cnt); p = put_str(p, ")"); }
    } else p = put_str(p, "No picture");
    *p = 0;
    draw_text_fit(cx + 84, cy + 10, cap, cw - 84 - (cw >= 520 ? 152 : 46), C_TXT);

    int ax = cx + 6, ay = cy + 42, aw = cw - 12, ah = ch - 48;
    if (aw < 4 || ah < 4) return;
    if (!have) {
        const char *m = (iv_node >= 0 && *iv_err) ? iv_err : "Open a picture from Files";
        draw_text(cx + (cw - slen(m) * 8) / 2, cy + ch / 2, m, 0xa3a7b3);
        return;
    }
    int dw, dh; fit_size(iv_img.w, iv_img.h, aw, ah, &dw, &dh);
    int dx = ax + (aw - dw) / 2, dy = ay + (ah - dh) / 2;
    if (rsz >= 0) { blit_img_nearest(&iv_img, dx, dy, dw, dh); return; }   /* cheap while resizing */
    if (!iv_cache || iv_cw != dw || iv_ch != dh) {
        iv_drop_cache();
        iv_cache = kmalloc((u32)dw * dh * 4);
        if (iv_cache) { scale_box(&iv_img, 0, 0, iv_img.w, iv_img.h, iv_cache, dw, dh, dw); iv_cw = dw; iv_ch = dh; }
    }
    if (iv_cache) blit_buf(iv_cache, dw, dh, dx, dy);
    else blit_img_nearest(&iv_img, dx, dy, dw, dh);
}
static void images_click(Win *w) {
    int cx, cy, cw, ch; content_rect(w, &cx, &cy, &cw, &ch);
    int have = iv_node >= 0 && fsn[iv_node].used && iv_img.px;
    if (inrect(mx, my, cx + 8, cy + 6, 30, 24)) iv_step(-1);
    else if (inrect(mx, my, cx + 44, cy + 6, 30, 24)) iv_step(1);
    else if (have && ((cw >= 520 && inrect(mx, my, cx + cw - 144, cy + 6, 136, 24)) ||
                      (cw < 520 && inrect(mx, my, cx + cw - 38, cy + 6, 30, 24)))) {
        if (!wp_set(&iv_img)) iv_err = "Not enough memory for wallpaper";
    }
}

/* ===================================================================== DISPLAY */
static const int res_list[][2] = {
    {800,600},{1024,768},{1280,720},{1280,800},{1280,1024},{1366,768},{1440,900},{1600,900},{1920,1080},{1920,1200} };
#define NRES ((int)(sizeof res_list / sizeof res_list[0]))
static int dp_sel = -1;
static const char *dp_msg = "";

static int res_usable(int i) { return bga_ver && res_list[i][0] <= MAXW && res_list[i][1] <= MAXH; }
static void dp_apply(void) {
    if (dp_sel < 0) { dp_msg = "Pick a resolution first"; return; }
    int nw = res_list[dp_sel][0], nh = res_list[dp_sel][1];
    if (nw == W && nh == H) { dp_msg = "Already using this resolution"; return; }
    dp_msg = change_resolution(nw, nh) ? "Resolution changed" : "This display can't do that mode";
}
static void draw_display(Win *w) {
    int cx, cy, cw, ch; content_rect(w, &cx, &cy, &cw, &ch);
    fill_rect(cx, cy, cw, ch, C_WIN);
    draw_text_bold(cx + 20, cy + 16, "Screen resolution", C_TXT);
    char cur[48], *p = cur;
    p = put_str(p, "Current: "); p = put_u32(p, W); p = put_str(p, " x "); p = put_u32(p, H); *p = 0;
    draw_text(cx + 20, cy + 40, cur, C_DIM);
    int colw = (cw - 40) / 2;
    for (int i = 0; i < NRES; i++) {
        int x = cx + 20 + (i & 1) * colw, y = cy + 68 + (i >> 1) * 30, ok = res_usable(i);
        int hov = ok && hov_in(x, y, colw - 8, 26);
        if (hov) rrect(x - 4, y - 2, colw - 8, 26, 6, 0xe3e6ee, 0xe3e6ee, 256);
        fill_circle(x + 8, y + 10, 8, ok ? 0x8a8f98 : 0xc3c6ce);
        fill_circle(x + 8, y + 10, 7, 0xffffff);
        if (i == dp_sel) fill_circle(x + 8, y + 10, 4, C_ACC);
        char t[24], *q = t;
        q = put_u32(q, res_list[i][0]); q = put_str(q, " x "); q = put_u32(q, res_list[i][1]); *q = 0;
        draw_text(x + 24, y + 2, t, ok ? C_TXT : 0xa3a7b3);
        if (res_list[i][0] == W && res_list[i][1] == H) draw_text(x + 24 + 12 * 8, y + 2, "*", C_ACC);
    }
    int ny = cy + 68 + 5 * 30 + 6;
    if (!bga_ver) {
        draw_text(cx + 20, ny, "Live switching needs a QEMU / Bochs /", C_DIM);
        draw_text(cx + 20, ny + 18, "VirtualBox VGA adapter. On this machine", C_DIM);
        draw_text(cx + 20, ny + 36, "pick a mode from the GRUB boot menu.", C_DIM);
    } else if (*dp_msg) draw_text(cx + 20, ny, dp_msg, C_DIM);
    btn(cx + cw - 110, cy + ch - 40, 90, 28, "Apply", bga_ver && dp_sel >= 0);
}
static void display_click(Win *w) {
    int cx, cy, cw, ch; content_rect(w, &cx, &cy, &cw, &ch);
    int colw = (cw - 40) / 2;
    for (int i = 0; i < NRES; i++) {
        int x = cx + 20 + (i & 1) * colw, y = cy + 68 + (i >> 1) * 30;
        if (res_usable(i) && inrect(mx, my, x - 4, y - 2, colw - 8, 26)) { dp_sel = i; dp_msg = ""; }
    }
    if (bga_ver && dp_sel >= 0 && inrect(mx, my, cx + cw - 110, cy + ch - 40, 90, 28)) dp_apply();
}

/* ===================================================================== ABOUT */
static int fs_count_nodes(void) { int n = 0; for (int i = 1; i < MAXNODES; i++) n += fsn[i].used; return n; }
static void draw_about(Win *w) {
    int cx, cy, cw, ch; content_rect(w, &cx, &cy, &cw, &ch);
    fill_rect(cx, cy, cw, ch, C_WIN);
    fill_circle(cx + 56, cy + 62, 36, C_ACC);
    fill_circle(cx + 56, cy + 62, 28, 0x1d3f8f);
    draw_text(cx + 52, cy + 54, "N", 0xffffff);
    draw_text_bold(cx + 112, cy + 24, "NovaOS 0.2", C_TXT);
    draw_text(cx + 112, cy + 48, "x86 | Multiboot | 32-bit", C_DIM);
    char l[64], *p = l;
    p = put_str(p, "Screen: "); p = put_u32(p, W); p = put_str(p, " x "); p = put_u32(p, H); *p = 0;
    draw_text(cx + 112, cy + 68, l, C_DIM);
    p = l; p = put_str(p, "Memory in use: "); p = put_u32(p, heap_used_bytes / 1024); p = put_str(p, " KB"); *p = 0;
    draw_text(cx + 112, cy + 88, l, C_DIM);
    p = l; p = put_str(p, "Files & folders: "); p = put_u32(p, fs_count_nodes()); *p = 0;
    draw_text(cx + 112, cy + 108, l, C_DIM);
    char di[64]; fs_disk_info(di, 0);
    draw_text_fit(cx + 20, cy + 134, di, cw - 30, disk_err ? 0xc9423a : C_TXT);
    draw_text(cx + 20, cy + 162, "Disk file system, dock, live resolution,", C_TXT);
    draw_text(cx + 20, cy + 180, "BMP / PNG / JPEG support.", C_TXT);
    draw_text(cx + 20, cy + 212, "Built from scratch in C.", C_DIM);
}


/* ============================================================ INSTALLED APPS
 * Application types implemented by the kernel (see pkg.h / docs/NOVAPKG_FORMAT.md):
 *   textview - a window that displays the package's DATA text
 *   builtin  - a shortcut to one of the built-in apps
 * Package bytes are only ever treated as text/data, never executed.           */
#define TV_MAX 4
typedef struct {
    int used; char id[PKG_ID_MAX + 1], name[PKG_NAME_MAX + 1], title[PKG_TITLE_MAX + 1], ver[PKG_VER_MAX + 1];
    u32 accent; int wrap; char *text; int len, scroll;
} TextView;
static TextView tv[TV_MAX];

static void tv_release(int inst) {
    if (inst < 0 || inst >= TV_MAX) return;
    if (tv[inst].text) kfree(tv[inst].text);
    memset(&tv[inst], 0, sizeof tv[inst]);
}
static int tv_win(int inst) {
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].type == APP_TEXTVIEW && wins[i].inst == inst) return i;
    return -1;
}
static int launch_textview(const PkgInfo *pi, const u8 *pkg) {
    int slot = -1;
    for (int i = 0; i < TV_MAX; i++)
        if (tv[i].used && !memcmp(tv[i].id, pi->id, (u32)slen(pi->id) + 1)) {         /* already open: just show it */
            int w = tv_win(i);
            if (w >= 0) { wins[w].min = 0; focus_win(w); return PKG_OK; }
        }
    for (int i = 0; i < TV_MAX && slot < 0; i++) if (!tv[i].used) slot = i;
    if (slot < 0) return PKG_E_BUSY;
    const u8 *src = pkg + pi->data_off;
    u32 need = 0;                                                      /* tabs become 4 spaces, '\r' is dropped */
    for (u32 i = 0; i < pi->data_len; i++) need += src[i] == '\t' ? 4 : (src[i] == '\r' ? 0 : 1);
    char *t = kmalloc(need + 1);
    if (!t) return FS_ERR_NOMEM;
    u32 n = 0;
    for (u32 i = 0; i < pi->data_len; i++) {
        if (src[i] == '\t') { for (int k = 0; k < 4; k++) t[n++] = ' '; }
        else if (src[i] != '\r') t[n++] = (char)src[i];
    }
    t[n] = 0;
    TextView *v = &tv[slot];
    memset(v, 0, sizeof *v);
    v->used = 1; v->text = t; v->len = (int)n; v->wrap = pi->wrap; v->accent = pi->accent;
    scpy(v->id, pi->id, sizeof v->id); scpy(v->name, pi->name, sizeof v->name);
    scpy(v->title, pi->title[0] ? pi->title : pi->name, sizeof v->title); scpy(v->ver, pi->version, sizeof v->ver);
    if (open_win_inst(APP_TEXTVIEW, slot) < 0) { tv_release(slot); return PKG_E_BUSY; }
    return PKG_OK;
}
static int launch_builtin(const PkgInfo *pi, const u8 *pkg) {
    (void)pkg;
    if (pi->target < 0 || pi->target > APP_ABOUT) return PKG_E_UNSUPPORTED;     /* target index == APP_* id */
    open_app(pi->target);
    return PKG_OK;
}
static void tv_close_app(const char *id) {                                      /* uninstall: close its windows */
    for (int i = 0; i < TV_MAX; i++)
        if (tv[i].used && !memcmp(tv[i].id, id, (u32)slen(id) + 1)) { int w = tv_win(i); if (w >= 0) destroy_win(w); else tv_release(i); }
}
static void pkg_ui_init(void) {
    pkg_launcher[PKG_T_TEXTVIEW] = launch_textview;
    pkg_launcher[PKG_T_BUILTIN]  = launch_builtin;
    pkg_close_hook = tv_close_app;
}

/* one display line starting at p: returns its length, *next = start of the following line */
static int tv_line(const TextView *t, int p, int cols, int *next) {
    int n = t->len, e = p;
    if (!t->wrap) {
        while (e < n && t->text[e] != '\n') e++;
        *next = e < n ? e + 1 : e;
        return e - p;
    }
    while (e < n && t->text[e] != '\n' && e - p < cols) e++;
    if (e < n && t->text[e] != '\n') {                                   /* cut by width: prefer a word boundary */
        int b = e;
        while (b > p && t->text[b] != ' ') b--;
        if (b > p) { *next = b + 1; return b - p; }
        *next = e; return e - p;
    }
    *next = e < n ? e + 1 : e;
    return e - p;
}
static int tv_total_lines(const TextView *t, int cols) {
    int p = 0, c = 0, nx;
    while (p < t->len) { tv_line(t, p, cols, &nx); if (nx <= p) nx = p + 1; p = nx; c++; }
    return c;
}
static void tv_geometry(const Win *w, int *tx, int *ty, int *cols, int *rows, int *cw, int *ch, int *cx, int *cy) {
    content_rect(w, cx, cy, cw, ch);
    *tx = *cx + 18; *ty = *cy + 10;
    *cols = maxi(4, (*cw - 18 - 22) / 8); *rows = maxi(1, (*ch - 10 - 30) / 16);
}
static void draw_textview(Win *w) {
    int tx, ty, cols, rows, cw, ch, cx, cy; tv_geometry(w, &tx, &ty, &cols, &rows, &cw, &ch, &cx, &cy);
    TextView *t = &tv[w->inst];
    fill_rect(cx, cy, cw, ch, 0xffffff);
    if (!t->used) return;
    fill_rect(cx, cy, 6, ch, t->accent);                                  /* accent stripe from the manifest */
    int total = tv_total_lines(t, cols);
    t->scroll = clampi(t->scroll, 0, maxi(0, total - rows));
    int p = 0, ln = 0, nx;
    while (p < t->len && ln < t->scroll) { tv_line(t, p, cols, &nx); if (nx <= p) nx = p + 1; p = nx; ln++; }
    for (int r = 0; r < rows && p < t->len; r++) {
        int l = tv_line(t, p, cols, &nx);
        draw_text_n(tx, ty + r * 16, t->text + p, mini(l, cols), 0x1c1f27);
        if (nx <= p) nx = p + 1;
        p = nx;
    }
    if (total > rows) {                                                   /* scroll bar */
        int th = maxi(20, (ch - 30) * rows / total), tyb = cy + ((ch - 30) - th) * t->scroll / maxi(1, total - rows);
        rrect(cx + cw - 9, tyb, 5, th, 2, 0xb7bbc7, 0xb7bbc7, 256);
    }
    int sy = cy + ch - 22;
    fill_rect(cx, sy, cw, 22, C_BAR); fill_rect(cx, sy, cw, 1, C_LINE);
    char sb[96], *q = sb;
    q = put_str(q, t->name); q = put_str(q, " v"); q = put_str(q, t->ver);
    q = put_str(q, "   Lines "); q = put_u32(q, total ? t->scroll + 1 : 0); q = put_str(q, "-"); q = put_u32(q, mini(total, t->scroll + rows));
    q = put_str(q, " of "); q = put_u32(q, total); *q = 0;
    draw_text_fit(cx + 12, sy + 3, sb, cw - 24, C_DIM);
}
static void tv_scroll(Win *w, int delta, int absolute) {                    /* absolute: -1 = top, 1 = bottom */
    int tx, ty, cols, rows, cw, ch, cx, cy; tv_geometry(w, &tx, &ty, &cols, &rows, &cw, &ch, &cx, &cy);
    TextView *t = &tv[w->inst];
    if (!t->used) return;
    int total = tv_total_lines(t, cols), mx_ = maxi(0, total - rows);
    t->scroll = absolute < 0 ? 0 : absolute > 0 ? mx_ : clampi(t->scroll + delta, 0, mx_);
}

/* ---- install / uninstall dialogs ---- */
static void pkg_error_dialog(int err) {
    char m[120], *p = m;
    p = put_str(p, pkg_err_text(err));
    if (pkg_detail[0] && err <= PKG_E_SIZE) { p = put_str(p, " ["); p = put_str(p, pkg_detail); p = put_str(p, "]"); }
    *p = 0;
    if (slen(m) > 94) m[94] = 0;
    dlg_message(m);
}
static void pkg_prepare_install(int node) {
    PkgInfo pi; const u8 *d;
    int e = pkg_load(node, &pi, &d);
    fs_pin = -1;
    if (e) { pkg_error_dialog(e); return; }
    reg_refresh(0);
    if (reg_find_id(pi.id) >= 0) { e = pkg_fail(PKG_E_DUP, pi.id); pkg_error_dialog(e); return; }
    char m[120], sz[20], *p = m;
    fmt_size(sz, pi.size);
    p = put_str(p, "Install \""); p = put_str(p, pi.name); p = put_str(p, "\" "); p = put_str(p, pi.version);
    if (pi.author[0]) { p = put_str(p, " by "); p = put_str(p, pi.author); }
    p = put_str(p, " ("); p = put_str(p, pkg_type_name[pi.type]); p = put_str(p, ", "); p = put_str(p, sz); p = put_str(p, ")?");
    *p = 0;
    dlg_confirm(DK_INSTALL, "Install application", m, node);
}
static void pkg_uninstall_prompt(int node) {
    int r = reg_find_node(node);
    if (r < 0) return;
    char m[120], *p = m;
    p = put_str(p, "Uninstall \""); p = put_str(p, reg[r].info.name); p = put_str(p, "\"? Its package file will be deleted.");
    *p = 0;
    dlg_confirm(DK_UNINSTALL, "Uninstall application", m, node);
}

/* ------------------------------------------------------- dialog results */
static const char *fs_err_text(int e) {
    return e == FS_ERR_NAME ? "That name isn't allowed (avoid / and \\)." :
           e == FS_ERR_EXISTS ? "An item with that name already exists." :
           e == FS_ERR_IO ? "Disk error - the change could not be written." :
           e == FS_ERR_NOMEM ? "Out of memory." : "No space left on the file system.";
}
static void fix_after_delete(void) {
    if (np_file >= 0 && !fsn[np_file].used) { np_file = -1; np_mod = 1; np_msg = "File was deleted - Save As to keep it"; }
    if (iv_node >= 0 && !fsn[iv_node].used) { iv_release(); iv_node = -1; iv_cnt = 0; iv_err = ""; }
    fm_click_node = -1;
    reg_refresh(0);
    fm_refresh();
}
static void dlg_ok(void) {
    int kind = dlg.kind, target = dlg.target, pa = dlg.pa, pa_arg = dlg.pa_arg;
    char name[NAME_MAX + 2]; scpy(name, dlg.buf, sizeof name);
    dlg.kind = DK_NONE;
    int r;
    switch (kind) {
    case DK_NEWFILE: case DK_NEWDIR:
        r = fs_new(fm_cwd, name, kind == DK_NEWDIR);
        if (r < 0) { dlg_message(fs_err_text(r)); break; }
        fm_refresh(); fm_sel = r; fm_ensure_visible();
        break;
    case DK_RENAME:
        r = fs_rename(target, name);
        if (r < 0) dlg_message(fs_err_text(r)); else fm_refresh();
        break;
    case DK_SAVEAS:
        r = fs_new(fm_cwd, name, 0);
        if (r < 0) { dlg_message(fs_err_text(r)); break; }
        { int we = fs_write(r, (const u8 *)np_buf, (u32)np_len);
          if (we < 0) { fs_delete(r); dlg_message(fs_err_text(we)); break; } }
        np_file = r; np_mod = 0; np_ro = 0; np_msg = "Saved";
        fm_refresh(); fm_sel = r; fm_ensure_visible();
        break;
    case DK_DELETE:
        fs_delete(target); fm_sel = -1; fix_after_delete();
        break;
    case DK_INSTALL: {
        PkgInfo pi;
        r = pkg_install(target, &pi);
        if (r) pkg_error_dialog(r);
        else {
            char m[120], *p = m;
            p = put_str(p, "Installed ""); p = put_str(p, pi.name); p = put_str(p, "". Find it in the Start menu.");
            *p = 0; dlg_message(m);
        }
        fm_refresh();
        break; }
    case DK_UNINSTALL:
        r = pkg_uninstall(target);
        if (r) { if (r <= PKG_E_SIZE || r > FS_ERR_NOMEM) pkg_error_dialog(r); else dlg_message("Disk error: the app was removed but this may not be saved. It might reappear after a restart."); }
        fm_sel = -1; fix_after_delete();
        break;
    case DK_FORMAT:
        if (fs_attach()) dlg_message("Formatting failed - the disk is not usable. Running from RAM.");
        else dlg_message("Disk formatted. Your files are now saved to disk.");
        fm_refresh();
        break;
    case DK_DISCARD:
        if (pa == PA_CLOSE) { int wi = find_win(APP_NOTEPAD); np_mod = 0; if (wi >= 0) destroy_win(wi); }
        else { np_mod = 0; np_pending(pa, pa_arg); }
        break;
    default: break;
    }
}
#endif

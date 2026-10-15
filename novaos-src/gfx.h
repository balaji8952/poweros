/* gfx.h - framebuffer, drawing primitives, text, image scaling.
 * The screen size is dynamic: W/H can change at runtime (see apply_mode in
 * ui.h).  Buffers are sized for the maximum supported mode and indexed with
 * the current width as stride.                                             */
#ifndef NOVA_GFX_H
#define NOVA_GFX_H
#include "core.h"
#include "font.h"

#define MAXW 1920
#define MAXH 1200
static u32 back[MAXW * MAXH];     /* off-screen compose buffer */
static u32 wall[MAXW * MAXH];     /* cached wallpaper */
static u32 *dst = back;           /* current drawing target */
static int  W = 1024, H = 768;
static u32 *fbp;
static u32  fbpitch;

/* What is currently on the real screen.  present_rect() compares the freshly
 * composed frame with this and only writes the pixels that changed, which is
 * what keeps the UI fluid (video memory writes are the slow part).         */
static u32 shown[MAXW * MAXH];
static void present_reset(void) { for (int i = 0; i < W * H; i++) shown[i] = 0xFFFFFFFFu; }
static void present_rect(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++) {
        const u32 *b = back + y * W; u32 *s = shown + y * W;
        int a = x0;
        while (a < x1 && b[a] == s[a]) a++;
        if (a >= x1) continue;
        int e = x1 - 1;
        while (e > a && b[e] == s[e]) e--;
        copy32((u8 *)fbp + (u32)y * fbpitch + (u32)a * 4, b + a, (u32)(e - a + 1));
        copy32(s + a, b + a, (u32)(e - a + 1));
    }
}
static void present(void) { present_rect(0, 0, W, H); }

/* alpha is 0..256 */
static inline u32 blend(u32 bg, u32 fg, int a) {
    u32 rb = (((bg & 0xFF00FF) * (256 - a) + (fg & 0xFF00FF) * a) >> 8) & 0xFF00FF;
    u32 g  = (((bg & 0x00FF00) * (256 - a) + (fg & 0x00FF00) * a) >> 8) & 0x00FF00;
    return rb | g;
}

static void fill_rect(int x, int y, int w, int h, u32 c) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x >= x1 || y >= y1) return;
    for (int j = y; j < y1; j++) {
        u32 *p = dst + j * W + x;
        for (int i = x; i < x1; i++) *p++ = c;
    }
}
static void fill_rect_a(int x, int y, int w, int h, u32 c, int a) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x >= x1 || y >= y1) return;
    for (int j = y; j < y1; j++) {
        u32 *p = dst + j * W + x;
        for (int i = x; i < x1; i++, p++) *p = blend(*p, c, a);
    }
}
static void frame_rect(int x, int y, int w, int h, u32 c) {
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c);
    fill_rect(x + w - 1, y, 1, h, c);
}
static void fill_vgrad(int x, int y, int w, int h, u32 c1, u32 c2) {
    for (int j = 0; j < h; j++) fill_rect(x, y + j, w, 1, blend(c1, c2, h > 1 ? j * 256 / (h - 1) : 0));
}
static void fill_circle(int cx, int cy, int r, u32 c) {
    for (int j = -r; j <= r; j++) {
        int x = 0;
        while ((x + 1) * (x + 1) + j * j <= r * r) x++;
        fill_rect(cx - x, cy + j, 2 * x + 1, 1, c);
    }
}

/* ---- rounded rectangles (vertical gradient c1->c2, alpha 0..256) ---- */
static int rr_inset(int r, int j, int h) {
    static short cache[48][48];                  /* [radius][distance from edge], 0 = not yet computed */
    int d = j < r ? j : (j >= h - r ? h - 1 - j : r);
    if (d >= r) return 0;
    if (r < 48 && cache[r][d]) return cache[r][d] - 1;
    int dd = 2 * (r - d) - 1;
    int v = 4 * r * r - dd * dd, s = 0;
    while ((s + 1) * (s + 1) <= v) s++;
    int ins = r - (s + 1) / 2;
    if (ins < 0) ins = 0;
    if (r < 48) cache[r][d] = (short)(ins + 1);
    return ins;
}
static void rrect(int x, int y, int w, int h, int r, u32 c1, u32 c2, int a) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= H) continue;
        int ins = rr_inset(r, j, h);
        u32 c = (c1 == c2) ? c1 : blend(c1, c2, h > 1 ? j * 256 / (h - 1) : 0);
        if (a >= 256) fill_rect(x + ins, yy, w - 2 * ins, 1, c);
        else          fill_rect_a(x + ins, yy, w - 2 * ins, 1, c, a);
    }
}

/* ------------------------------------------------------------------ text */
static void draw_char(int x, int y, char ch, u32 c) {
    if (ch < 32 || ch > 126) return;
    const u8 *g = font[(u8)ch];
    if (x >= 0 && x + 8 <= W && y >= 0 && y + 16 <= H) {          /* fast path: fully on screen */
        u32 *p = dst + y * W + x;
        for (int r = 0; r < 16; r++, p += W) {
            u8 b = g[r];
            if (!b) continue;
            if (b & 0x80) p[0] = c;
            if (b & 0x40) p[1] = c;
            if (b & 0x20) p[2] = c;
            if (b & 0x10) p[3] = c;
            if (b & 0x08) p[4] = c;
            if (b & 0x04) p[5] = c;
            if (b & 0x02) p[6] = c;
            if (b & 0x01) p[7] = c;
        }
        return;
    }
    for (int r = 0; r < 16; r++) {
        u8 b = g[r];
        int yy = y + r;
        if (!b || yy < 0 || yy >= H) continue;
        for (int k = 0; k < 8; k++)
            if (b & (0x80 >> k)) {
                int xx = x + k;
                if (xx >= 0 && xx < W) dst[yy * W + xx] = c;
            }
    }
}
static void draw_text(int x, int y, const char *s, u32 c) {
    for (; *s; s++, x += 8) draw_char(x, y, *s, c);
}
static void draw_text_bold(int x, int y, const char *s, u32 c) {
    draw_text(x, y, s, c); draw_text(x + 1, y, s, c);
}
static void draw_text_n(int x, int y, const char *s, int n, u32 c) {
    for (int i = 0; s[i] && i < n; i++) draw_char(x + i * 8, y, s[i], c);
}
/* draw within maxw pixels, replacing the tail with ".." when it doesn't fit */
static void draw_text_fit(int x, int y, const char *s, int maxw, u32 c) {
    int n = slen(s), cap = maxw / 8;
    if (cap <= 0) return;
    if (n <= cap) { draw_text(x, y, s, c); return; }
    if (cap <= 2) { draw_text_n(x, y, s, cap, c); return; }
    draw_text_n(x, y, s, cap - 2, c);
    draw_text(x + (cap - 2) * 8, y, "..", c);
}
static int inrect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

/* ----------------------------------------------------------- bitmap images
 * Every decoder produces 0x00RRGGBB pixels in a heap buffer.                */
typedef struct { u32 *px; int w, h; } Img;
static void img_free(Img *im) { if (im->px) kfree(im->px); im->px = 0; im->w = im->h = 0; }

/* Scale a source rectangle into an output buffer.  Averages up to 3x3 samples
 * per output pixel when shrinking, so downscaled photos don't alias.        */
static void scale_box(const Img *im, int sx0, int sy0, int sw, int sh,
                      u32 *out, int ow, int oh, int ostride) {
    static int xa[MAXW], xb[MAXW];
    if (ow <= 0 || oh <= 0 || ow > MAXW || sw <= 0 || sh <= 0) return;
    for (int i = 0; i < ow; i++) {
        xa[i] = sx0 + i * sw / ow;
        xb[i] = sx0 + (i + 1) * sw / ow;
        if (xb[i] <= xa[i]) xb[i] = xa[i] + 1;
    }
    for (int j = 0; j < oh; j++) {
        int y0 = sy0 + j * sh / oh, y1 = sy0 + (j + 1) * sh / oh;
        if (y1 <= y0) y1 = y0 + 1;
        int ys = (y1 - y0 + 2) / 3;
        for (int i = 0; i < ow; i++) {
            int xs = (xb[i] - xa[i] + 2) / 3;
            u32 r = 0, g = 0, b = 0, n = 0;
            for (int y = y0; y < y1; y += ys) {
                const u32 *row = im->px + (u32)clampi(y, 0, im->h - 1) * im->w;
                for (int x = xa[i]; x < xb[i]; x += xs) {
                    u32 p = row[clampi(x, 0, im->w - 1)];
                    r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
                }
            }
            out[j * ostride + i] = ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
}
/* fast nearest-neighbour blit into the current target (used while dragging) */
static void blit_img_nearest(const Img *im, int dx, int dy, int dw, int dh) {
    static int xs[MAXW];
    if (dw <= 0 || dh <= 0 || dw > MAXW) return;
    for (int i = 0; i < dw; i++) xs[i] = i * im->w / dw;
    for (int j = 0; j < dh; j++) {
        int yy = dy + j;
        if (yy < 0 || yy >= H) continue;
        const u32 *row = im->px + (u32)(j * im->h / dh) * im->w;
        for (int i = 0; i < dw; i++) {
            int xx = dx + i;
            if (xx >= 0 && xx < W) dst[yy * W + xx] = row[xs[i]];
        }
    }
}
static void fit_size(int iw, int ih, int aw, int ah, int *dw, int *dh) {
    if (iw * ah > ih * aw) { *dw = aw; *dh = ih * aw / iw; }
    else                   { *dh = ah; *dw = iw * ah / ih; }
    if (*dw < 1) *dw = 1;
    if (*dh < 1) *dh = 1;
}
static void blit_buf(const u32 *src, int sw, int sh, int dx, int dy) {
    for (int j = 0; j < sh; j++) {
        int yy = dy + j;
        if (yy < 0 || yy >= H) continue;
        int x0 = dx < 0 ? -dx : 0, x1 = dx + sw > W ? W - dx : sw;
        if (x1 > x0) memcpy(dst + yy * W + dx + x0, src + j * sw + x0, (u32)(x1 - x0) * 4);
    }
}
#endif

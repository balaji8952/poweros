/* image.h - BMP, PNG (with inflate) and baseline JPEG decoders -> 0x00RRGGBB */
#ifndef NOVA_IMAGE_H
#define NOVA_IMAGE_H
#include "core.h"
#include "gfx.h"

#define MAX_IMG_PIXELS 3000000
#define MAX_IMG_SIDE   8192

static u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static u32 rd16(const u8 *p) { return p[0] | (p[1] << 8); }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static u32 be16(const u8 *p) { return (p[0] << 8) | p[1]; }

static int img_alloc(Img *im, int w, int h, const char **err) {
    if (w <= 0 || h <= 0 || w > MAX_IMG_SIDE || h > MAX_IMG_SIDE || (u32)w * (u32)h > MAX_IMG_PIXELS) {
        *err = "Image is too large (max 3 megapixels)"; return 0;
    }
    im->px = kmalloc((u32)w * h * 4);
    if (!im->px) { *err = "Out of memory"; return 0; }
    im->w = w; im->h = h;
    return 1;
}
/* composite over a light checkerboard so transparency is visible */
static u32 over_checker(u32 fg, int a, int x, int y) {
    if (a >= 255) return fg;
    u32 bg = (((x >> 3) + (y >> 3)) & 1) ? 0xFFFFFF : 0xC8C8C8;
    return blend(bg, fg, a + (a >> 7));
}

/* =============================================================== inflate
 * Compact "puff"-style decoder: stored, fixed and dynamic Huffman blocks.  */
typedef struct {
    const u8 *in; u32 inlen, incnt;
    u8 *out; u32 outlen, outcnt;
    u32 bitbuf; int bitcnt, err;
} Inf;
typedef struct { short count[16]; short symbol[288]; } Huff;

static int inf_bits(Inf *s, int need) {
    u32 val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) { s->err = 1; return 0; }
        val |= (u32)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}
static int inf_decode(Inf *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= inf_bits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}
static int inf_construct(Huff *h, const short *length, int n) {
    for (int l = 0; l <= 15; l++) h->count[l] = 0;
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l <= 15; l++) { left <<= 1; left -= h->count[l]; if (left < 0) return left; }
    short offs[16]; offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h->count[l];
    for (int i = 0; i < n; i++) if (length[i]) h->symbol[offs[length[i]]++] = (short)i;
    return left;
}
static const short inf_lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short inf_lext[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short inf_dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short inf_dext[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int inf_codes(Inf *s, const Huff *lencode, const Huff *distcode) {
    for (;;) {
        int sym = inf_decode(s, lencode);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->outcnt >= s->outlen) return -1;
            s->out[s->outcnt++] = (u8)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = inf_lbase[sym] + inf_bits(s, inf_lext[sym]);
            sym = inf_decode(s, distcode);
            if (sym < 0 || sym >= 30) return -1;
            u32 dist = (u32)(inf_dbase[sym] + inf_bits(s, inf_dext[sym]));
            if (s->err || dist > s->outcnt || s->outcnt + len > s->outlen) return -1;
            for (int i = 0; i < len; i++, s->outcnt++) s->out[s->outcnt] = s->out[s->outcnt - dist];
        }
    }
}
static int inflate_raw(const u8 *in, u32 inlen, u8 *out, u32 outlen, u32 *produced) {
    static Huff lencode, distcode;
    Inf s; memset(&s, 0, sizeof s);
    s.in = in; s.inlen = inlen; s.out = out; s.outlen = outlen;
    int last;
    do {
        last = inf_bits(&s, 1);
        int type = inf_bits(&s, 2);
        if (s.err) return -1;
        if (type == 0) {
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.incnt + 4 > s.inlen) return -1;
            u32 len = s.in[s.incnt] | (s.in[s.incnt + 1] << 8);
            u32 nlen = s.in[s.incnt + 2] | (s.in[s.incnt + 3] << 8);
            s.incnt += 4;
            if (len != (~nlen & 0xFFFF) || s.incnt + len > s.inlen || s.outcnt + len > s.outlen) return -1;
            memcpy(s.out + s.outcnt, s.in + s.incnt, len);
            s.incnt += len; s.outcnt += len;
        } else if (type == 1) {
            short lengths[320]; int i;
            for (i = 0; i < 144; i++) lengths[i] = 8;
            for (; i < 256; i++) lengths[i] = 9;
            for (; i < 280; i++) lengths[i] = 7;
            for (; i < 288; i++) lengths[i] = 8;
            inf_construct(&lencode, lengths, 288);
            for (i = 0; i < 30; i++) lengths[i] = 5;
            inf_construct(&distcode, lengths, 30);
            if (inf_codes(&s, &lencode, &distcode)) return -1;
        } else if (type == 2) {
            static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            short lengths[320];
            int nlen = inf_bits(&s, 5) + 257, ndist = inf_bits(&s, 5) + 1, ncode = inf_bits(&s, 4) + 4;
            if (s.err || nlen > 286 || ndist > 30) return -1;
            int idx;
            for (idx = 0; idx < ncode; idx++) lengths[order[idx]] = (short)inf_bits(&s, 3);
            for (; idx < 19; idx++) lengths[order[idx]] = 0;
            if (inf_construct(&lencode, lengths, 19) != 0) return -1;
            idx = 0;
            while (idx < nlen + ndist) {
                int sym = inf_decode(&s, &lencode);
                if (sym < 0) return -1;
                if (sym < 16) lengths[idx++] = (short)sym;
                else {
                    int len = 0, rep;
                    if (sym == 16) { if (idx == 0) return -1; len = lengths[idx - 1]; rep = 3 + inf_bits(&s, 2); }
                    else if (sym == 17) rep = 3 + inf_bits(&s, 3);
                    else rep = 11 + inf_bits(&s, 7);
                    if (idx + rep > nlen + ndist) return -1;
                    while (rep--) lengths[idx++] = (short)len;
                }
            }
            if (s.err || lengths[256] == 0) return -1;
            int e = inf_construct(&lencode, lengths, nlen);
            if (e < 0 || (e > 0 && nlen - lencode.count[0] != 1)) return -1;
            e = inf_construct(&distcode, lengths + nlen, ndist);
            if (e < 0 || (e > 0 && ndist - distcode.count[0] != 1)) return -1;
            if (inf_codes(&s, &lencode, &distcode)) return -1;
        } else return -1;
    } while (!last);
    *produced = s.outcnt;
    return 0;
}

/* =================================================================== PNG */
static int png_decode(const u8 *d, u32 n, Img *out, const char **err) {
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    u32 pal[256]; u8 trns[256];
    for (int i = 0; i < 256; i++) { trns[i] = 255; pal[i] = 0; }
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0, haveih = 0;
    u32 idat_total = 0, pos = 8;
    if (n < 33 || memcmp(d, sig, 8)) { *err = "Not a PNG file"; return 0; }
    for (int pass = 0; pass < 2; pass++) {
        pos = 8;
        u8 *comp = 0; u32 cpos = 0;
        if (pass == 1) {
            comp = kmalloc(idat_total ? idat_total : 16);
            if (!comp) { *err = "Out of memory"; return 0; }
            /* fallthrough to chunk loop collecting IDATs */
        }
        while (pos + 12 <= n) {
            u32 len = be32(d + pos); const u8 *type = d + pos + 4; const u8 *cd = d + pos + 8;
            if (len > n || pos + 12 + len > n) break;
            if (pass == 0) {
                if (!memcmp(type, "IHDR", 4) && len >= 13) {
                    w = (int)be32(cd); h = (int)be32(cd + 4);
                    depth = cd[8]; ctype = cd[9]; interlace = cd[12]; haveih = 1;
                } else if (!memcmp(type, "PLTE", 4)) {
                    for (u32 i = 0; i < len / 3 && i < 256; i++)
                        pal[i] = (cd[i * 3] << 16) | (cd[i * 3 + 1] << 8) | cd[i * 3 + 2];
                } else if (!memcmp(type, "tRNS", 4) && ctype == 3) {
                    for (u32 i = 0; i < len && i < 256; i++) trns[i] = cd[i];
                } else if (!memcmp(type, "IDAT", 4)) idat_total += len;
            } else if (!memcmp(type, "IDAT", 4)) {
                memcpy(comp + cpos, cd, len); cpos += len;
            }
            if (!memcmp(type, "IEND", 4)) break;
            pos += 12 + len;
        }
        if (pass == 0) {
            if (!haveih) { *err = "Corrupt PNG"; return 0; }
            if (interlace) { *err = "Interlaced PNG not supported"; return 0; }
            int okd = (ctype == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
                      (ctype == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
                      ((ctype == 2 || ctype == 4 || ctype == 6) && (depth == 8 || depth == 16));
            if (!okd) { *err = "Unsupported PNG format"; return 0; }
            if (w <= 0 || h <= 0 || w > MAX_IMG_SIDE || h > MAX_IMG_SIDE || (u32)w * (u32)h > MAX_IMG_PIXELS) {
                *err = "Image is too large (max 3 megapixels)"; return 0;
            }
            if (!idat_total) { *err = "Corrupt PNG"; return 0; }
            continue;
        }
        /* pass 1: inflate + unfilter + convert */
        int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : 4;
        int bitspp = ch * depth, bpp = bitspp >= 8 ? bitspp / 8 : 1;
        u32 rb = ((u32)w * bitspp + 7) / 8;
        u32 rawsize = (u32)h * (rb + 1);
        u8 *raw = kmalloc(rawsize);
        if (!raw) { kfree(comp); *err = "Out of memory"; return 0; }
        u32 got = 0;
        int bad = cpos < 6 || (comp[0] & 15) != 8 || (comp[1] & 0x20) ||
                  inflate_raw(comp + 2, cpos - 2, raw, rawsize, &got) || got < rawsize;
        kfree(comp);
        if (bad) { kfree(raw); *err = "Corrupt PNG data"; return 0; }
        if (!img_alloc(out, w, h, err)) { kfree(raw); return 0; }
        static u8 zero[MAX_IMG_SIDE * 8 + 8];
        for (int y = 0; y < h; y++) {
            u8 *row = raw + (u32)y * (rb + 1) + 1;
            const u8 *prev = y ? row - (rb + 1) : zero;
            int ft = row[-1];
            for (u32 i = 0; i < rb; i++) {
                int a = i >= (u32)bpp ? row[i - bpp] : 0, b = prev[i], c = i >= (u32)bpp ? prev[i - bpp] : 0, x = row[i];
                switch (ft) {
                    case 1: x += a; break;
                    case 2: x += b; break;
                    case 3: x += (a + b) >> 1; break;
                    case 4: { int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
                              x += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); } break;
                }
                row[i] = (u8)x;
            }
            u32 *o = out->px + (u32)y * w;
            for (int x = 0; x < w; x++) {
                u32 rgb; int a = 255;
                if (ctype == 2) {
                    const u8 *p = row + x * 3 * (depth / 8);
                    int st = depth / 8;
                    rgb = (p[0] << 16) | (p[st] << 8) | p[2 * st];
                } else if (ctype == 6) {
                    const u8 *p = row + x * 4 * (depth / 8);
                    int st = depth / 8;
                    rgb = (p[0] << 16) | (p[st] << 8) | p[2 * st]; a = p[3 * st];
                } else if (ctype == 4) {
                    const u8 *p = row + x * 2 * (depth / 8);
                    int g = p[0]; a = p[depth / 8];
                    rgb = (g << 16) | (g << 8) | g;
                } else {                                     /* gray or palette, 1..16 bit */
                    int v;
                    if (depth == 16) v = row[x * 2];
                    else if (depth == 8) v = row[x];
                    else { int bit = x * depth; v = (row[bit >> 3] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1); }
                    if (ctype == 3) { rgb = pal[v]; a = trns[v]; }
                    else { int g = depth >= 8 ? v : v * 255 / ((1 << depth) - 1); rgb = (g << 16) | (g << 8) | g; }
                }
                o[x] = over_checker(rgb, a, x, y);
            }
        }
        kfree(raw);
        return 1;
    }
    return 0;
}

/* =================================================================== BMP */
static int bmp_decode(const u8 *d, u32 n, Img *out, const char **err) {
    if (n < 54 || d[0] != 'B' || d[1] != 'M') { *err = "Not a BMP file"; return 0; }
    u32 off = rd32(d + 10), dib = rd32(d + 14);
    if (dib < 40) { *err = "Unsupported BMP header"; return 0; }
    int w = (i32)rd32(d + 18), h = (i32)rd32(d + 22);
    u32 bpp = rd16(d + 28), comp = rd32(d + 30), used = rd32(d + 46);
    int topdown = h < 0; if (h < 0) h = -h;
    if (comp != 0 && !(comp == 3 && (bpp == 32 || bpp == 16))) { *err = "Compressed BMP not supported"; return 0; }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) { *err = "Unsupported BMP depth"; return 0; }
    u32 stride = (((u32)w * bpp + 31) / 32) * 4;
    if (w <= 0 || h <= 0 || off + stride * (u32)h > n) { *err = "Corrupt BMP"; return 0; }
    u32 pal[256]; u32 npal = 0;
    if (bpp <= 8) {
        npal = used ? used : (1u << bpp);
        if (npal > 256 || 14 + dib + npal * 4 > n) { *err = "Corrupt BMP palette"; return 0; }
        for (u32 i = 0; i < npal; i++) pal[i] = rd32(d + 14 + dib + i * 4) & 0xFFFFFF;
    }
    int is565 = 0;
    if (bpp == 16 && comp == 3 && n >= 66 && rd32(d + 58) == 0x07E0) is565 = 1;
    if (!img_alloc(out, w, h, err)) return 0;
    for (int y = 0; y < h; y++) {
        const u8 *row = d + off + stride * (u32)(topdown ? y : h - 1 - y);
        u32 *o = out->px + (u32)y * w;
        for (int x = 0; x < w; x++) {
            u32 c;
            switch (bpp) {
                case 1:  c = pal[(row[x >> 3] >> (7 - (x & 7))) & 1]; break;
                case 4:  c = pal[(row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15]; break;
                case 8:  c = pal[row[x]]; break;
                case 16: { u32 v = rd16(row + x * 2); int r, g, b;
                           if (is565) { r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31; r = r * 255 / 31; g = g * 255 / 63; b = b * 255 / 31; }
                           else { r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31; r = r * 255 / 31; g = g * 255 / 31; b = b * 255 / 31; }
                           c = (r << 16) | (g << 8) | b; } break;
                case 24: c = (row[x * 3 + 2] << 16) | (row[x * 3 + 1] << 8) | row[x * 3]; break;
                default: c = (row[x * 4 + 2] << 16) | (row[x * 4 + 1] << 8) | row[x * 4]; break;
            }
            o[x] = c;
        }
    }
    return 1;
}

/* ================================================================== JPEG
 * Baseline (sequential, Huffman) JPEG: grayscale or YCbCr, any chroma
 * subsampling, restart markers.  Progressive files are rejected.            */
typedef struct { int maxcode[18], mincode[17], valptr[17]; u8 vals[256]; int ok; } JH;
typedef struct { const u8 *p, *end; u32 acc; int cnt; } JBits;
typedef struct { int id, h, v, tq, td, ta, pred, bw, bh; u8 *plane; } JC;

static const u8 jzz[64] = {
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };

static int jbit(JBits *b) {
    if (b->cnt == 0) {
        u32 c = 0;
        if (b->p < b->end) {
            c = *b->p++;
            if (c == 0xFF) {
                u32 c2 = b->p < b->end ? *b->p : 0;
                if (c2 == 0) b->p++;                      /* stuffed zero */
                else { b->p--; c = 0; }                   /* marker: feed zeros, stay on it */
            }
        }
        b->acc = c; b->cnt = 8;
    }
    b->cnt--;
    return (b->acc >> b->cnt) & 1;
}
static int jhuff(JBits *b, const JH *h) {
    int code = jbit(b), l = 1;
    while (l <= 16 && code > h->maxcode[l]) { code = (code << 1) | jbit(b); l++; }
    if (l > 16) return 0;
    int i = h->valptr[l] + code - h->mincode[l];
    return h->vals[i & 255];
}
static int jrecv(JBits *b, int n) {
    int v = 0;
    while (n--) v = (v << 1) | jbit(b);
    return v;
}
static int jext(int v, int n) { return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

#define JP_IDCT_1D(s0,s1,s2,s3,s4,s5,s6,s7)                                   \
    int t0,t1,t2,t3,p1,p2,p3,p4,p5,x0,x1,x2,x3;                              \
    p2 = s2; p3 = s6;                                                        \
    p1 = (p2+p3) * 2217;                                                     \
    t2 = p1 + p3 * -7568;                                                    \
    t3 = p1 + p2 * 3135;                                                     \
    p2 = s0; p3 = s4;                                                        \
    t0 = (p2+p3) * 4096;                                                     \
    t1 = (p2-p3) * 4096;                                                     \
    x0 = t0+t3; x3 = t0-t3; x1 = t1+t2; x2 = t1-t2;                          \
    t0 = s7; t1 = s5; t2 = s3; t3 = s1;                                      \
    p3 = t0+t2; p4 = t1+t3; p1 = t0+t3; p2 = t1+t2;                          \
    p5 = (p3+p4) * 4816;                                                     \
    t0 = t0 * 1223; t1 = t1 * 8410; t2 = t2 * 12586; t3 = t3 * 6149;         \
    p1 = p5 + p1 * -3686;                                                    \
    p2 = p5 + p2 * -10498;                                                   \
    p3 = p3 * -8035;                                                         \
    p4 = p4 * -1598;                                                         \
    t3 += p1+p4; t2 += p2+p3; t1 += p2+p4; t0 += p1+p3;

static u8 jclamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (u8)v); }
static void jidct(const int *in, u8 *out, int stride) {
    int tmp[64];
    for (int i = 0; i < 8; i++) {
        JP_IDCT_1D(in[i], in[i+8], in[i+16], in[i+24], in[i+32], in[i+40], in[i+48], in[i+56])
        x0 += 512; x1 += 512; x2 += 512; x3 += 512;
        tmp[i]    = (x0 + t3) >> 10;  tmp[i+56] = (x0 - t3) >> 10;
        tmp[i+8]  = (x1 + t2) >> 10;  tmp[i+48] = (x1 - t2) >> 10;
        tmp[i+16] = (x2 + t1) >> 10;  tmp[i+40] = (x2 - t1) >> 10;
        tmp[i+24] = (x3 + t0) >> 10;  tmp[i+32] = (x3 - t0) >> 10;
    }
    for (int i = 0; i < 8; i++) {
        const int *v = tmp + i * 8;
        u8 *o = out + i * stride;
        JP_IDCT_1D(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
        x0 += 65536 + (128 << 17); x1 += 65536 + (128 << 17);
        x2 += 65536 + (128 << 17); x3 += 65536 + (128 << 17);
        o[0] = jclamp((x0 + t3) >> 17); o[7] = jclamp((x0 - t3) >> 17);
        o[1] = jclamp((x1 + t2) >> 17); o[6] = jclamp((x1 - t2) >> 17);
        o[2] = jclamp((x2 + t1) >> 17); o[5] = jclamp((x2 - t1) >> 17);
        o[3] = jclamp((x3 + t0) >> 17); o[4] = jclamp((x3 - t0) >> 17);
    }
}

static int jpeg_decode(const u8 *d, u32 n, Img *out, const char **err) {
    static JH hdc[4], hac[4];
    static int qt[4][64];
    static int xmap[3][MAX_IMG_SIDE];
    JC comp[3]; int nc = 0, w = 0, h = 0, ri = 0, hmax = 1, vmax = 1;
    for (int i = 0; i < 4; i++) { hdc[i].ok = hac[i].ok = 0; for (int k = 0; k < 64; k++) qt[i][k] = 1; }
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) { *err = "Not a JPEG file"; return 0; }
    u32 pos = 2;
    while (pos + 4 <= n) {
        if (d[pos] != 0xFF) { pos++; continue; }
        int m = d[pos + 1];
        if (m == 0xFF) { pos++; continue; }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0) { pos += 2; continue; }
        if (m == 0xD9) break;
        u32 len = be16(d + pos + 2);
        if (len < 2 || pos + 2 + len > n) break;
        const u8 *seg = d + pos + 4; u32 sl = len - 2;
        if (m == 0xDB) {
            u32 o = 0;
            while (o < sl) {
                int pq = seg[o] >> 4, tq = seg[o] & 3; o++;
                for (int k = 0; k < 64 && o < sl; k++) { qt[tq][k] = pq ? (int)be16(seg + o) : seg[o]; o += pq ? 2 : 1; }
            }
        } else if (m == 0xC0 || m == 0xC1) {
            if (sl < 6 || seg[0] != 8) { *err = "Unsupported JPEG precision"; return 0; }
            h = (int)be16(seg + 1); w = (int)be16(seg + 3); nc = seg[5];
            if ((nc != 1 && nc != 3) || sl < 6 + (u32)nc * 3) { *err = "Unsupported JPEG (CMYK?)"; return 0; }
            for (int i = 0; i < nc; i++) {
                comp[i].id = seg[6 + i * 3]; comp[i].h = seg[7 + i * 3] >> 4; comp[i].v = seg[7 + i * 3] & 15;
                comp[i].tq = seg[8 + i * 3] & 3; comp[i].pred = 0; comp[i].plane = 0; comp[i].td = comp[i].ta = 0;
                if (comp[i].h < 1 || comp[i].h > 4 || comp[i].v < 1 || comp[i].v > 4) { *err = "Bad JPEG sampling"; return 0; }
            }
        } else if (m == 0xC2) { *err = "Progressive JPEG not supported"; return 0; }
        else if ((m >= 0xC3 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) { *err = "Unsupported JPEG type"; return 0; }
        else if (m == 0xC4) {
            u32 o = 0;
            while (o + 17 <= sl) {
                int tc = seg[o] >> 4, th = seg[o] & 3;
                JH *hh = tc ? &hac[th] : &hdc[th];
                const u8 *cnt = seg + o + 1; int total = 0;
                for (int i = 0; i < 16; i++) total += cnt[i];
                if (total > 256 || o + 17 + (u32)total > sl) break;
                memcpy(hh->vals, seg + o + 17, (u32)total);
                int code = 0, k = 0;
                for (int l = 1; l <= 16; l++) {
                    hh->valptr[l] = k; hh->mincode[l] = code;
                    code += cnt[l - 1]; k += cnt[l - 1];
                    hh->maxcode[l] = cnt[l - 1] ? code - 1 : -1;
                    code <<= 1;
                }
                hh->maxcode[17] = 0x7FFFFFFF; hh->ok = 1;
                o += 17 + (u32)total;
            }
        } else if (m == 0xDD && sl >= 2) ri = (int)be16(seg);
        else if (m == 0xDA) {
            if (!nc || !w || !h) { *err = "Corrupt JPEG"; return 0; }
            int ns = seg[0];
            if (ns != nc) { *err = "Multi-scan JPEG not supported"; return 0; }
            for (int i = 0; i < ns; i++) {
                int cs = seg[1 + i * 2], tdta = seg[2 + i * 2];
                for (int k = 0; k < nc; k++) if (comp[k].id == cs) { comp[k].td = tdta >> 4 & 3; comp[k].ta = tdta & 3; }
            }
            if (nc == 1) { comp[0].h = comp[0].v = 1; }
            for (int i = 0; i < nc; i++) { if (comp[i].h > hmax) hmax = comp[i].h; if (comp[i].v > vmax) vmax = comp[i].v; }
            if ((u32)w * (u32)h > MAX_IMG_PIXELS || w > MAX_IMG_SIDE || h > MAX_IMG_SIDE) { *err = "Image is too large (max 3 megapixels)"; return 0; }
            int mcux = (w + 8 * hmax - 1) / (8 * hmax), mcuy = (h + 8 * vmax - 1) / (8 * vmax);
            for (int i = 0; i < nc; i++) {
                comp[i].bw = mcux * comp[i].h * 8; comp[i].bh = mcuy * comp[i].v * 8;
                comp[i].plane = kmalloc((u32)comp[i].bw * comp[i].bh);
                if (!comp[i].plane) { for (int k = 0; k < i; k++) kfree(comp[k].plane); *err = "Out of memory"; return 0; }
            }
            JBits jb; jb.p = d + pos + 2 + len; jb.end = d + n; jb.acc = 0; jb.cnt = 0;
            int blk[64], rst = 0, bad = 0;
            for (int my = 0; my < mcuy && !bad; my++) {
                for (int mx_ = 0; mx_ < mcux; mx_++) {
                    if (ri && rst == ri) {                       /* restart interval */
                        jb.cnt = 0;
                        while (jb.p + 1 < jb.end && !(jb.p[0] == 0xFF && jb.p[1] >= 0xD0 && jb.p[1] <= 0xD7)) jb.p++;
                        if (jb.p + 1 < jb.end) jb.p += 2;
                        for (int i = 0; i < nc; i++) comp[i].pred = 0;
                        rst = 0;
                    }
                    rst++;
                    for (int ci = 0; ci < nc; ci++) {
                        JC *c = &comp[ci];
                        if (!hdc[c->td].ok || !hac[c->ta].ok) { bad = 1; break; }
                        for (int by = 0; by < c->v; by++) for (int bx = 0; bx < c->h; bx++) {
                            memset(blk, 0, sizeof blk);
                            int t = jhuff(&jb, &hdc[c->td]);
                            if (t > 11) t = 11;
                            int diff = t ? jext(jrecv(&jb, t), t) : 0;
                            c->pred += diff;
                            blk[0] = c->pred * qt[c->tq][0];
                            for (int k = 1; k < 64;) {
                                int rs = jhuff(&jb, &hac[c->ta]), r = rs >> 4, s = rs & 15;
                                if (s == 0) { if (r == 15) { k += 16; continue; } break; }
                                k += r;
                                if (k > 63) break;
                                blk[jzz[k]] = jext(jrecv(&jb, s), s) * qt[c->tq][k];
                                k++;
                            }
                            jidct(blk, c->plane + (u32)(my * c->v * 8 + by * 8) * c->bw + mx_ * c->h * 8 + bx * 8, c->bw);
                        }
                    }
                    if (bad) break;
                }
            }
            if (bad) { for (int i = 0; i < nc; i++) kfree(comp[i].plane); *err = "Corrupt JPEG data"; return 0; }
            if (!img_alloc(out, w, h, err)) { for (int i = 0; i < nc; i++) kfree(comp[i].plane); return 0; }
            for (int i = 0; i < nc; i++) for (int x = 0; x < w; x++) xmap[i][x] = x * comp[i].h / hmax;
            for (int y = 0; y < h; y++) {
                const u8 *rp[3];
                for (int i = 0; i < nc; i++) rp[i] = comp[i].plane + (u32)(y * comp[i].v / vmax) * comp[i].bw;
                u32 *o = out->px + (u32)y * w;
                if (nc == 1) for (int x = 0; x < w; x++) { int v = rp[0][xmap[0][x]]; o[x] = (v << 16) | (v << 8) | v; }
                else for (int x = 0; x < w; x++) {
                    int Y = rp[0][xmap[0][x]], cb = rp[1][xmap[1][x]] - 128, cr = rp[2][xmap[2][x]] - 128;
                    int r = Y + ((91881 * cr) >> 16);
                    int g = Y - ((22553 * cb + 46802 * cr) >> 16);
                    int b = Y + ((116130 * cb) >> 16);
                    o[x] = (jclamp(r) << 16) | (jclamp(g) << 8) | jclamp(b);
                }
            }
            for (int i = 0; i < nc; i++) kfree(comp[i].plane);
            return 1;
        }
        pos += 2 + len;
    }
    *err = "Corrupt JPEG";
    return 0;
}

/* ---------------------------------------------------------------- dispatch */
static int image_decode(const u8 *d, u32 n, Img *out, const char **err) {
    out->px = 0; out->w = out->h = 0;
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P') return png_decode(d, n, out, err);
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return jpeg_decode(d, n, out, err);
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return bmp_decode(d, n, out, err);
    *err = "Unknown image format";
    return 0;
}
static int is_image_name(const char *name) {
    return ext_is(name, "bmp") || ext_is(name, "png") || ext_is(name, "jpg") || ext_is(name, "jpeg") || ext_is(name, "jpe");
}
#endif

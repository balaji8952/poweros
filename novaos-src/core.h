/* core.h - types, port I/O, libc bits, string helpers, heap allocator */
#ifndef NOVA_CORE_H
#define NOVA_CORE_H
typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef int            i32;

#ifdef NOVA_HOST            /* host build: used only by the test harness */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
static void (*hk_out)(u16 port, u32 val, int size);
static u32  (*hk_in)(u16 port, int size);
static void outb(u16 p, u8 v)  { if (hk_out) hk_out(p, v, 1); }
static void outw(u16 p, u16 v) { if (hk_out) hk_out(p, v, 2); }
static u8   inb(u16 p)         { return hk_in ? (u8)hk_in(p, 1) : 0; }
static u16  inw(u16 p)         { return hk_in ? (u16)hk_in(p, 2) : 0; }
static void ins16(u16 p, void *buf, int n)        { u16 *b = buf; for (int i = 0; i < n; i++) b[i] = inw(p); }
static void outs16(u16 p, const void *buf, int n) { const u16 *b = buf; for (int i = 0; i < n; i++) outw(p, b[i]); }
static void copy32(void *d, const void *s, u32 n) { memcpy(d, s, (size_t)n * 4); }
#else
static inline void outb(u16 p, u8 v)  { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)         { u8 v;  __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline u16  inw(u16 p)         { u16 v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }

static inline void ins16(u16 p, void *buf, int n) {
    __asm__ volatile("rep insw" : "+D"(buf), "+c"(n) : "d"(p) : "memory");
}
static inline void outs16(u16 p, const void *buf, int n) {
    __asm__ volatile("rep outsw" : "+S"(buf), "+c"(n) : "d"(p) : "memory");
}

/* libc bits gcc may emit calls to */
void *memcpy(void *d, const void *s, u32 n) {
    void *r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
    return r;
}
void *memset(void *d, int c, u32 n) {
    void *r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
void *memmove(void *d, const void *s, u32 n) {
    u8 *dd = d; const u8 *ss = s;
    if (dd < ss) { for (u32 i = 0; i < n; i++) dd[i] = ss[i]; }
    else         { for (u32 i = n; i > 0; i--) dd[i - 1] = ss[i - 1]; }
    return d;
}
int memcmp(const void *a, const void *b, u32 n) {
    const u8 *x = a, *y = b;
    for (u32 i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
static void copy32(void *d, const void *s, u32 n) {
    __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
}
#endif

/* ------------------------------------------------------------- strings */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int scmp_ci(const char *a, const char *b) {
    while (*a && *b && lc((u8)*a) == lc((u8)*b)) { a++; b++; }
    return lc((u8)*a) - lc((u8)*b);
}
static void scpy(char *d, const char *s, int max) {
    int i = 0;
    for (; i < max - 1 && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}
static const char *sext(const char *name) {      /* extension without the dot, or "" */
    const char *dot = 0;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    return (dot && dot != name) ? dot + 1 : "";
}
static int ext_is(const char *name, const char *ext) { return scmp_ci(sext(name), ext) == 0; }
static char *put_u32(char *p, u32 v) {
    char t[12]; int n = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static char *put_str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static u32 ld32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static void st32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int mini(int a, int b) { return a < b ? a : b; }
static int maxi(int a, int b) { return a > b ? a : b; }

/* ---------------------------------------------------------------- heap
 * First-fit free-list allocator over a static arena.  Used for file data,
 * decoded images and caches.                                              */
#define HEAP_SIZE (32u * 1024 * 1024)
typedef struct HB { u32 size, used, pad0, pad1; } HB;      /* 16-byte header */
static u8 heap_mem[HEAP_SIZE] __attribute__((aligned(16)));
static int heap_ready;
static u32 heap_used_bytes;
static void (*heap_reclaim)(void);          /* set by fs.h: frees cached file data */

static void heap_init(void) {
    HB *h = (HB *)heap_mem;
    h->size = HEAP_SIZE - sizeof(HB); h->used = 0;
    heap_ready = 1;
}
static void *kmalloc_try(u32 n);
#ifdef NOVA_HOST
static int host_fail_alloc_after = -1;           /* test hook: after N more allocations every one fails; -1 = off */
#endif
static void *kmalloc(u32 n) {
#ifdef NOVA_HOST
    if (host_fail_alloc_after >= 0) { if (host_fail_alloc_after == 0) return 0; host_fail_alloc_after--; }
#endif
    void *p = kmalloc_try(n);
    if (!p && heap_reclaim) { heap_reclaim(); p = kmalloc_try(n); }
    return p;
}
static void *kmalloc_try(u32 n) {
    if (!heap_ready) heap_init();
    n = (n + 15) & ~15u; if (!n) n = 16;
    u8 *end = heap_mem + HEAP_SIZE;
    for (HB *h = (HB *)heap_mem; (u8 *)h < end; h = (HB *)((u8 *)h + sizeof(HB) + h->size)) {
        if (h->used || h->size < n) continue;
        if (h->size >= n + sizeof(HB) + 16) {
            HB *nx = (HB *)((u8 *)h + sizeof(HB) + n);
            nx->size = h->size - n - sizeof(HB); nx->used = 0;
            h->size = n;
        }
        h->used = 1; heap_used_bytes += h->size;
        return (u8 *)h + sizeof(HB);
    }
    return 0;
}
static void kfree(void *p) {
    if (!p) return;
    HB *h = (HB *)((u8 *)p - sizeof(HB));
    heap_used_bytes -= h->size; h->used = 0;
    u8 *end = heap_mem + HEAP_SIZE;
    HB *c = (HB *)heap_mem;
    while ((u8 *)c < end) {                      /* coalesce neighbours */
        HB *nx = (HB *)((u8 *)c + sizeof(HB) + c->size);
        if ((u8 *)nx < end && !c->used && !nx->used) { c->size += sizeof(HB) + nx->size; continue; }
        c = nx;
    }
}
#endif

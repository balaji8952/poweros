/* hw.h - RTC, PIT timer, Bochs/QEMU/VirtualBox VGA (BGA) mode switching, PS/2 */
#ifndef NOVA_HW_H
#define NOVA_HW_H
#include "core.h"

static u8 cmos(u8 r) { outb(0x70, r); return inb(0x71); }
static int bcd_dec(int v, int bin) { return bin ? v : (v & 15) + (v >> 4) * 10; }
#ifdef NOVA_HOST
static void get_time(int *h, int *m) { *h = 14; *m = 5; }
static void get_date(int *y, int *mo, int *d) { *y = 2026; *mo = 10; *d = 12; }
#else
static void rtc_wait(void) { int spin = 0; while ((cmos(0x0A) & 0x80) && spin++ < 100000) {} }
static void get_time(int *h, int *m) {
    rtc_wait();
    u8 mi = cmos(2), hr = cmos(4), b = cmos(0x0B);
    int pm = hr & 0x80, bin = b & 4;
    hr &= 0x7F;
    int hh = bcd_dec(hr, bin);
    if (!(b & 2)) hh = (hh % 12) + (pm ? 12 : 0);
    *h = hh; *m = bcd_dec(mi, bin);
}
static void get_date(int *y, int *mo, int *d) {
    rtc_wait();
    u8 b = cmos(0x0B); int bin = b & 4;
    int day = bcd_dec(cmos(7), bin), mon = bcd_dec(cmos(8), bin), yy = bcd_dec(cmos(9), bin);
    int cen = bcd_dec(cmos(0x32), bin);
    *y = (cen >= 19 && cen <= 21) ? cen * 100 + yy : 2000 + yy;
    *mo = mon < 1 || mon > 12 ? 1 : mon; *d = day < 1 || day > 31 ? 1 : day;
}
#endif

/* ---- millisecond clock.  The PIT is only used once, to calibrate the CPU's
 * time-stamp counter; after that now_ms() needs no port I/O and cannot lose
 * time during long frames.  Falls back to polling the PIT if there is no TSC. */
#ifdef NOVA_HOST
static u32 host_ms;
static void pit_init(void) {}
static u32 now_ms(void) { return host_ms; }
#else
static u16 pit_last; static u32 pit_rem, pit_ms, tsc_per_ms, tsc_last, tsc_rem;
static u16 pit_read(void) { outb(0x43, 0x00); u8 lo = inb(0x40); u8 hi = inb(0x40); return (u16)(lo | (hi << 8)); }
static u32 rdtsc32(void) { u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return lo; }
static void pit_init(void) {
    outb(0x43, 0x34); outb(0x40, 0); outb(0x40, 0);          /* channel 0, mode 2, 18.2 Hz wrap */
    pit_last = pit_read();
    u16 last = pit_last; u32 acc = 0, t0 = rdtsc32();
    while (acc < 59659) { u16 c = pit_read(); acc += (u16)(last - c); last = c; }     /* ~50 ms */
    u32 d = rdtsc32() - t0, ms = acc / 1193;
    tsc_per_ms = ms ? d / ms : 0;
    if (tsc_per_ms < 200) tsc_per_ms = 0;                     /* implausible: use the PIT */
    tsc_last = rdtsc32(); pit_last = pit_read();
}
static u32 now_ms(void) {
    if (tsc_per_ms) {
        u32 t = rdtsc32();
        tsc_rem += t - tsc_last; tsc_last = t;
        pit_ms += tsc_rem / tsc_per_ms; tsc_rem %= tsc_per_ms;
        return pit_ms;
    }
    u16 c = pit_read();
    pit_rem += (u16)(pit_last - c);
    pit_last = c;
    pit_ms += pit_rem / 1193; pit_rem %= 1193;
    return pit_ms;
}
#endif

/* ---- Bochs VBE Extensions (BGA): lets us change resolution at runtime ---- */
static int bga_ver; static u32 bga_vram;
static void bga_w(u16 i, u16 v) { outw(0x1CE, i); outw(0x1CF, v); }
static u16  bga_r(u16 i)        { outw(0x1CE, i); return inw(0x1CF); }
static void bga_probe(void) {
    u16 id = bga_r(0);
    if (id >= 0xB0C0 && id <= 0xB0C5) {
        bga_ver = id;
        bga_vram = id >= 0xB0C4 ? (u32)bga_r(0x0A) * 65536u : 0;
        if (!bga_vram) bga_vram = 8u << 20;
    }
}
/* returns the new pitch in bytes, or 0 on failure */
static u32 bga_set(int w, int h) {
    if (!bga_ver) return 0;
    if ((u32)w * (u32)h * 4u > bga_vram) return 0;
    bga_w(4, 0);
    bga_w(1, (u16)w); bga_w(2, (u16)h); bga_w(3, 32);
    bga_w(6, (u16)w); bga_w(8, 0); bga_w(9, 0);
    bga_w(4, 0x01 | 0x40);                       /* enabled + linear framebuffer */
    if (bga_r(1) != w || bga_r(2) != h) return 0;
    u16 vw = bga_r(6);
    return (u32)(vw ? vw : w) * 4u;
}

/* ---- PS/2 controller, keyboard + mouse (with IntelliMouse wheel) ---- */
static int mouse_pkt = 3;
static int wait_in(void)  { for (int i = 0; i < 200000; i++) if (!(inb(0x64) & 2)) return 1; return 0; }
static int wait_out(void) { for (int i = 0; i < 200000; i++) if (inb(0x64) & 1)   return 1; return 0; }
static void mouse_cmd(u8 c) {
    wait_in(); outb(0x64, 0xD4);
    wait_in(); outb(0x60, c);
    if (wait_out()) inb(0x60);            /* swallow ACK */
}
static void ps2_init(void) {
    while (inb(0x64) & 1) inb(0x60);       /* flush */
    wait_in(); outb(0x64, 0xA8);           /* enable aux port */
    wait_in(); outb(0x64, 0x20);
    wait_out(); u8 st = inb(0x60);
    st &= ~0x20;                           /* enable aux clock */
    wait_in(); outb(0x64, 0x60);
    wait_in(); outb(0x60, st);
    mouse_cmd(0xF6);                       /* defaults */
    /* magic knock for the scroll wheel (IntelliMouse): rates 200, 100, 80 */
    mouse_cmd(0xF3); mouse_cmd(200);
    mouse_cmd(0xF3); mouse_cmd(100);
    mouse_cmd(0xF3); mouse_cmd(80);
    mouse_cmd(0xF2);                       /* get device id */
    if (wait_out() && inb(0x60) == 3) mouse_pkt = 4;
    mouse_cmd(0xF3); mouse_cmd(100);
    mouse_cmd(0xF4);                       /* enable streaming */
    while (inb(0x64) & 1) inb(0x60);
}
#endif

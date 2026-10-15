/* disk.h - ATA PIO driver (legacy IDE, LBA28).  Probes the primary and
 * secondary buses for the first ATA hard disk (CD-ROMs/ATAPI are skipped). */
#ifndef NOVA_DISK_H
#define NOVA_DISK_H
#include "core.h"

typedef struct { u16 io, ctl; int slave; u32 sectors; int ok; } AtaDev;
static AtaDev ata;
static int disk_err;                         /* set after any failed transfer */
static u32 disk_err_count;                   /* increments on every failed transfer (lets callers detect failures) */

static void ata_delay(void) { for (int i = 0; i < 4; i++) inb(ata.ctl); }   /* ~400ns */
/* wait for BSY to clear (and for DRQ if asked); -1 on error/timeout */
static int ata_wait(int drq) {
    for (int i = 0; i < 2000000; i++) {
        u8 s = inb(ata.io + 7);
        if (s & 0x80) continue;
        if (s & 0x21) return -1;             /* ERR or device fault */
        if (!drq || (s & 0x08)) return 0;
    }
    return -1;
}
static int ata_identify(u16 io, u16 ctl, int slave, u32 *sectors) {
    outb(io + 6, 0xA0 | (slave << 4));
    for (int i = 0; i < 4; i++) inb(ctl);
    outb(io + 2, 0); outb(io + 3, 0); outb(io + 4, 0); outb(io + 5, 0);
    outb(io + 7, 0xEC);                      /* IDENTIFY DEVICE */
    u8 s = inb(io + 7);
    if (s == 0 || s == 0xFF) return 0;       /* nothing there */
    int i;
    for (i = 0; i < 2000000; i++) { s = inb(io + 7); if (!(s & 0x80)) break; }
    if (s & 0x80) return 0;
    if (inb(io + 4) || inb(io + 5)) return 0;        /* ATAPI / SATA signature: not for us */
    for (i = 0; i < 2000000; i++) { s = inb(io + 7); if (s & 0x08) break; if (s & 0x01) return 0; }
    if (!(s & 0x08)) return 0;
    u16 id[256];
    for (int k = 0; k < 256; k++) id[k] = inw(io);
    u32 n = id[60] | ((u32)id[61] << 16);
    if (n < 2048) return 0;
    *sectors = n;
    return 1;
}
static int disk_init(void) {
    static const u16 io[2] = { 0x1F0, 0x170 }, ctl[2] = { 0x3F6, 0x376 };
    for (int b = 0; b < 2; b++) for (int sl = 0; sl < 2; sl++) {
        u32 n;
        if (ata_identify(io[b], ctl[b], sl, &n)) {
            ata.io = io[b]; ata.ctl = ctl[b]; ata.slave = sl; ata.sectors = n; ata.ok = 1;
            outb(ctl[b], 2);                 /* nIEN: we poll, no interrupts */
            return 1;
        }
    }
    return 0;
}
static int ata_xfer(u32 lba, int count, void *buf, int wr) {        /* count 1..128 */
    outb(ata.io + 6, 0xE0 | (ata.slave << 4) | ((lba >> 24) & 0x0F));
    ata_delay();
    outb(ata.io + 2, (u8)count);
    outb(ata.io + 3, (u8)lba); outb(ata.io + 4, (u8)(lba >> 8)); outb(ata.io + 5, (u8)(lba >> 16));
    outb(ata.io + 7, wr ? 0x30 : 0x20);
    u8 *p = buf;
    for (int i = 0; i < count; i++, p += 512) {
        ata_delay();
        if (ata_wait(1)) return -1;
        if (wr) outs16(ata.io, p, 256); else ins16(ata.io, p, 256);
    }
    if (wr) { ata_delay(); if (ata_wait(0)) return -1; }
    return 0;
}
static int disk_rw(u32 lba, u32 count, void *buf, int wr) {
    u8 *p = buf;
    if (!ata.ok || lba + count > ata.sectors) { disk_err = 1; disk_err_count++; return -1; }
    while (count) {
        u32 n = count > 128 ? 128 : count;
        if (ata_xfer(lba, (int)n, p, wr)) { disk_err = 1; disk_err_count++; return -1; }
        lba += n; count -= n; p += n * 512;
    }
    return 0;
}
static int disk_read(u32 lba, u32 count, void *buf)        { return disk_rw(lba, count, buf, 0); }
static int disk_write(u32 lba, u32 count, const void *buf) { return disk_rw(lba, count, (void *)buf, 1); }
static void disk_flush(void) {                                /* FLUSH CACHE */
    if (!ata.ok) return;
    outb(ata.io + 6, 0xE0 | (ata.slave << 4)); ata_delay();
    outb(ata.io + 7, 0xE7);
    ata_delay();
    if (ata_wait(0)) { disk_err = 1; disk_err_count++; }
}
#endif

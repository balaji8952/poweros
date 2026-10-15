/* Emulated primary-master IDE disk for the host test harnesses (drives the real ATA driver in disk.h).
 * Fault injection: emu_fail_writes / emu_fail_reads make commands report ERR; emu_corrupt_writes silently
 * flips a bit in the data written (a disk that lies) - used to prove read-after-write verification works. */
#ifndef IDE_EMU_H
#define IDE_EMU_H
static u8 *emu_img; static u32 emu_secs;
static int emu_fail_writes, emu_fail_reads, emu_corrupt_writes, emu_present = 1;
static u32 emu_write_cmds;
static struct { u8 sel, cnt, l0, l1, l2, err; int drq, wr, idx, total; u32 lba; u16 xb[256 * 256]; } E;
static u32 emu_in(u16 port, int size) {
    (void)size;
    if (!emu_present) return 0xFF;
    if (port >= 0x170 && port < 0x178) return 0xFF;
    if (port == 0x376) return 0xFF;
    if (E.sel & 0x10) return 0;
    switch (port) {
    case 0x1F7: case 0x3F6: return 0x50 | (E.drq ? 8 : 0) | (E.err ? 1 : 0);
    case 0x1F4: case 0x1F5: return 0;
    case 0x1F0: { u16 w = E.drq ? E.xb[E.idx++] : 0; if (E.drq && E.idx >= E.total) E.drq = 0; return w; }
    }
    return 0;
}
static void emu_out(u16 port, u32 v, int size) {
    (void)size;
    if (!emu_present || port < 0x1F0 || port > 0x1F7) { if (port == 0x1F6) E.sel = (u8)v; return; }
    switch (port) {
    case 0x1F6: E.sel = (u8)v; break;
    case 0x1F2: E.cnt = (u8)v; break;
    case 0x1F3: E.l0 = (u8)v; break;
    case 0x1F4: E.l1 = (u8)v; break;
    case 0x1F5: E.l2 = (u8)v; break;
    case 0x1F0:
        if (E.drq && E.wr) {
            E.xb[E.idx++] = (u16)v;
            if (E.idx >= E.total) {
                if (emu_corrupt_writes && E.lba >= 160) E.xb[7] ^= 0x0100;       /* data area only: flip a bit */
                memcpy(emu_img + (size_t)E.lba * 512, E.xb, (size_t)E.total * 2);
                E.drq = 0;
            }
        }
        break;
    case 0x1F7: {
        E.err = 0; E.drq = 0; E.idx = 0;
        u32 cnt = E.cnt ? E.cnt : 256;
        E.lba = E.l0 | (E.l1 << 8) | (E.l2 << 16) | ((u32)(E.sel & 15) << 24);
        if (v == 0xEC) {
            if (E.sel & 0x10) break;
            memset(E.xb, 0, 512); E.xb[60] = (u16)emu_secs; E.xb[61] = (u16)(emu_secs >> 16);
            E.total = 256; E.wr = 0; E.drq = 1;
        } else if (v == 0x20) {
            if (emu_fail_reads || E.lba + cnt > emu_secs) { E.err = 1; break; }
            memcpy(E.xb, emu_img + (size_t)E.lba * 512, (size_t)cnt * 512);
            E.total = (int)cnt * 256; E.wr = 0; E.drq = 1;
        } else if (v == 0x30) {
            emu_write_cmds++;
            if (emu_fail_writes || E.lba + cnt > emu_secs) { E.err = 1; break; }
            E.total = (int)cnt * 256; E.wr = 1; E.drq = 1;
        }
    } break;
    }
}
static void attach_emu(u32 secs) {
    free(emu_img);
    emu_secs = secs; emu_img = calloc(secs, 512); emu_present = 1;
    emu_fail_writes = emu_fail_reads = emu_corrupt_writes = 0;
    hk_in = emu_in; hk_out = emu_out; memset(&E, 0, sizeof E);
}
static void detach_emu(void) { hk_in = 0; hk_out = 0; }
#endif

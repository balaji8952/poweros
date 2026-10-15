/* ===========================================================================
 *  NovaOS 0.2 - hobby OS: framebuffer GUI with a macOS-style dock, in-RAM
 *  file system (create / edit / rename / delete), Notepad, Photos (BMP, PNG,
 *  JPEG), live resolution switching.  Boots via GRUB (Multiboot 1).
 *
 *  Layout:  core.h  types, libc bits, heap       hw.h     RTC/PIT/VGA/PS2
 *           gfx.h   drawing + scaling            image.h  BMP/PNG/JPEG
 *           fs.h    file system                  ui_base.h/apps.h/shell.h  GUI
 * ======================================================================== */
#include "shell.h"

typedef struct {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4];
    u32 mmap_length, mmap_addr, drives_length, drives_addr, config_table;
    u32 boot_loader_name, apm_table, vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    u32 fb_addr_lo, fb_addr_hi, fb_pitch, fb_width, fb_height;
    u8  fb_bpp, fb_type;
} __attribute__((packed)) MBI;

#ifndef NOVA_HOST
static void text_fail(const char *msg) {
    volatile u16 *vga = (volatile u16 *)0xB8000;
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0720;
    for (int i = 0; msg[i]; i++) vga[i] = 0x4f00 | (u8)msg[i];
    for (;;) __asm__ volatile("cli; hlt");
}

void kmain(u32 magic, MBI *mbi) {
    if (magic != 0x2BADB002 || !(mbi->flags & (1 << 12)) || mbi->fb_type != 1 || mbi->fb_bpp != 32)
        text_fail("NovaOS: GRUB did not provide a 32-bit linear framebuffer.");
    fbp = (u32 *)mbi->fb_addr_lo;
    fbpitch = mbi->fb_pitch;
    W = mbi->fb_width  > MAXW ? MAXW : (int)mbi->fb_width;     /* any resolution GRUB gives us */
    H = mbi->fb_height > MAXH ? MAXH : (int)mbi->fb_height;

    bga_probe();                      /* enables live mode switching when available */
    pit_init();
    fs_boot();                        /* mounts the ATA disk if there is one */
    pkg_ui_init();                    /* application-type handlers */
    pkg_system_init();                /* /System/Installed Applications + registry scan */
    wp_apply();
    fm_cwd = fs_lookup(0, "Documents"); if (fm_cwd < 0) fm_cwd = 0;
    fm_refresh();
    open_app(APP_FILES);
    int w = fs_lookup(fm_cwd, "Welcome.txt");
    if (w > 0) open_notepad_file(w);
    ps2_init();
    mx = W / 2; my = H / 2;
    if (fs_state == 2)
        dlg_confirm(DK_FORMAT, "Format disk?", "A disk was found that is not formatted for NovaOS. Format it? ALL DATA ON IT WILL BE ERASED.", 0);

    present_reset();
    clock_refresh(1);
    int tick = 0;
    render_full();
    for (;;) {
        u8 st;
        while ((st = inb(0x64)) & 1) {                 /* drain every pending keyboard/mouse byte first ... */
            u8 d = inb(0x60);
            if (st & 0x20) mouse_byte(d); else key_byte(d);
        }
        if ((++tick & 0xFF) == 0) clock_refresh(0);    /* cheap: uses the TSC, reads the RTC once a second */
        ui_frame();                                    /* ... then do the cheapest redraw that is correct */
        __asm__ volatile("pause");
    }
}
#endif

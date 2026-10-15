# NovaOS 0.2

Build (Linux): `sudo apt install gcc-multilib grub-pc-bin xorriso mtools python3-pil python3-numpy fonts-dejavu-core && ./build.sh`
No grub-mkrescue? `python3 patch_iso.py OLD.iso kernel.elf grub.cfg NovaOS.iso`
Run (with persistence):
    qemu-img create -f raw disk.img 64M          # once
    qemu-system-i386 -cdrom NovaOS.iso -hda disk.img -boot d -m 128 -vga std
(needs >= 96 MB RAM, ~64 MB is used; `-vga std` enables live resolution switching). Without `-hda` NovaOS runs from RAM only.
VirtualBox: attach the virtual disk to an **IDE (PIIX4)** controller - SATA/AHCI is not supported.

## What's new
* **File system** (RAM): Files app - New File / New Folder / Rename / Delete / Open, folders, sidebar, sorting, scrolling.
  Keys: Enter open, F2 rename, Del delete, Backspace up, Ctrl+N new file, Ctrl+Shift+N new folder.
* **Notepad** edits real files: Ctrl+S save, Ctrl+Shift+S Save As, unsaved-changes prompts.
* **Dock** (macOS style, magnification, running dots, tooltips) + top menu bar with the N menu (About / Display / Restart / Shut Down).
  Windows have traffic-light buttons, double-click title = zoom, drag bottom-right corner to resize.
* **Dynamic resolution**: any framebuffer GRUB provides is used (up to 1920x1200, edit MAXW/MAXH in gfx.h);
  Display app switches mode live on QEMU/Bochs/VirtualBox VGA; GRUB menu has 800x600 ... 1920x1080 entries for real hardware.
* **Images**: Photos opens BMP (1/4/8/16/24/32-bit), PNG (all colour types/depths, alpha) and baseline JPEG; prev/next, set wallpaper.
  Drop files in `assets/` before building: images -> /Pictures (anything PIL reads; auto-converted), other files -> /Documents.

* **Disk persistence**: ATA PIO driver (legacy IDE, LBA28) + NovaFS (superblock, node table, FAT, 4 KB clusters).
  Every create/rename/delete/save is written straight to disk. A blank disk is formatted automatically on first boot;
  a disk holding other data triggers a "Format?" prompt instead (Cancel = RAM only). Uses up to the first 256 MB of the disk.
  The first (ATA) disk found is used; CD-ROMs are ignored.

Limits: IDE only (no SATA/AHCI/NVMe/USB); not journaled, so a power cut mid-write can lose that one change;
without a disk, files live in RAM (lost on restart); progressive/interlaced images must go through `assets/` (auto-converted); images up to 3 MP.
`test_disk.c` runs the real ATA driver + NovaFS against an emulated IDE controller (reboots, disk-full, I/O errors, fsck).
`test.c` is a host harness that runs the real GUI code natively: `gcc -DNOVA_HOST -Ibuild -I. test.c build/assets_host.c`.

## 0.3 - smoother UI, Start menu, date
* **Start menu** (menu bar, left): launch Files / Notepad / Photos / Display / About, plus Restart and Shut Down.
* **Date + time** in the menu bar ("Mon 12 Oct 14:05"); click it for a calendar with month navigation.
  The clock reads the PC's RTC (QEMU: add `-rtc base=localtime` for local time instead of UTC).
* **Performance**: the cursor moves without redrawing the scene; hovering the dock redraws only the dock;
  only changed pixels are written to video memory; the clock uses the CPU's TSC (calibrated at boot) and the RTC is read once a second.
  Hover highlights no longer leak through overlapping windows.
  In QEMU on Windows add `-accel whpx` (enable "Windows Hypervisor Platform") or `-accel tcg,thread=multi` for much better speed.
* `test_fast.c` fuzzes the redraw fast paths and checks every step is pixel-identical to a full redraw.

## 0.4 - package installer + application launcher
Docs: `docs/NOVAPKG_FORMAT.md` (format, limits, security model, why there is no native-code loader).
* Open a `.powor` / `.appd` in **Files** -> confirm -> installed as `/System/Installed Applications/<id>.power`.
  Samples ship in /Documents (Hello.powor, Manual.appd, Photos Shortcut.appd); existing disks get them on first boot of this version.
* **Start menu** has an *Installed* section built from the registry; hover an entry and press **x** to uninstall (with confirmation).
* Application types: `textview` (text window), `builtin` (shortcut to Files/Notepad/Photos/Display/About). Packages are data - never executed.
* Build a package: `python3 tools/mkpkg.py build out.powor --id my.app --name "My App" --version 1.0.0 --type textview --data notes.txt`
  Validate: `python3 tools/mkpkg.py check out.powor`.
* Tests (native, AddressSanitizer/UBSan + plain/poisoned builds + 32-bit kernel build): `tests/run_tests.sh`

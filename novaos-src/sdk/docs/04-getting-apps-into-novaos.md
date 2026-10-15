# 4. Getting apps into NovaOS

NovaOS has no network, USB or clipboard yet, so files reach it through its **disk image**. `tools/novadisk.py` reads
and writes that image from your PC.

> **Always close NovaOS/QEMU before running novadisk.** Editing an image that is in use will corrupt it.

## Make a disk

```
python tools/novadisk.py create disk.img --size 64M
```

Any size from 1 MB works; NovaOS uses at most the first 256 MB. A blank file that NovaOS has never seen is also fine:
NovaOS formats a completely blank disk itself on first boot (and asks before touching a disk that holds other data).
novadisk refuses to touch images that contain unknown data.

## Install apps straight into the disk

```
python tools/novadisk.py install disk.img examples/dist/*.powor examples/dist/*.appd
python tools/novadisk.py install disk.img my-app.powor --update      # replace an older version
python tools/novadisk.py apps disk.img                                 # list what is installed
python tools/novadisk.py uninstall disk.img my.app
```

These validate the package with the kernel's rules, refuse duplicates, write
`/System/Installed Applications/<id>.power`, read it back to check it, and verify the whole file system.

## Or hand NovaOS the package and let it install it

```
python tools/novadisk.py put disk.img my-app.powor /Documents
```

Then in NovaOS: Files -> Documents -> double-click `my-app.powor` -> Install. (This is the same path a user takes.)

## Other file commands

```
python tools/novadisk.py ls disk.img /Documents
python tools/novadisk.py put disk.img photo.png /Pictures          # PNG/JPEG/BMP show up in Photos
python tools/novadisk.py get disk.img /Documents/notes.txt notes.txt
python tools/novadisk.py mkdir disk.img /Documents/Projects
python tools/novadisk.py rm disk.img /Documents/old.txt
python tools/novadisk.py check disk.img
```

## Starting NovaOS with the disk

```
qemu-system-i386 -cdrom NovaOS.iso -hda disk.img -boot d -m 128 -vga std -rtc base=localtime
```

(`scripts/run-qemu.bat` / `.sh` run exactly this.) Add `-accel whpx` on Windows for speed if the Windows Hypervisor
Platform feature is on. NovaOS reads IDE disks only: in VirtualBox attach the image to the **IDE (PIIX4)** controller.

## Baking apps into the OS image itself (advanced)

If you build NovaOS from source, drop `.powor`/`.appd` files into the source tree's `assets/` folder before running
`build.sh`; they appear in `/Documents` on a freshly formatted disk. For everyone else the disk-image route above is
simpler and doesn't need a rebuild.

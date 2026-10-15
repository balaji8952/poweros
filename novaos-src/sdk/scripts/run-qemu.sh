#!/bin/sh
cd "$(dirname "$0")/.." || exit 1
exec qemu-system-i386 -cdrom NovaOS.iso -hda disk.img -boot d -m 128 -vga std -rtc base=localtime "$@"

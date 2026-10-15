#!/bin/bash
# Builds NovaOS.iso  (needs: gcc-multilib, grub-pc-bin, xorriso, mtools, python3+PIL+numpy)
# Drop pictures/files into ./assets to embed them (images -> /Pictures, others -> /Documents).
set -e
cd "$(dirname "$0")"
python3 gen_assets.py
CFLAGS="-m32 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns \
        -mno-sse -mno-mmx -mno-80387 -Wall -Wextra -Wno-unused-parameter -Ibuild -I."
gcc $CFLAGS -c kernel.c -o build/kernel.o
gcc -m32 -c boot.s -o build/boot.o
gcc -m32 -c build/assets.s -o build/assets.o
ld -m elf_i386 -nostdlib -T linker.ld -o kernel.elf build/boot.o build/kernel.o build/assets.o
grub-file --is-x86-multiboot kernel.elf && echo "multiboot header OK"
rm -rf iso && mkdir -p iso/boot/grub
cp kernel.elf iso/boot/kernel.elf
cp grub.cfg iso/boot/grub/grub.cfg
grub-mkrescue -o NovaOS.iso iso 2>&1 | tail -2
ls -la NovaOS.iso

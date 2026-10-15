/* NovaOS multiboot entry: asks GRUB for a linear framebuffer (default 1024x768x32;
 * the GRUB menu entries override it with gfxpayload) */
.set MAGIC,  0x1BADB002
.set FLAGS,  (1<<0) | (1<<1) | (1<<2)
.set CHECK,  -(MAGIC + FLAGS)

.section .multiboot, "a"
.align 4
.long MAGIC
.long FLAGS
.long CHECK
.long 0, 0, 0, 0, 0
.long 0          /* linear graphics mode */
.long 1024
.long 768
.long 32

.section .bss
.align 16
stack_bottom:
.skip 65536
stack_top:

.section .text
.global _start
_start:
    cli
    mov $stack_top, %esp
    push %ebx
    push %eax
    call kmain
1:  hlt
    jmp 1b
.section .note.GNU-stack,"",@progbits

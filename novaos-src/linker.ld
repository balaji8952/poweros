ENTRY(_start)
SECTIONS
{
    . = 1M;
    .multiboot : { *(.multiboot) }
    .text      : { *(.text*) }
    .rodata    : ALIGN(4096) { *(.rodata*) }
    .data      : { *(.data*) }
    .bss       : { *(COMMON) *(.bss*) }
    /DISCARD/  : { *(.eh_frame) *(.note*) *(.comment) }
}

/*
 * Memory map (from Kobo's own source, kobolabs/b300-sage):
 *   0x40000000  start of DRAM
 *   0x50000000 - 0x58000000  reserved for the e-ink engine's buffers
 *                            (sun8iw15p1-eink_128m.dtsi, /memreserve/)
 * We load at 0x48000000: inside DRAM, below the e-ink buffers, and away from
 * where Linux and U-Boot normally put themselves.
 */
ENTRY(_start)

SECTIONS
{
    . = 0x48000000;
    .text : { KEEP(*(.text.boot)) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(4);
    _image_end = .;
    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    }
    . = ALIGN(16);
    . += 0x10000;           /* 64 KiB stack */
    _stack_top = .;
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}

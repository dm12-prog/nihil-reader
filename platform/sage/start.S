/*
 * Entry point for the Kobo Sage (Allwinner B300 / sun8iw15p1, 4x Cortex-A7, ARMv7-A).
 *
 * The first 0x30 bytes look like a Linux ARM zImage header. That is on purpose:
 * every tool that already knows how to start Linux on this device (kexec from the
 * running Kobo system, U-Boot "bootz", an Android boot.img via fastboot) will also
 * start us, without anyone having to write to the Kobo's storage.
 *
 * Linux boot protocol on entry: MMU off, caches off, r0 = 0, r1 = machine id,
 * r2 = device tree address. We ignore r1/r2 for now.
 */
    .syntax unified
    .arm
    .section .text.boot, "ax"
    .global _start

_start:
    .rept 8
    mov     r0, r0                  @ 8 no-ops, same as a real zImage
    .endr
    b       reset                   @ offset 0x20
    .word   0x016f2818              @ offset 0x24: zImage magic number
    .word   _start                  @ offset 0x28: image start address
    .word   _image_end              @ offset 0x2c: image end address

reset:
    cpsid   if                      @ no interrupts yet
    mrc     p15, 0, r4, c0, c0, 5   @ MPIDR: which CPU core are we?
    ands    r4, r4, #3
    bne     park                    @ only core 0 runs; others sleep forever

    /* Move ourselves to the address we were linked for, if a loader put us
     * somewhere else (the Linux chainloader drops us wherever it found free
     * memory). Only position-independent code until "relocated". */
    adr     r0, _start              @ r0 = where we are now
    ldr     r1, =_start             @ r1 = where we should be
    cmp     r0, r1
    beq     relocated
    ldr     r2, =_image_end
    sub     r2, r2, r1              @ r2 = image size (multiple of 4)
    blo     copy_down               @ r0 < r1: copy from the end, overlap-safe
copy_up:
    ldr     r3, [r0], #4
    str     r3, [r1], #4
    subs    r2, r2, #4
    bne     copy_up
    b       copied
copy_down:
    add     r0, r0, r2
    add     r1, r1, r2
1:  ldr     r3, [r0, #-4]!
    str     r3, [r1, #-4]!
    subs    r2, r2, #4
    bne     1b
copied:
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0   @ forget any stale instructions (ICIALLU)
    dsb
    isb
    ldr     pc, =relocated          @ jump into the moved copy
relocated:
    ldr     sp, =_stack_top

    ldr     r0, =_bss_start         @ zero the .bss section
    ldr     r1, =_bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    bl      main                    @ C code takes over
park:
    wfi
    b       park

! Entry point. The BIOS loads 0MAIN.BIN at 0x06004000 and jumps here.

    .section .text.start,"ax"
    .global start
start:
    mov.l   stack_top, r15

    ! CCR = 0x11: purge and enable the cache
    mov.l   ccr, r1
    mov     #0x11, r0
    mov.b   r0, @r1

    ! zero .bss
    mov.l   bss_start, r1
    mov.l   bss_end, r2
    mov     #0, r0
1:  cmp/hs  r2, r1
    bt      2f
    mov.l   r0, @r1
    bra     1b
    add     #4, r1

    ! run static constructors
2:  mov.l   init_start, r8
    mov.l   init_end, r9
3:  cmp/hs  r9, r8
    bt      4f
    mov.l   @r8+, r0
    jsr     @r0
    nop
    bra     3b
    nop

4:  mov.l   main, r0
    jsr     @r0
    nop
5:  bra     5b
    nop

    .align 2
stack_top:  .long __stack_top
ccr:        .long 0xFFFFFE92
bss_start:  .long __bss_start
bss_end:    .long __bss_end
init_start: .long __init_array_start
init_end:   .long __init_array_end
main:       .long _main

! The slave starts here (platform.cpp slaveStart), interrupts masked at once.
    .text
    .global _slaveEntry
    .align 2
_slaveEntry:
    mov.l   6f, r0
    ldc     r0, sr
    mov.l   7f, r0
    jmp     @r0
    nop

    .align 2
6:  .long 0xF0
7:  .long _slaveMain

! Slave interrupts that may arrive before slaveEntry masks them.
    .global _slaveIgnoreIrq
    .align 2
_slaveIgnoreIrq:
    rte
    nop

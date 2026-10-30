! Entry point. The BIOS loads 0MAIN.BIN at 0x06004000 and jumps here (the
! DSP bench's, dspbench/crt0.s).

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

! The SCU's DSP end interrupt on the master (platform.cpp dspEndHandler),
! entered from the BIOS's dispatcher as a subroutine: the registers C may use
! saved around the handler.
    .text
    .global _dspEndEntry
    .align 2
_dspEndEntry:
    mov.l   r0, @-r15
    mov.l   r1, @-r15
    mov.l   r2, @-r15
    mov.l   r3, @-r15
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   r6, @-r15
    mov.l   r7, @-r15
    sts.l   pr, @-r15
    sts.l   mach, @-r15
    sts.l   macl, @-r15
    stc.l   gbr, @-r15
    mov.l   8f, r0
    jsr     @r0
    nop
    ldc.l   @r15+, gbr
    lds.l   @r15+, macl
    lds.l   @r15+, mach
    lds.l   @r15+, pr
    mov.l   @r15+, r7
    mov.l   @r15+, r6
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    mov.l   @r15+, r3
    mov.l   @r15+, r2
    mov.l   @r15+, r1
    rts
    mov.l   @r15+, r0

    .align 2
8:  .long _dspEndHandler

! Slave interrupts that may arrive before slaveEntry masks them.
    .global _slaveIgnoreIrq
    .align 2
_slaveIgnoreIrq:
    rte
    nop

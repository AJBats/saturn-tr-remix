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

2:  mov.l   main, r0
    jsr     @r0
    nop
3:  bra     3b
    nop

    .align 2
stack_top:  .long __stack_top
ccr:        .long 0xFFFFFE92
bss_start:  .long __bss_start
bss_end:    .long __bss_end
main:       .long _main

! C's signed divide on the SH-2's divider, as retail's (060457f6), in place of
! libgcc's 32 DIV1 steps (the divider takes 39 cycles, the steps 64).
!
! GCC calls ___sdivsi3 with the dividend in r4 and the divisor in r5, takes the
! quotient from r0 and expects only r1-r3, T and PR changed. A divide by 0
! gives 0, as libgcc's does. Each CPU has its own divider, but the master's
! main line and its interrupts share one: interrupt code must not divide.

    .text
    .align 2
    .global ___sdivsi3
    .type   ___sdivsi3, @function
___sdivsi3:
    tst     r5, r5
    bt      1f
    mov.l   2f, r1
    mov.l   r5, @r1                 ! DVSR
    mov.l   r4, @(4,r1)             ! DVDNT: a 32 / 32 divide starts
    rts
    mov.l   @(20,r1), r0            ! DVDNTL: the read waits out the divide
1:  rts
    mov     #0, r0

    .align 2
2:  .long   0xFFFFFF00
    .size   ___sdivsi3, . - ___sdivsi3

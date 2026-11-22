! The colour of saturn/mdec.cpp (mdecColour) in SH-2 assembly: a macroblock's 16x16 pixels from its six
! blocks after the IDCT (Cr, Cb, Y1..Y4, FRAC = 8 fraction bits), bit for bit the C's for indexes
! inside the clamp tables, which tools/fmvpack.py checks every frame keeps to (the C masks them to
! 1023 as well).
!
! void mdecColourAsm(int32_t blocks[6][64], uint32_t* dst, int pitch)
!
! For each 2x2 square one Cr and one Cb: terms R = Cr x 1.402, G = -(Cb x 0.3437) - Cr x 0.7143,
! B = Cb x 1.772 (each a DMULS.L >> 16 by XTRCT), each plus BIAS; then each of its four pixels is
! gMdecClamp[(Y + R) >> 8] | gMdecClamp[(Y + G) >> 8] << 8 | gMdecClampB[(Y + B) >> 8] << 16, the last
! table's 0x8000 | value putting VDP2's top bit in place by the same shift.
!
! Registers: r4 Cr, r8 Cb, r7 the square's Y, r5 its first pixel, r2 the pixel below, r6 the pitch
! in bytes, r9 r10 r11 the terms R G B, r12 gMdecClamp, r13 gMdecClampB, r14 squares left in the
! half row; r0 r1 r3 the pixel's work. The stack: +0 the square row, +4 the blocks.

    .section .text.mdecColourAsm, "ax"
    .align  2

    .macro  PIXEL ys, rd, ds
    mov.l   @(\ys, r7), r1      ! Y
    mov     r1, r0
    add     r10, r0
    shlr8   r0
    mov.b   @(r0, r12), r3      ! G
    mov     r1, r0
    add     r9, r0
    shlr8   r0
    extu.b  r3, r3
    shll8   r3
    mov.b   @(r0, r12), r0      ! R
    extu.b  r0, r0
    or      r0, r3
    mov     r1, r0
    add     r11, r0
    shlr8   r0
    add     r0, r0
    mov.w   @(r0, r13), r0      ! 0x8000 | B, sign-extended: the shift drops the top
    shll16  r0
    or      r3, r0
    mov.l   r0, @(\ds, \rd)
    .endm

    ! four squares of a half row
    .macro  SQUARES
    mov     #4, r14
1:  mov.l   @r4+, r1            ! Cr
    mov.l   .Lcr_r, r0
    dmuls.l r0, r1
    mov.l   .Lbias, r9
    sts     mach, r0
    sts     macl, r3
    xtrct   r0, r3
    add     r3, r9              ! R = Cr x 1.402 + BIAS
    mov.l   .Lcr_g, r0
    dmuls.l r0, r1
    mov.l   .Lbias, r10
    sts     mach, r0
    sts     macl, r3
    xtrct   r0, r3
    add     r3, r10
    mov.l   @r8+, r1            ! Cb
    mov.l   .Lcb_g, r0
    dmuls.l r0, r1
    mov.l   .Lbias, r11
    sts     mach, r0
    sts     macl, r3
    xtrct   r0, r3
    add     r3, r10             ! G = -(Cb x 0.3437) - Cr x 0.7143 + BIAS
    mov.l   .Lcb_b, r0
    dmuls.l r0, r1
    sts     mach, r0
    sts     macl, r3
    xtrct   r0, r3
    add     r3, r11             ! B = Cb x 1.772 + BIAS
    mov     r5, r2
    add     r6, r2
    PIXEL   0, r5, 0
    PIXEL   4, r5, 4
    PIXEL   32, r2, 0
    PIXEL   36, r2, 4
    add     #8, r7
    dt      r14
    bf/s    1b
    add     #8, r5
    .endm

    .global _mdecColourAsm
_mdecColourAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    mov.l   r4, @-r15           ! the blocks
    mov     #0, r0
    mov.l   r0, @-r15           ! the square row
    shll2   r6
    mov     #64, r8
    shll2   r8
    add     r4, r8              ! Cb, the next block
    mov.l   .Lclamp, r12
    mov.l   .Lclampb, r13

    ! a row of squares (two rows of pixels): Y from block 2 + (row >> 2) x 2, its line (row & 3) x 2
2:  mov.l   @r15, r0
    mov     r0, r1
    shlr2   r1
    shll8   r1
    shll    r1                  ! (row >> 2) x 512: two blocks on
    and     #3, r0
    shll2   r0
    shll2   r0
    shll2   r0                  ! (row & 3) x 64: two lines on
    mov.l   @(4, r15), r7
    add     r1, r7
    add     r0, r7
    mov     #64, r0
    shll2   r0
    shll    r0
    add     r0, r7              ! + 512: block 2
    SQUARES
    add     #112, r7
    add     #112, r7            ! the next Y block, the same lines
    SQUARES
    mov     r6, r0
    add     r0, r0
    add     #-64, r0
    add     r0, r5              ! two pixel rows down, back to the left
    mov.l   @r15, r0
    add     #1, r0
    mov.l   r0, @r15
    cmp/eq  #8, r0
    bt      3f
    bra     2b                  ! (a row is past BF's reach)
    nop

3:  add     #8, r15
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    .align  2
.Lclamp:    .long   _gMdecClamp
.Lclampb:   .long   _gMdecClampB
.Lbias:     .long   131200      ! (512 << 8) + 128: the tables' middle, and the rounding
.Lcr_r:     .long   91881       ! 1.402 x 65536
.Lcr_g:     .long   -46812      ! -0.7143 x 65536
.Lcb_g:     .long   -22525      ! -0.3437 x 65536
.Lcb_b:     .long   116130      ! 1.772 x 65536

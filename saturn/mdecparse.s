! The parsing of saturn/mdec.cpp (mdecParseBlock) in SH-2 assembly, a macroblock's six blocks a call:
! the same blocks, and the same bit reader state after each, for every stream tools/fmvpack.py writes
! (it checks no value reaches the saturation the C applies, which this leaves out).
!
! int mdecParseMbAsm(MdecBits* bits, const MdecCoef* coefs, int32_t blocks[6][64])
!
! Returns bit i set when block i has an AC value (else its IDCT is its DC everywhere).
!
! The SH-2 shifts by a register's amount only by multiplying: x << n is MUL.L by gMdecPow2[n] (2^n).
! A code is looked up by the next 8 bits in gMdecCodes8 (mdecInit): len x 4 | (run + 1) x 8 << 8 |
! level << 16, run + 1 of 0 for the end of block; len 0 for the escape (000001) and the codes of six
! or more leading zeros (gMdecLong) and the 9-bit codes 00100xxxs (gMdecCodes9: their sign the 9th bit).
!
! Registers: r8 the bits, top first (MdecBits.buf), r9 how many x 4, r10 the next 16 (MdecBits.p),
! r11 gMdecCodes8, r12 gMdecPow2, r13 this value's MdecCoef (coefs + k x 8), r14 coefs + 64 x 8,
! r6 the block, r5 the blocks' end, r7 the block's bit; r0..r4 the work. The stack: +0 the AC bits,
! +4 coefs, +8 bits.

    .equ    B_BUF,  0
    .equ    B_CNT,  4
    .equ    B_P,    8
    .equ    C_PRE,  0
    .equ    C_SCALE, 4
    .equ    C_RASTER, 6

    .section .text.mdecParseMbAsm, "ax"
    .align  2

    ! at least 17 bits in r8 (mdec.cpp's refill): a 16-bit word under the last bit, once or twice
    .macro  REFILL
    mov     #64, r0
    cmp/gt  r0, r9
    bt      9f
    sub     r9, r0
    mov.l   @(r0, r12), r1      ! 2^(16 - count)
    mov.w   @r10+, r2
    extu.w  r2, r2
    mul.l   r1, r2
    add     #64, r9
    sts     macl, r2
    or      r2, r8
    mov     #64, r0
    cmp/gt  r0, r9
    bt      9f
    sub     r9, r0
    mov.l   @(r0, r12), r1
    mov.w   @r10+, r2
    extu.w  r2, r2
    mul.l   r1, r2
    add     #64, r9
    sts     macl, r2
    or      r2, r8
9:
    .endm

    .global _mdecParseMbAsm
_mdecParseMbAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    mov.l   r4, @-r15           ! bits
    mov.l   r5, @-r15           ! coefs
    mov     #0, r0
    mov.l   r0, @-r15           ! the AC bits
    mov.l   @(B_BUF, r4), r8
    mov.l   @(B_CNT, r4), r9
    shll2   r9
    mov.l   @(B_P, r4), r10
    mov.l   .Lcodes, r11
    mov.l   .Lpow2, r12
    mov     #64, r14
    shll2   r14
    shll    r14
    add     r5, r14             ! coefs + 512
    mov     #96, r5
    shll2   r5
    shll2   r5
    add     r6, r5              ! blocks + 1536
    mov     #1, r7

.Lblock:
    mov     #0, r0              ! the block cleared
    mov     r6, r1
    mov     #4, r2
1:  mov.l   r0, @r1
    mov.l   r0, @(4, r1)
    mov.l   r0, @(8, r1)
    mov.l   r0, @(12, r1)
    mov.l   r0, @(16, r1)
    mov.l   r0, @(20, r1)
    mov.l   r0, @(24, r1)
    mov.l   r0, @(28, r1)
    mov.l   r0, @(32, r1)
    mov.l   r0, @(36, r1)
    mov.l   r0, @(40, r1)
    mov.l   r0, @(44, r1)
    mov.l   r0, @(48, r1)
    mov.l   r0, @(52, r1)
    mov.l   r0, @(56, r1)
    mov.l   r0, @(60, r1)
    dt      r2
    bf/s    1b
    add     #64, r1

    REFILL                      ! the DC: 10 bits, signed, x 64 (x 2 x 8, prescaled by 1)
    mov     r8, r0
    shlr16  r0
    exts.w  r0, r0
    mov     #-64, r1
    and     r1, r0
    mov.l   r0, @r6
    shll8   r8
    shll2   r8
    add     #-40, r9
    mov.l   @(4, r15), r13      ! k = 0

.Lcode:
    REFILL
    mov     r8, r0
    shlr16  r0
    shlr8   r0                  ! the next 8 bits
    shll2   r0
    mov.l   @(r0, r11), r1      ! the code
    extu.b  r1, r0              ! len x 4
    tst     r0, r0
    bt      .Lslow
    mov.l   @(r0, r12), r2
    mul.l   r2, r8              ! the bits after it
    sub     r0, r9
    mov     r1, r2
    shlr8   r2
    extu.b  r2, r2              ! (run + 1) x 8
    swap.w  r1, r1
    exts.w  r1, r1              ! level
    tst     r2, r2
    bt/s    .Lend               ! the end of block
    sts     macl, r8

    ! r1 level, r2 (run + 1) x 8. No saturation: tools/fmvpack.py checks no value reaches it.
.Lvalue:
    add     r2, r13             ! k += run + 1
    cmp/hs  r14, r13
    bt      .Lend               ! past the 64th value: a broken stream, the block ends
    mov.w   @(C_SCALE, r13), r0
    muls.w  r0, r1
    mov.l   @(C_PRE, r13), r2
    sts     macl, r1            ! level x scale
    dmuls.l r2, r1
    mov.w   @(C_RASTER, r13), r0
    shll2   r0
    sts     mach, r2
    sts     macl, r1
    xtrct   r2, r1
    bra     .Lcode
    mov.l   r1, @(r0, r6)       ! block[raster] = (value x prescale) >> 16

.Lend:
    mov.l   @(4, r15), r0
    cmp/eq  r0, r13
    bt      5f                  ! no AC value
    mov.l   @r15, r0
    or      r7, r0
    mov.l   r0, @r15
5:  shll    r7
    mov     #64, r0
    shll2   r0
    add     r0, r6
    cmp/hs  r5, r6
    bt      11f
    bra     .Lblock             ! (past BF's reach)
    nop

11: mov.l   @(8, r15), r4
    mov.l   r8, @(B_BUF, r4)
    shlr2   r9
    mov.l   r9, @(B_CNT, r4)
    mov.l   r10, @(B_P, r4)
    mov.l   @r15, r0
    add     #12, r15
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    ! the next 8 bits are 00100xxx (a 9-bit code: its sign the 9th bit), or 00000xxx: the escape
    ! (000001), or a code of 6 to 11 leading zeros
.Lslow:
    mov     r8, r4
    shlr16  r4
    shlr8   r4
    mov     #32, r2
    cmp/hs  r2, r4
    bt      .Lnine
    mov     #4, r2
    cmp/hs  r2, r4
    bt      .Lescape
    mov     #6, r4              ! zeros, up to the first 1 or 11 (mdec.cpp)
    mov.l   .Lbit25, r2
6:  tst     r2, r8
    bf      7f
    mov     #11, r0
    cmp/eq  r0, r4
    bt      7f
    add     #1, r4
    bra     6b
    shlr    r2
7:  mov     r4, r0              ! the bits after the 1: x (3 bits after 6 zeros, else 4), the sign
    add     #1, r0
    shll2   r0
    mov.l   @(r0, r12), r1
    mul.l   r1, r8
    mov     #6, r0
    cmp/eq  r0, r4
    sts     macl, r1
    mov     r1, r2
    shlr16  r2
    shlr8   r2
    shlr2   r2
    bt/s    8f
    shlr2   r2                  ! x = top 4 bits
    shll2   r1
    shll2   r1                  ! the sign on top
    mov     r4, r0
    bra     9f
    add     #6, r0              ! len = zeros + 4 + 2
8:  shlr    r2                  ! x = top 3 bits
    shll2   r1
    shll    r1
    mov     r4, r0
    add     #5, r0              ! len = 6 + 3 + 2
9:  shll2   r0
    sub     r0, r9
    mov.l   @(r0, r12), r3
    mul.l   r3, r8
    mov.l   .Llong, r3
    add     #-6, r4
    shll2   r4
    shll2   r4
    shll    r4
    add     r4, r3              ! gMdecLong[zeros - 6]
    mov     r2, r0
    add     r0, r0
    mov.w   @(r0, r3), r2       ! run << 10 | level
    sts     macl, r8
    mov.l   .L3ff, r3
    mov     r2, r0
    and     r3, r2              ! level
    cmp/pz  r1
    bt      10f
    neg     r2, r2
10: shlr8   r0
    shlr2   r0
    add     #1, r0
    shll2   r0
    shll    r0                  ! (run + 1) x 8
    mov     r2, r1
    bra     .Lvalue
    mov     r0, r2

.Lnine:
    mov.l   .Lcodes9, r2
    mov     r4, r0
    add     #-32, r0
    shll2   r0
    mov.l   @(r0, r2), r1       ! (run + 1) x 8 << 8 | level << 16, the level positive
    mov     r1, r2
    shlr8   r2
    extu.b  r2, r2
    swap.w  r1, r1
    exts.w  r1, r1
    mov.l   .Lbit23, r0
    tst     r0, r8
    bt      13f
    neg     r1, r1
13: shll8   r8
    shll    r8
    bra     .Lvalue
    add     #-36, r9            ! 9 bits

.Lescape:                       ! 000001, then 16 bits as they are: run << 10 | level (10 bits, signed)
    shll2   r8
    shll2   r8
    shll2   r8
    add     #-24, r9
    REFILL
    mov     r8, r0
    shlr16  r0
    shll16  r8
    add     #-64, r9
    mov     r0, r2
    shlr8   r2
    shlr2   r2
    add     #1, r2
    shll2   r2
    shll    r2                  ! (run + 1) x 8
    mov.l   .L3ff, r1
    and     r0, r1
    mov.l   .L200, r0
    tst     r0, r1
    bt      12f
    mov.l   .L400, r0
    sub     r0, r1
12: bra     .Lvalue             ! (past BT's reach)
    nop

    .align  2
.Lcodes:    .long   _gMdecCodes8
.Lcodes9:   .long   _gMdecCodes9
.Lpow2:     .long   _gMdecPow2
.Llong:     .long   _gMdecLong
.Lbit23:    .long   0x00800000
.Lbit25:    .long   0x02000000
.L3ff:      .long   0x3FF
.L200:      .long   0x200
.L400:      .long   0x400

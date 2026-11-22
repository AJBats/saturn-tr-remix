! The IDCT of saturn/mdec.cpp (mdecIdct) in SH-2 assembly: psx-spx's fast_idct_core in tools/mdec.py's
! fixed point, bit for bit. Each product is DMULS.L with its middle 32 bits taken by XTRCT: k x x >> 16,
! rounded down.
!
! void mdecIdctAsm(int32_t* block)
!
! Two passes, each over 8 vectors: vector i is in[i], in[8 + i] ... in[56 + i] (a column), its result
! out[8i] ... out[8i + 7]. The first pass goes from the block to a temp on the stack, the second back.
! A vector whose last 7 inputs are 0 is filled with its first.
!
! Registers in a pass: r4 the column's first input, r7 its fifth (r4 + 128), r5 the result's first,
! r6 the vectors left; the inputs s0..s7 in r8 r9 r10 r11 r12 r13 r14 r1, the rest in r0 r2 r3.

    .section .text.mdecIdctAsm, "ax"
    .align  2
    .global _mdecIdctAsm
_mdecIdctAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    mov.l   r4, @-r15           ! the block, for the second pass
    mov     #64, r0
    shll2   r0                  ! 256: the temp's size, and the block's place above it
    sub     r0, r15
    bsr     idctPass
    mov     r15, r5             ! (delay slot) first pass: block -> temp
    mov     #64, r0
    shll2   r0
    mov.l   @(r0, r15), r5      ! the block
    bsr     idctPass
    mov     r15, r4             ! (delay slot) second pass: temp -> block
    mov     #64, r0
    shll2   r0
    add     r0, r15
    add     #4, r15
    lds.l   @r15+, pr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

! One pass: r4 in, r5 out.
idctPass:
    mov     #8, r6
    mov     r4, r7
    add     #64, r7
    add     #64, r7
1:  mov.l   @r4, r8             ! s0
    mov.l   @(32, r4), r9       ! s1
    mov     #64, r0
    mov.l   @(r0, r4), r10      ! s2
    mov.l   @(r0, r7), r14      ! s6
    mov     #96, r0
    mov.l   @(r0, r4), r11      ! s3
    mov.l   @(r0, r7), r1       ! s7
    mov.l   @r7, r12            ! s4
    mov.l   @(32, r7), r13      ! s5
    mov     r9, r0
    or      r10, r0
    or      r11, r0
    or      r12, r0
    or      r13, r0
    or      r14, r0
    or      r1, r0
    tst     r0, r0
    bf      2f
    mov.l   r8, @r5             ! s1..s7 all 0: every result is s0
    mov.l   r8, @(4, r5)
    mov.l   r8, @(8, r5)
    mov.l   r8, @(12, r5)
    mov.l   r8, @(16, r5)
    mov.l   r8, @(20, r5)
    mov.l   r8, @(24, r5)
    bra     3f
    mov.l   r8, @(28, r5)

    ! the even half
2:  mov     r8, r2
    add     r12, r2             ! z10 = s0 + s4
    sub     r12, r8             ! z11 = s0 - s4
    mov     r10, r12
    add     r14, r12            ! z13 = s2 + s6
    sub     r14, r10            ! s2 - s6
    mov.l   .Lk1414, r14
    dmuls.l r14, r10
    sts     mach, r14
    sts     macl, r10
    xtrct   r14, r10
    sub     r12, r10            ! z12 = (s2 - s6) x 1.414 - z13
    mov     r2, r14
    add     r12, r14            ! t0 = z10 + z13
    sub     r12, r2             ! t3 = z10 - z13
    mov     r8, r12
    add     r10, r12            ! t1 = z11 + z12
    sub     r10, r8             ! t2 = z11 - z12

    ! the odd half
    mov     r11, r10
    add     r13, r10            ! z13 = s3 + s5
    sub     r13, r11            ! z10 = s3 - s5
    mov     r9, r13
    add     r1, r13             ! z11 = s1 + s7
    sub     r1, r9              ! z12 = s1 - s7
    mov     r9, r1
    sub     r11, r1
    mov.l   .Lk1847, r0
    dmuls.l r0, r1
    sts     mach, r0
    sts     macl, r1
    xtrct   r0, r1              ! z5 = (z12 - z10) x 1.848
    mov     r13, r0
    add     r10, r0             ! t7 = z11 + z13
    mov.l   .Lk2613, r3
    dmuls.l r3, r11
    sts     mach, r3
    sts     macl, r11
    xtrct   r3, r11
    add     r1, r11
    sub     r0, r11             ! t6 = z10 x 2.613 + z5 - t7
    sub     r10, r13
    mov.l   .Lk1414, r10
    dmuls.l r10, r13
    sts     mach, r10
    sts     macl, r13
    xtrct   r10, r13
    sub     r11, r13            ! t5 = (z11 - z13) x 1.414 - t6
    mov.l   .Lk1082, r10
    dmuls.l r10, r9
    sts     mach, r10
    sts     macl, r9
    xtrct   r10, r9
    sub     r1, r9
    add     r13, r9             ! t4 = z12 x 1.082 - z5 + t5

    ! t0 r14, t1 r12, t2 r8, t3 r2, t4 r9, t5 r13, t6 r11, t7 r0
    mov     r14, r1
    add     r0, r1
    mov.l   r1, @r5             ! t0 + t7
    sub     r0, r14
    mov.l   r14, @(28, r5)      ! t0 - t7
    mov     r12, r1
    add     r11, r1
    mov.l   r1, @(4, r5)        ! t1 + t6
    sub     r11, r12
    mov.l   r12, @(24, r5)      ! t1 - t6
    mov     r8, r1
    add     r13, r1
    mov.l   r1, @(8, r5)        ! t2 + t5
    sub     r13, r8
    mov.l   r8, @(20, r5)       ! t2 - t5
    mov     r2, r1
    sub     r9, r1
    mov.l   r1, @(12, r5)       ! t3 - t4
    add     r9, r2
    mov.l   r2, @(16, r5)       ! t3 + t4

3:  add     #4, r4
    add     #4, r7
    dt      r6
    bf/s    1b
    add     #32, r5
    rts
    nop

    .align  2
.Lk1414: .long  92682           ! 1.414213562 x 65536
.Lk1847: .long  121095          ! 1.847759065 x 65536
.Lk2613: .long  171254          ! 2.613125930 x 65536
.Lk1082: .long  70936           ! 1.082392200 x 65536

! The master's half of a mesh meshlight.dsp rotated and lit, as branch
! meshlight's lab/gamemeshxform.s meshProjectAsm has it (page_meshes.cpp).
!
! void meshProjectAsm(Vertex* v, int32 count, const uint16* divTable)
!   each vertex's x >> 14 << 16 | y >> 14 projected in place, as
!   lab/gamemeshxform.s's .LfsLoop does it

    .text
    .align 2
    .global _meshProjectAsm
_meshProjectAsm:
    mov.l   .LmpCentre, r7
.LmpLoop:
    mov.l   @(4, r4), r0        ! z >> 14 << 16 | shade << 8
    mov.l   @r4, r1
    shlr16  r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r6), r0       ! d
    exts.w  r1, r3              ! y
    shlr16  r1
    exts.w  r1, r1              ! x
    muls.w  r0, r1
    sts     macl, r1
    muls.w  r0, r3
    shlr8   r1
    sts     macl, r3
    shlr2   r1
    shlr2   r1
    exts.w  r1, r1
    shlr8   r3
    shlr2   r3
    shlr2   r3
    exts.w  r3, r3
    shll16  r1
    add     r3, r1              ! x << 16 + y, y borrowing from x when below 0,
    add     r7, r1              ! which the centre's carry repays
    mov.l   r1, @r4
    dt      r5
    bf/s    .LmpLoop
    add     #8, r4
    rts
    nop

    .align 2
.LmpCentre:     .long   0x00B00070  ! FRAME_WIDTH / 2 << 16 | FRAME_HEIGHT / 2

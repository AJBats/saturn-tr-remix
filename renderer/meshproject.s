! Stage 2 the new way (vertices.h): a mesh's vertices projected in place from
! what the DSP left there, as saturn/meshxform.s transformMeshAsm projects
! them after its multiplies; and the shade pass of a mesh shaded by its
! vertices' own shades.
!
! void meshProjectAsm(Vertex* v, int32 count, MeshProject* mp)
!   each vertex x >> 14 << 16 | y >> 14 & 0xFFFF, z >> 14 << 16 | shade << 8
!   (the DSP's, meshxform.s's sums >> FIXED_SHIFT): x and y projected, and
!   unless the mesh lies inside the frame, the clip flags; z >> 14 at the near
!   plane itself can't say on which side the vertex lies, so mp->exact
!   works that vertex out from the mesh
! void meshShadesAsm(Vertex* v, int32 count, const uint8* shades, int32 ambient)
!   each vertex's shade (ambient + its own << 5) >> 8, within 0 .. 31
!
! MeshProject (vertices.h):
!   +0  const uint16* divTable
!   +4  int32 inside            every vertex lands inside the frame and
!                               between the planes: no clamps or clip flags
!   +8  void (*exact)(MeshProject* mp, int32 index, Vertex* v)
!   +16 int32 farScaled         a vertex past the far plane scaled to it
!                               (vertices.h verticesFarScaled)
!   +20 const int16* divScaled  divTable << 4 from z 1024: a coordinate * this has
!                               the pixel in its high word, the frame's edge added
!                               there, and XTRCT packs the two
!
! Vertex (render.h): int16 x, y; int16 z; uint8 g, clip.

    .equ    CLIP_FRAME,     1
    .equ    CLIP_LEFT,      2
    .equ    CLIP_RIGHT,     4
    .equ    CLIP_TOP,       8
    .equ    CLIP_BOTTOM,    16
    .equ    CLIP_PLANE,     32
    .equ    VIEW_MIN,       64      ! VIEW_MIN_F >> FIXED_SHIFT

    .text
    .align 2
    .global _meshProjectAsm
_meshProjectAsm:
    tst     r5, r5
    bf      1f
    rts
    nop
1:  mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    mov.l   r5, @-r15           ! the count, for a vertex's index
    mov.l   @r6, r8             ! divTable
    mov.l   @(20, r6), r9       ! ... << 4
    mov.l   .Lcentre, r7
    mov.l   @(4, r6), r0
    tst     r0, r0
    bt      .Lreload
    mov.l   .LedgeX, r10
    mov.l   .LedgeY, r11
    mov     #64, r2

    ! inside the frame: x, y projected, the rest as the DSP left it
    .align  2
.Linside:
    mov.l   @(4, r4), r0        ! z >> 14 << 16 | shade << 8
    mov.l   @r4, r1
    shlr16  r0
    shlr2   r0
    shlr2   r0
    cmp/hs  r2, r0
    bf      .LinsideNear
    add     r0, r0
    mov.w   @(r0, r9), r0       ! d << 4
    muls.w  r0, r1              ! y
    swap.w  r1, r1
    sts     macl, r3
    muls.w  r0, r1              ! x
    add     r11, r3
    sts     macl, r1
    add     r10, r1
    swap.w  r1, r1
    xtrct   r1, r3
    mov.l   r3, @r4
    dt      r5
    bf/s    .Linside
    add     #8, r4
    bra     .Ldone
    nop

    ! up close d << 4 won't fit
.LinsideNear:
    add     r0, r0
    mov.w   @(r0, r8), r0       ! d
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
    add     r7, r1              ! which the centre's carry repays (y + 112 > 0)
    mov.l   r1, @r4
    dt      r5
    bf/s    .Linside
    add     #8, r4
    bra     .Ldone
    nop

    ! anywhere: from z 1024 to the far plane by the loop's way; r2 r3 what z is tested by
.Lreload:
    mov.l   .LzRange, r2
    mov     #4, r3
    shll8   r3                  ! z 1024
    .align  2
.Lfast:
    mov.l   @(4, r4), r11       ! z >> 14 << 16 | shade << 8
    mov.l   @r4, r12            ! x >> 14 << 16 | y >> 14
    mov     r11, r10
    shlr16  r10
    mov     r10, r0
    sub     r3, r0
    cmp/hs  r2, r0
    bt      .Lgeneral
    mov     r10, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r9), r1       ! d << 4
    muls.w  r1, r12             ! y
    swap.w  r12, r12
    sts     macl, r13
    muls.w  r1, r12             ! x
    mov.l   .LedgeY, r0
    sts     macl, r12
    add     r0, r13
    mov.l   .LedgeX, r0
    add     r0, r12
    mov.l   .LframeX, r0
    cmp/hi  r0, r12
    bt      .LoffX
    mov.l   .LframeY, r0
    cmp/hi  r0, r13
    bt      .LoffY
.Lstore:
    swap.w  r12, r12
    xtrct   r12, r13
    mov.l   r13, @r4
    dt      r5
    bf/s    .Lfast
    add     #8, r4
    bra     .Ldone
    nop

    ! off the frame: the sides it's past, clamped to what VDP1 can address
.LoffX:
    cmp/pz  r12
    bt      1f
    mov     #CLIP_FRAME + CLIP_LEFT, r14
    mov.l   .Llow, r0
    cmp/ge  r0, r12
    bt      2f
    bra     2f
    mov     r0, r12
1:  mov     #CLIP_FRAME + CLIP_RIGHT, r14
    mov.l   .Lhigh, r0
    cmp/gt  r0, r12
    bf      2f
    mov     r0, r12
2:  mov.l   .LframeY, r0
    cmp/hi  r0, r13
    bf      .LoffDone
    bra     .LoffYSide
    nop
.LoffY:
    mov     #CLIP_FRAME, r14
.LoffYSide:
    cmp/pz  r13
    bt      3f
    add     #CLIP_TOP, r14
    mov.l   .Llow, r0
    cmp/ge  r0, r13
    bt      .LoffDone
    bra     .LoffDone
    mov     r0, r13
3:  add     #CLIP_BOTTOM, r14
    mov.l   .Lhigh, r0
    cmp/gt  r0, r13
    bf      .LoffDone
    mov     r0, r13
.LoffDone:
    or      r14, r11
    bra     .Lstore
    mov.l   r11, @(4, r4)

    ! z >> 14 at the near plane: worked out in C, then on
.Lexact:
    mov.l   @r15, r0
    sub     r5, r0              ! the vertex's index
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   r6, @-r15
    mov.l   r7, @-r15
    mov     r0, r5
    mov     r4, r0
    mov     r6, r4
    mov     r0, r6
    mov.l   @(8, r4), r0
    jsr     @r0                 ! exact(mp, index, v)
    nop
    mov.l   @r15+, r7
    mov.l   @r15+, r6
    mov.l   @r15+, r5
    bra     .Lnext
    mov.l   @r15+, r4

.Lgeneral:
    mov.l   @(4, r4), r11       ! z >> 14 << 16 | shade << 8
    mov.l   @r4, r12            ! x >> 14 << 16 | y >> 14
    mov     r11, r0
    shlr16  r0
    exts.w  r0, r10             ! z >> 14
    mov     #0, r14             ! clip flags

    ! near and far planes: clamp z, flag the vertex
    mov     #VIEW_MIN, r1
    cmp/gt  r1, r10
    bt      1f
    cmp/eq  r1, r10
    bt      .Lexact
    mov     r1, r10
    mov     #CLIP_PLANE, r14
1:  mov.w   .LviewDist, r1
    cmp/ge  r1, r10
    bf      2f
    bsr     .Lfar
    nop
    mov.w   .LviewDist, r1
    mov     r1, r10
    mov     #CLIP_PLANE, r14
2:
    ! d = divTable[z >> PROJ_SHIFT]
    mov     r10, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r8), r0
    exts.w  r12, r13            ! y
    shlr16  r12
    exts.w  r12, r12            ! x

    ! screen x, y = (coordinate * d) >> 12, clamped, + centre
    muls.w  r0, r12
    sts     macl, r12
    muls.w  r0, r13
    mov.l   .LpMax, r1
    sts     macl, r13
    neg     r1, r2
    cmp/gt  r1, r12
    bf      3f
    mov     r1, r12
3:  cmp/ge  r2, r12
    bt      4f
    mov     r2, r12
4:  cmp/gt  r1, r13
    bf      5f
    mov     r1, r13
5:  cmp/ge  r2, r13
    bt      6f
    mov     r2, r13
6:  shlr8   r12
    shlr2   r12
    shlr2   r12
    exts.w  r12, r12
    mov.w   .LhalfWidth, r0
    add     r0, r12
    shlr8   r13
    shlr2   r13
    shlr2   r13
    exts.w  r13, r13
    mov.w   .LhalfHeight, r0
    add     r0, r13

    ! off the frame: flag, and clamp to what VDP1 can address
    mov.w   .LframeWidth, r0
    cmp/hi  r0, r12
    bt      7f
    mov.w   .LframeHeight, r0
    cmp/hi  r0, r13
    bf      12f
7:  add     #CLIP_FRAME, r14
    mov.w   .LcoordMax, r1
    neg     r1, r2
    add     #-1, r2             ! -1024
    cmp/gt  r1, r12
    bf      8f
    mov     r1, r12
8:  cmp/ge  r2, r12
    bt      9f
    mov     r2, r12
9:  cmp/gt  r1, r13
    bf      10f
    mov     r1, r13
10: cmp/ge  r2, r13
    bt      11f
    mov     r2, r13
11:
    ! which sides of the frame
    cmp/pz  r12
    bt      13f
    add     #CLIP_LEFT, r14
13: mov.w   .LframeWidth, r0
    cmp/gt  r0, r12
    bf      14f
    add     #CLIP_RIGHT, r14
14: cmp/pz  r13
    bt      15f
    add     #CLIP_TOP, r14
15: mov.w   .LframeHeight, r0
    cmp/gt  r0, r13
    bf      12f
    add     #CLIP_BOTTOM, r14
12:
    ! store x|y, z|g|clip
    shll16  r12
    extu.w  r13, r13
    or      r13, r12
    mov.l   r12, @r4
    shll16  r10
    extu.w  r11, r11            ! the shade, the DSP's clip byte 0
    or      r11, r10
    or      r14, r10
    mov.l   r10, @(4, r4)
.Lnext:
    dt      r5
    bt      .Ldone
    bra     .Lreload            ! the loop is too long for a conditional branch
    add     #8, r4

.Ldone:
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

    ! a vertex past the far plane (z r10) unless the original's way (+16 of
    ! the projection, r6): x and y (r12's halves) times VIEW_DIST / z, so that
    ! projected at the far plane they land where it does; r0 .. r3
.Lfar:
    mov.l   @(16, r6), r0
    tst     r0, r0
    bt      9f
    mov     #-1, r3
    shll8   r3                  ! the divider at 0xFFFFFF00
    mov.l   r10, @r3            ! DVSR: z
    mov.w   .LviewDist, r1
    mov     r12, r0
    shlr16  r0
    exts.w  r0, r0              ! x
    muls.w  r1, r0
    sts     macl, r0
    mov.l   r0, @(4, r3)        ! DVDNT: divides
    mov.l   @(4, r3), r2
    exts.w  r12, r0             ! y
    muls.w  r1, r0
    sts     macl, r0
    mov.l   r0, @(4, r3)
    mov.l   @(4, r3), r0
    shll16  r2
    extu.w  r0, r0
    or      r0, r2
    mov     r2, r12
9:  rts
    nop

    .global _meshShadesAsm
_meshShadesAsm:
    tst     r5, r5
    bt      9f
    add     #6, r4              ! the g byte
    mov     #31, r3
1:  mov.b   @r6+, r0
    extu.b  r0, r0
    shll2   r0
    shll2   r0
    shll    r0
    add     r7, r0
    shlr8   r0
    exts.w  r0, r0
    cmp/pz  r0
    bt      2f
    mov     #0, r0
2:  cmp/gt  r3, r0
    bf      3f
    mov     r3, r0
3:  mov.b   r0, @r4
    dt      r5
    bf/s    1b
    add     #8, r4
9:  rts
    nop

    .align 2
.Lcentre:       .long   0x00B00070  ! FRAME_WIDTH / 2 << 16 | FRAME_HEIGHT / 2
.LedgeX:        .long   176 << 16
.LedgeY:        .long   112 << 16
.LframeX:       .long   (353 << 16) - 1
.LframeY:       .long   (225 << 16) - 1
.Llow:          .long   -1024 << 16
.Lhigh:         .long   (1024 << 16) - 1
.LzRange:       .long   20480 - 1024
.LpMax:         .long   0x007FF000  ! projected 2047 << 12
.LhalfWidth:    .short  176         ! FRAME_WIDTH / 2
.LhalfHeight:   .short  112         ! FRAME_HEIGHT / 2
.LframeWidth:   .short  352         ! FRAME_WIDTH
.LframeHeight:  .short  224         ! FRAME_HEIGHT
.LcoordMax:     .short  1023        ! VDP1_COORD_MAX
.LviewDist:     .short  20480       ! VIEW_MAX_F >> FIXED_SHIFT

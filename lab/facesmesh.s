! Stage 3 the new way (faces.h): a mesh's faces straight to VDP1 commands.
!
! void facesMeshAsm(const FaceSet* set, const Vertex* vertices, int32 inside, CommandList* list)
!
! Each face facing the camera and not culled has its command and gouraud
! table written at once into the list, chained into its depth bucket: its
! link is the bucket's last command added (heads), the first added recorded
! for the join (tails). faces.cpp facesMeshC does the same in C.
!
! FaceSet: uint16 quads, triangles; then the quads, then the triangles.
! FaceQuad: uint16 corners[4], byte offsets into the vertices; uint16 material,
! spare. FaceTriangle: uint16 corners[3], flags (TRI_*); uint16 material,
! spare.
! Vertex: int16 x, y; int16 z; uint8 g, clip.
! Material: uint32 w0 (control | jump << 16), w1, w2, flags (MAT_SHADEABLE 1).
! CommandList: +0 cmd, +4 end, +8 grd, +12 link, +16 grdLink, +20 heads,
! +24 tails, +28 materials, +32 shadeGouraud, +36 shade (ShadeCache, or 0).
! ShadeCache: +0 frame, +4 validFrom, +8 vram, +12 miss(cache, key, slot),
! +16 keys[256], +1040 frames[256].
!
! Through a face: r1, r2, r3, r7 its corners A B C D; r4 the face; r8 the
! vertices; r9 its command, r10 its gouraud table, r11 its link, r12 its
! last word; r13 the list; r14 its material. The stack: +0 the quads' or
! triangles' end, +4 inside, +8 the triangles, +12 a triangle's flags.

    .equ    CLIP_FRAME,     0x01
    .equ    CLIP_DISCARD,   0x3E
    .equ    CLIP_PLANE,     0x20
    .equ    MAT_SHADEABLE,  1
    .equ    TRI_PARALLELOGRAM, 1
    .equ    TRI_V0_AT_D,    2
    .equ    TRI_SWAP,       4

! acc (r14) op= how far vertex v lies off the frame; r0, r6 (flush.s OFF)
    .macro  OFF v, op, inv
    mov.l   @\v, r6
    mov     r6, r0
    shlr16  r0
    exts.w  r0, r0              ! x
    cmp/pz  r0
    bt      1f
    bra     2f
    \inv    r0, r14             ! left of the frame by -x
1:  mov     r0, r6
    mov     #88, r0
    shll2   r0                  ! FRAME_WIDTH
    cmp/gt  r0, r6
    bf      2f
    sub     r0, r6
    \op     r6, r14             ! right of it
2:  mov.l   @\v, r6
    exts.w  r6, r6              ! y
    cmp/pz  r6
    bt      3f
    bra     4f
    \inv    r6, r14
3:  mov     #56, r0
    shll2   r0                  ! FRAME_HEIGHT
    cmp/gt  r0, r6
    bf      4f
    sub     r0, r6
    \op     r6, r14
4:
    .endm

! r within VDP1_COORD_MIN .. VDP1_COORD_MAX; r0
    .macro  CLAMP r
    mov     #4, r0
    shll8   r0
    add     #-1, r0             ! 1023
    cmp/gt  r0, \r
    bf      1f
    mov     r0, \r
1:  not     r0, r0              ! -1024
    cmp/ge  r0, \r
    bt      2f
    mov     r0, \r
2:
    .endm

! the corners from the offsets in r1 (A, B) and r7 (C, D, or C and a
! triangle's flags, then D = C)
    .macro  CORNERS triangle
    extu.w  r1, r2
    shlr16  r1
    add     r8, r1
    add     r8, r2
    mov     r7, r3
    shlr16  r3
    add     r8, r3
    .if \triangle
    extu.w  r7, r0
    mov.l   r0, @(12, r15)      ! the flags
    mov     r3, r7
    .else
    extu.w  r7, r7
    add     r8, r7
    .endif
    .endm

! A (r1), B (r2), C (r3) wind the wrong way on the frame: to 'skip'; r0, r5,
! r6, r14
    .macro  BACKFACE skip
    mov.w   @r1, r6             ! ax
    mov.w   @r2, r14
    sub     r6, r14             ! bx - ax
    mov.w   @r3, r5
    sub     r6, r5              ! cx - ax
    mov.l   @r1, r6
    exts.w  r6, r6              ! ay
    mov.l   @r3, r0
    exts.w  r0, r0
    sub     r6, r0              ! cy - ay
    muls.w  r14, r0
    mov.l   @r2, r14
    exts.w  r14, r14
    sub     r6, r14             ! by - ay
    sts     macl, r6
    muls.w  r5, r14
    sts     macl, r0
    cmp/gt  r0, r6
    bf      \skip
    .endm

! unless inside: culled wholly beyond a frame edge or with a corner behind the
! near plane, to 'skip'; r5 the corners' clip flags ored (0 inside); r0, r6
    .macro  CULL skip
    mov.l   @(4, r15), r0
    tst     r0, r0
    bf/s    1f
    mov     #0, r5
    mov.b   @(7, r1), r0
    mov     r0, r6
    mov     r0, r5
    mov.b   @(7, r2), r0
    and     r0, r6
    or      r0, r5
    mov.b   @(7, r3), r0
    and     r0, r6
    or      r0, r5
    mov.b   @(7, r7), r0
    and     r0, r6
    or      r0, r5
    mov     r6, r0
    tst     #CLIP_DISCARD, r0
    bf      \skip
    mov     r5, r0
    tst     #CLIP_PLANE, r0
    bf      \skip
1:
    .endm

! a quad further off the frame on its AD side is drawn as B A D C, mirrored:
! r5 from the clip flags ored to what the control word is xored with; r0
    .macro  MIRROR
    mov     r5, r0
    tst     #CLIP_FRAME, r0
    bt/s    1f
    mov     #0, r5
    bsr     .Lmirror
    nop
1:
    .endm

! r6 = the bucket of A, B, C and D (r7): their z summed >> (2 + OT_SHIFT),
! doubled; r0
    .macro  DEPTH
    mov.w   @(4, r1), r0
    mov     r0, r6
    mov.w   @(4, r2), r0
    add     r0, r6
    mov.w   @(4, r3), r0
    add     r0, r6
    mov.w   @(4, r7), r0
    add     r0, r6
    shlr2   r6
    shlr2   r6
    shlr    r6
    shlr    r6
    add     r6, r6
    .endm

! the command's first word, the material's control word xored with r5,
! linked to the last in bucket r6 (doubled), and the bucket's last made this;
! r14 the material, its index at r4 + 8; then w1, w2 and the last word; r0,
! r5, r6
    .macro  LINK
    mov.w   @(8, r4), r0
    extu.w  r0, r0
    shll2   r0
    shll2   r0
    mov.l   @(28, r13), r14
    add     r0, r14             ! the material
    mov.l   @r14, r0
    xor     r0, r5              ! w0: control, perhaps mirrored
    mov.l   @(20, r13), r0
    add     r6, r0              ! the bucket's head
    mov.w   @r0, r0
    extu.w  r0, r0
    tst     r0, r0
    or      r0, r5
    mov.l   r5, @r9             ! w0
    mov.l   @(20, r13), r5
    add     r6, r5
    bf/s    2f
    mov.w   r11, @r5            ! this the bucket's last
    mov.l   @(24, r13), r5
    add     r6, r5
    mov.w   r11, @r5            ! and its first
2:  mov.l   @(4, r14), r0
    mov.l   r0, @(4, r9)        ! w1
    mov.l   @(8, r14), r0
    mov.l   r0, @(8, r9)        ! w2
    mov.l   r12, @(28, r9)      ! w7: the gouraud table
    .endm

! the corners A B C D to the command; r0
    .macro  QUAD_CORNERS
    mov.l   @r1, r0
    mov.l   r0, @(12, r9)
    mov.l   @r2, r0
    mov.l   r0, @(16, r9)
    mov.l   @r3, r0
    mov.l   r0, @(20, r9)
    mov.l   @r7, r0
    mov.l   r0, @(24, r9)
    .endm

! the gouraud table of corners r1, r2, r3, r7, or a shaded copy of the
! colour table for one shade at all four; r0, r5, r6
    .macro  SHADE
    mov.l   @(32, r13), r6      ! shadeGouraud
    mov.b   @(6, r1), r0
    extu.b  r0, r0
    add     r0, r0
    mov.w   @(r0, r6), r1
    mov.b   @(6, r2), r0
    extu.b  r0, r0
    add     r0, r0
    mov.w   @(r0, r6), r2
    mov.b   @(6, r3), r0
    extu.b  r0, r0
    add     r0, r0
    mov.w   @(r0, r6), r3
    mov.b   @(6, r7), r0
    extu.b  r0, r0
    add     r0, r0
    mov.w   @(r0, r6), r7
    cmp/eq  r1, r2
    bf      3f
    cmp/eq  r1, r3
    bf      3f
    cmp/eq  r1, r7
    bf      3f
    mov.l   @(12, r14), r0
    tst     #MAT_SHADEABLE, r0
    bt      3f
    bsr     .LshadedCopy
    nop
    tst     r0, r0
    bf      4f                  ! drawn through the copy, no gouraud
    mov     r1, r2              ! (one shade, which the call kept in r1)
    mov     r1, r3
    mov     r1, r7
3:  shll16  r1
    or      r2, r1
    mov.l   r1, @r10
    shll16  r3
    or      r7, r3
    mov.l   r3, @(4, r10)
4:
    .endm

! on to the next command: T set when the list is full; r0
    .macro  NEXT
    add     #32, r9
    add     #8, r10
    add     #4, r11
    mov     #1, r0
    shll16  r0
    add     r0, r12
    mov.l   @(4, r13), r0
    cmp/eq  r0, r9
    .endm

    .text
    .align 2
    .global _facesMeshAsm
_facesMeshAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    add     #-16, r15
    mov.l   r6, @(4, r15)       ! inside
    mov     r5, r8
    mov     r7, r13
    mov.l   @r13, r9
    mov.l   @(8, r13), r10
    mov.l   @(12, r13), r11
    mov.l   @(16, r13), r12
    mov.l   @(4, r13), r0
    cmp/eq  r0, r9
    bf      1f
    bra     .Ldone              ! no room left
    nop
1:
    mov.w   @(2, r4), r0
    extu.w  r0, r0
    mov.l   r0, @(8, r15)       ! the triangles
    mov.w   @r4, r0
    extu.w  r0, r0
    add     #4, r4              ! the first quad
    mov     r0, r1
    add     r1, r1
    add     r0, r1
    shll2   r1                  ! the quads' bytes
    add     r4, r1
    mov.l   r1, @r15            ! their end
    bra     .LquadTest
    nop

.LquadNext:
    add     #12, r4
.LquadTest:
    mov.l   @r15, r0
    cmp/eq  r0, r4
    bf      .Lquad
    bra     .LquadsDone
    nop
.Lquad:
    mov.l   @r4, r1
    mov.l   @(4, r4), r7
    CORNERS 0
    BACKFACE .LquadNext
    CULL    .LquadNext
    MIRROR
    DEPTH
    LINK
    QUAD_CORNERS
    SHADE
    NEXT
    bt      .LfullQuads
    bra     .LquadNext
    nop
.LfullQuads:
    bra     .Ldone
    nop

.LquadsDone:
    mov.l   @(8, r15), r0
    mov     r0, r1
    add     r1, r1
    add     r0, r1
    shll2   r1                  ! the triangles' bytes
    add     r4, r1
    mov.l   r1, @r15            ! their end
    bra     .LtriTest
    nop

.LtriNext:
    add     #12, r4
.LtriTest:
    mov.l   @r15, r0
    cmp/eq  r0, r4
    bf      .Ltri
    bra     .Ldone
    nop
.Ltri:
    mov.l   @r4, r1
    mov.l   @(4, r4), r7
    CORNERS 1
    BACKFACE .LtriNext
    CULL    .LtriNext
    mov.l   @(12, r15), r0
    tst     #TRI_PARALLELOGRAM, r0
    bt      1f
    bra     .Lparallelogram
    nop
1:  ! a quad A B C C
    MIRROR
    DEPTH
    LINK
    QUAD_CORNERS
    SHADE
    NEXT
    bt      .LfullTris
    bra     .LtriNext
    nop
.LfullTris:
    bra     .Ldone
    nop

    ! v0 (r1), x (r2), y (r3): the parallelogram v0, x, x + y - v0, y, its v0
    ! at corner A or D
.Lparallelogram:
    tst     #TRI_SWAP, r0
    bt      1f
    mov     r2, r6
    mov     r3, r2
    mov     r6, r3              ! x, y = v2, v1
1:  mov     r3, r7
    mov     #0, r5
    DEPTH                       ! v0 + x + y + y
    LINK
    ! the fourth corner, within VDP1's coordinates
    mov.w   @r2, r5
    mov.w   @r3, r0
    add     r0, r5
    mov.w   @r1, r0
    sub     r0, r5
    CLAMP   r5
    mov.l   @r2, r0
    exts.w  r0, r6
    mov.l   @r3, r0
    exts.w  r0, r0
    add     r0, r6
    mov.l   @r1, r0
    exts.w  r0, r0
    sub     r0, r6
    CLAMP   r6
    shll16  r5
    extu.w  r6, r6
    or      r6, r5              ! x + y - v0
    mov.l   @(12, r15), r0
    tst     #TRI_V0_AT_D, r0
    bt      .LparaA
    ! v0 at D: x, p, y, v0, shaded x, x, y, v0
    mov.l   @r2, r0
    mov.l   r0, @(12, r9)
    mov.l   r5, @(16, r9)
    mov.l   @r3, r0
    mov.l   r0, @(20, r9)
    mov.l   @r1, r0
    mov.l   r0, @(24, r9)
    mov     r1, r7
    bra     .LparaShade
    mov     r2, r1
    ! v0 at A: v0, x, p, y, shaded v0, x, y, y
.LparaA:
    mov.l   @r1, r0
    mov.l   r0, @(12, r9)
    mov.l   @r2, r0
    mov.l   r0, @(16, r9)
    mov.l   r5, @(20, r9)
    mov.l   @r3, r0
    mov.l   r0, @(24, r9)
.LparaShade:
    SHADE
    NEXT
    bt      .Ldone
    bra     .LtriNext
    nop

.Ldone:
    mov.l   r9, @r13
    mov.l   r10, @(8, r13)
    mov.l   r11, @(12, r13)
    mov.l   r12, @(16, r13)
    add     #16, r15
    lds.l   @r15+, pr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    ! offFrame(A) + offFrame(D) > offFrame(B) + offFrame(C): the corners
    ! swapped to B A D C, r5 the control word's flip; r0, r6, r14
.Lmirror:
    mov     #0, r14
    OFF     r1, add, sub
    OFF     r7, add, sub
    OFF     r2, sub, add
    OFF     r3, sub, add
    cmp/pl  r14
    bf      1f
    mov     r1, r0
    mov     r2, r1
    mov     r0, r2
    mov     r3, r0
    mov     r7, r3
    mov     r0, r7
    mov     #16, r5
    shll16  r5                  ! CMD_FLIP_H << 16
1:  rts
    nop

    ! a shaded copy of the material's colour table for gouraud value r1 in
    ! the list's cache (flush.s .LshadeTable): w1 written through it, r0 not
    ! 0; else r0 0, r1 kept. r0, r2, r3, r5 .. r7
.LshadedCopy:
    mov.l   @(36, r13), r6      ! the cache
    tst     r6, r6
    bt      9f
    mov.l   r4, @-r15
    mov.l   r1, @-r15
    mov.l   @(4, r14), r0
    extu.w  r0, r5              ! the table / 8
    mov     r1, r4
    shll2   r4
    shll    r4
    xor     r5, r4
    extu.b  r4, r4              ! its slot, or the other of the pair
    shll8   r5
    shll2   r5
    shll2   r5
    shll2   r5
    shll    r5
    or      r1, r5              ! the key: table << 15 | gouraud value
    mov     r4, r0
    shll2   r0
    add     r6, r0
    mov.l   @(16, r0), r0       ! keys[slot]
    cmp/eq  r5, r0
    bt      5f
    mov     #1, r0
    xor     r0, r4              ! the other
    mov     r4, r0
    shll2   r0
    add     r6, r0
    mov.l   @(16, r0), r0
    cmp/eq  r5, r0
    bf      7f
5:  mov     r4, r1              ! a hit: if still valid, drawn this frame
    shll2   r1
    add     r6, r1
    mov.w   .LframesAt, r0
    add     r0, r1              ! &frames[slot]
    mov.l   @r1, r0
    mov.l   @(4, r6), r2        ! validFrom
    cmp/hs  r2, r0
    bf      7f
    mov.l   @r6, r0
    mov.l   r0, @r1
    mov     r4, r0
    shll2   r0
    shll2   r0
    shll    r0                  ! slot * 32
    mov.l   @(8, r6), r1
    add     r1, r0
    shlr2   r0
    bra     8f
    shlr    r0                  ! the copy / 8
7:  sts.l   pr, @-r15           ! shadeTableMiss(cache, key, slot)
    mov     r4, r0
    mov     r6, r4
    mov     r0, r6
    mov.l   @(12, r4), r0
    jsr     @r0
    nop
    lds.l   @r15+, pr
    tst     r0, r0
    bt      10f
8:  mov.l   @(4, r14), r5
    mov.l   .LnoGouraud, r6
    and     r6, r5
    or      r0, r5
    mov.l   r5, @(4, r9)        ! w1 through the copy
10: mov.l   @r15+, r1
    rts
    mov.l   @r15+, r4
9:  rts
    mov     #0, r0

    .align 2
.LnoGouraud:    .long   0xFFF80000  ! w1 without the colour calculation and table
.LframesAt:     .short  1040        ! ShadeCache.frames

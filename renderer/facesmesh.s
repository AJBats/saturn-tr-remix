! Stage 3 the new way (faces.h): a mesh's faces straight to VDP1 commands.
!
! void facesMeshAsm(const FaceSet* set, const Vertex* vertices, uint32 mode, CommandList* list)
!
! Each face facing the camera and not culled has its command and gouraud
! table written at once into the list, chained into its depth bucket: its
! link is the bucket's last command added (heads), the first added recorded
! for the join (tails). A face the far plane cuts or lies wholly past is
! filled transparent behind everything instead, over its corners' box on
! the frame (.Lvoid), as rooms' are: what it would have covered shows no old
! frame. A draw under water (MESH_WATER) takes the loops built with its
! shading. labfaces.inc facesMeshC does the same in C.
!
! FaceSet: uint16 quads, triangles, cards, back; then the quads (the last
! 'cards' of them cards), then the triangles. FaceQuad: uint16 corners[4],
! byte offsets from the vertices given (frame.cpp frameMesh takes back off);
! uint16 material, half. FaceTriangle: uint16 corners[3], flags (TRI_*); uint16
! material, spare.
! Vertex: int16 x, y; int16 z; uint8 g, clip.
! Material: uint32 w0 (control | jump << 16), w1, w2, flags (MAT_SHADEABLE 1).
! CommandList: +0 cmd, +4 end, +8 grd, +12 link, +16 grdLink, +20 heads,
! +24 tails, +28 materials, +32 shadeGouraud, +36 shade (ShadeCache, or 0),
! +40 shadeLevel.
! ShadeCache: +0 frame, +4 validFrom, +8 vram, +12 miss(cache, key, slot),
! +16 keys[256], +1040 frames[256].
!
! Through a face: r1, r2, r3, r7 its corners A B C D; r4 the face; r8 the
! vertices; r9 its command, r10 its gouraud table, r11 its link, r12 its
! last word; r13 the list; r14 its material. The stack: +0 the quads' or
! triangles' end, +4 the mode (MESH_*), +8 the triangles, +12 a triangle's
! flags, +16 a parallelogram's fourth corner for its fill (a Vertex).

    .equ    CLIP_FRAME,     0x01
    .equ    CLIP_LEFT,      0x02
    .equ    CLIP_RIGHT,     0x04
    .equ    CLIP_TOP,       0x08
    .equ    CLIP_BOTTOM,    0x10
    .equ    CLIP_DISCARD,   0x3E
    .equ    CLIP_EDGES,     0x1E    ! beyond a frame edge
    .equ    CLIP_PLANE,     0x20
    .equ    VIEW_NEAR,      64      ! a corner's z behind the near plane
    .equ    FAR_BUCKET,     1280    ! faces.cpp's: behind everything
    .equ    MAT_SHADEABLE,  1
    .equ    MESH_INSIDE,    1
    .equ    MESH_WATER,     2
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

! unless inside: culled wholly beyond a frame edge, to 'skip'; with a corner
! past a plane, to 'void'; r5's low byte the corners' clip flags ored (0
! inside): each corner's last longword (z, shade, clip flags) anded and ored,
! loaded ahead of its use; r0, r6, r14
    .macro  CULL skip, void
    mov.l   @(4, r15), r0
    tst     #MESH_INSIDE, r0
    bf/s    1f
    mov     #0, r5
    mov.l   @(4, r1), r6
    mov.l   @(4, r2), r0
    mov     r6, r5
    and     r0, r6
    or      r0, r5
    mov.l   @(4, r3), r0
    mov.l   @(4, r7), r14
    and     r0, r6
    or      r0, r5
    and     r14, r6
    or      r14, r5
    mov     r6, r0
    tst     #CLIP_EDGES, r0
    bf      \skip
    mov     r5, r0
    tst     #CLIP_PLANE, r0
    bt      1f
    bra     \void
    nop
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

! corner v's gouraud colour under water in place (render.cpp waterGouraud):
! its shade's level, red losing 2 for every 1024 beyond the first 1024 of
! depth; r6 shadeLevel, r14 1024; r0, r5
    .macro  WATER v
    mov.b   @(6, \v), r0
    extu.b  r0, r0
    mov.b   @(r0, r6), r5       ! the level
    mov.w   @(4, \v), r0
    sub     r14, r0             ! z - 1024
    cmp/pl  r0
    bf/s    1f
    mov     r5, \v              ! red
    shlr8   r0
    shlr2   r0
    add     r0, r0
    sub     r0, \v
    cmp/pz  \v
    bt      1f
    mov     #0, \v
1:  mov     r5, r0
    shll2   r0
    shll2   r0
    shll    r0
    or      r5, r0
    shll2   r0
    shll2   r0
    shll    r0
    or      r0, \v              ! level << 10 | level << 5 | red
    .endm

! the gouraud table of corners r1, r2, r3, r7 under water; r0, r5, r6, r14
    .macro  WATER_SHADE
    mov.l   @(40, r13), r6      ! shadeLevel
    mov     #4, r14
    shll8   r14                 ! 1024
    WATER   r1
    WATER   r2
    WATER   r3
    WATER   r7
    shll16  r1
    or      r2, r1
    mov.l   r1, @r10
    shll16  r3
    or      r7, r3
    mov.l   r3, @(4, r10)
    .endm

! the shading: under water or not
    .macro  SHADING water
    .if \water
    WATER_SHADE
    .else
    SHADE
    .endif
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
    add     #-24, r15
    mov.l   r6, @(4, r15)       ! mode
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
    add     #8, r4              ! the first quad
    mov     r0, r1
    add     r1, r1
    add     r0, r1
    shll2   r1                  ! the quads' bytes
    add     r4, r1
    mov.l   r1, @r15            ! their end
    mov.l   @(4, r15), r0
    tst     #MESH_WATER, r0
    bt      .LquadTest
    bra     .LquadTestWater
    nop

! the quads, then the triangles, shaded under water or not; labels end in
! sfx
    .macro  FACES sfx, water
    .align 2
    nop                         ! the loops placed as measured fastest (Mednafen, the ledge: 1%)
.LquadNext\sfx:
    add     #12, r4
.LquadTest\sfx:
    mov.l   @r15, r0
    cmp/eq  r0, r4
    bf      .Lquad\sfx
    bra     .LquadsDone\sfx
    nop
.Lquad\sfx:
    mov.l   @r4, r1
    mov.l   @(4, r4), r7
    CORNERS 0
.LQuadBack\sfx:
    BACKFACE .LquadNext\sfx
.LQuadCull\sfx:
    CULL    .LquadNext\sfx, .LquadVoid\sfx
.LQuadMirror\sfx:
    MIRROR
.LQuadDepth\sfx:
    DEPTH
.LQuadLink\sfx:
    LINK
.LQuadCorners\sfx:
    QUAD_CORNERS
.LQuadShade\sfx:
    SHADING \water
.LQuadNext\sfx:
    NEXT
    bt      .LfullQuads\sfx
    bra     .LquadNext\sfx
    nop
.LfullQuads\sfx:
    bra     .Ldone
    nop

.LquadsDone\sfx:
    mov.l   @(8, r15), r0
    mov     r0, r1
    add     r1, r1
    add     r0, r1
    shll2   r1                  ! the triangles' bytes
    add     r4, r1
    mov.l   r1, @r15            ! their end
    bra     .LtriTest\sfx
    nop

.LtriNext\sfx:
    add     #12, r4
.LtriTest\sfx:
    mov.l   @r15, r0
    cmp/eq  r0, r4
    bf      .Ltri\sfx
    bra     .Ldone
    nop
.Ltri\sfx:
    mov.l   @r4, r1
    mov.l   @(4, r4), r7
    CORNERS 1
.LTriBack\sfx:
    BACKFACE .LtriNext\sfx
.LTriCull\sfx:
    CULL    .LtriNext\sfx, .LtriVoid\sfx
    mov.l   @(12, r15), r0
    tst     #TRI_PARALLELOGRAM, r0
    bt      1f
    bra     .Lparallelogram\sfx
    nop
1:  ! a quad A B C C
    MIRROR
.LTriDepth\sfx:
    DEPTH
.LTriLink\sfx:
    LINK
.LTriCorners\sfx:
    QUAD_CORNERS
.LTriShade\sfx:
    SHADING \water
.LTriNext\sfx:
    NEXT
    bt      .LfullTris\sfx
    bra     .LtriNext\sfx
    nop
.LfullTris\sfx:
    bra     .Ldone
    nop

    ! v0 (r1), x (r2), y (r3): the parallelogram v0, x, x + y - v0, y, its v0
    ! at corner A or D
.Lparallelogram\sfx:
    tst     #TRI_SWAP, r0
    bt      1f
    mov     r2, r6
    mov     r3, r2
    mov     r6, r3              ! x, y = v2, v1
1:  mov     r3, r7
    mov     #0, r5
.LParaDepth\sfx:
    DEPTH                       ! v0 + x + y + y
.LParaLink\sfx:
    LINK
.LParaCorner\sfx:
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
    bt      .LparaA\sfx
    ! v0 at D: x, p, y, v0, shaded x, x, y, v0
    mov.l   @r2, r0
    mov.l   r0, @(12, r9)
    mov.l   r5, @(16, r9)
    mov.l   @r3, r0
    mov.l   r0, @(20, r9)
    mov.l   @r1, r0
    mov.l   r0, @(24, r9)
    mov     r1, r7
    bra     .LparaShade\sfx
    mov     r2, r1
    ! v0 at A: v0, x, p, y, shaded v0, x, y, y
.LparaA\sfx:
    mov.l   @r1, r0
    mov.l   r0, @(12, r9)
    mov.l   @r2, r0
    mov.l   r0, @(16, r9)
    mov.l   r5, @(20, r9)
    mov.l   @r3, r0
    mov.l   r0, @(24, r9)
.LparaShade\sfx:
    SHADING \water
.LParaNext\sfx:
    NEXT
    bt      .LfullPara\sfx
    bra     .LtriNext\sfx
    nop
.LfullPara\sfx:
    bra     .Ldone
    nop

.LquadVoid\sfx:
    bsr     .Lvoid
    nop
    bf      1f
    bra     .Ldone              ! full
    nop
1:  bra     .LquadNext\sfx
    nop

.LtriVoid\sfx:
    mov.l   @(12, r15), r0
    tst     #TRI_PARALLELOGRAM, r0
    bt      1f
    ! its fourth corner, x + y - v0, at r7 on the stack (its z C's)
    mov.w   @r2, r5
    mov.w   @r3, r0
    add     r0, r5
    mov.w   @r1, r0
    sub     r0, r5
    mov.w   @(2, r2), r0
    mov     r0, r6
    mov.w   @(2, r3), r0
    add     r0, r6
    mov.w   @(2, r1), r0
    sub     r0, r6
    mov     r15, r7
    add     #16, r7
    mov.w   r5, @r7
    mov     r6, r0
    mov.w   r0, @(2, r7)
    mov.w   @(4, r3), r0
    mov.w   r0, @(4, r7)
1:  bsr     .Lvoid
    nop
    bf      2f
    bra     .Ldone              ! full
    nop
2:  bra     .LtriNext\sfx
    nop
    .endm

    FACES   , 0

.Ldone:
    mov.l   r9, @r13
    mov.l   r10, @(8, r13)
    mov.l   r11, @(12, r13)
    mov.l   r12, @(16, r13)
    add     #24, r15
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

    ! under water, out of the way of the loops above
    .align 2
    FACES   Water, 1

! lo, hi (registers) widened to take corner v's int16 at off; r0
    .macro  RANGE v, off, lo, hi
    mov.w   @(\off, \v), r0
    cmp/gt  r0, \lo
    bf      1f
    mov     r0, \lo
1:  cmp/gt  \hi, r0
    bf      2f
    mov     r0, \hi
2:
    .endm

    ! a face a plane cuts (corners r1, r2, r3, r7), unless a corner lies
    ! behind the near plane: a transparent fill over the corners' box on the
    ! frame, behind everything (labfaces.inc meshVoid, as facesroom.s .Lvoid
    ! for rooms), and on to the next command; T set when the list is then
    ! full, else clear (a fill made or not). r0 .. r3, r5 .. r7, r14
.Lvoid:
    mov     #VIEW_NEAR, r5
    mov.w   @(4, r1), r0
    cmp/gt  r5, r0
    bf      .LvoidNone
    mov.w   @(4, r2), r0
    cmp/gt  r5, r0
    bf      .LvoidNone
    mov.w   @(4, r3), r0
    cmp/gt  r5, r0
    bf      .LvoidNone
    mov.w   @(4, r7), r0
    cmp/gt  r5, r0
    bt      .LvoidMade
.LvoidNone:
    rts
    clrt
.LvoidMade:
    mov.w   @r1, r0
    mov     r0, r6              ! x: least
    mov     r0, r14             ! ... most
    RANGE   r2, 0, r6, r14
    RANGE   r3, 0, r6, r14
    RANGE   r7, 0, r6, r14
    mov.w   @(2, r1), r0
    mov     r0, r5              ! y: least
    mov     r0, r1              ! ... most
    RANGE   r2, 2, r5, r1
    RANGE   r3, 2, r5, r1
    RANGE   r7, 2, r5, r1
    cmp/pz  r6                  ! on the frame
    bt      1f
    mov     #0, r6
1:  cmp/pz  r5
    bt      2f
    mov     #0, r5
2:  mov     #88, r0
    shll2   r0
    add     #-1, r0             ! FRAME_WIDTH - 1
    cmp/gt  r0, r14
    bf      3f
    mov     r0, r14
3:  mov     #56, r0
    shll2   r0
    add     #-1, r0             ! FRAME_HEIGHT - 1
    cmp/gt  r0, r1
    bf      4f
    mov     r0, r1
4:  mov     #0x10, r0           ! (CMD_POLYGON | CMD_JUMP) >> 8
    cmp/gt  r14, r6
    bt      5f
    cmp/gt  r1, r5
    bf      6f
5:  mov     #0x50, r0           ! empty: skipped
6:  shll8   r0
    or      #4, r0
    shll16  r0
    mov     r0, r2              ! w0 but its link
    mov     #(FAR_BUCKET * 2) >> 8, r3
    shll8   r3                  ! the far bucket's head and tail, 16 bits each
    mov.l   @(20, r13), r0
    add     r3, r0
    mov.w   @r0, r7
    extu.w  r7, r7              ! its last command
    or      r7, r2
    mov.l   r2, @r9             ! w0
    mov.w   r11, @r0            ! this the bucket's last
    tst     r7, r7
    bf      7f
    mov.l   @(24, r13), r0
    add     r3, r0
    mov.w   r11, @r0            ! and its first
7:  mov     #-64, r0
    extu.b  r0, r0
    shll16  r0
    mov.l   r0, @(4, r9)        ! w1: PMOD_NO_ECD_SPD << 16, colour 0
    shll16  r6
    shll16  r14
    extu.w  r5, r5
    extu.w  r1, r1
    mov     r6, r0
    or      r5, r0
    mov.l   r0, @(12, r9)       ! x0 y0
    mov     r14, r0
    or      r5, r0
    mov.l   r0, @(16, r9)       ! x1 y0
    mov     r14, r0
    or      r1, r0
    mov.l   r0, @(20, r9)       ! x1 y1
    mov     r6, r0
    or      r1, r0
    mov.l   r0, @(24, r9)       ! x0 y1
    mov.l   r12, @(28, r9)      ! w7
    NEXT
    rts
    nop


! --- the cards --------------------------------------------------------------
!
! void facesMeshCardsAsm(const FaceSet* set, Vertex* vertices, uint32 mode, const uint16* divTable, int32 shade)
!
! Each card's corners placed in the vertices (faces.h): its ends A and B, and
! their copies D and C, moved across the line between the ends by its half
! width at each end, as labfaces.inc facesMeshCardsC works it out; shade added
! to each end's and its copy's; a facing card's A first placed from B
! (.LcardFacing). The stack: +0 on the frame (corners neither flagged nor
! clamped), +4 .. +16 the offsets of A (x, y) and of B, in pixels, +52 shade.

    .macro  ROUND17 r           ! from half pixels and 16 bits below to pixels, rounded
    swap.w  \r, \r
    exts.w  \r, \r
    add     #1, \r
    shar    \r
    .endm

    .align 2
    .global _facesMeshCardsAsm
_facesMeshCardsAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    add     #-20, r15
    mov.w   @(4, r4), r0
    extu.w  r0, r8              ! the cards
    tst     r8, r8
    bf      1f
    bra     .LcardsDone
    nop
1:  mov.w   @r4, r0
    extu.w  r0, r0
    sub     r8, r0              ! the quads before them
    mov     r0, r1
    add     r1, r1
    add     r0, r1
    shll2   r1
    add     #8, r4
    add     r1, r4              ! the first card

.Lcard:
    mov.w   @r4, r0
    extu.w  r0, r9
    add     r5, r9              ! its first end, A
    mov.w   @(2, r4), r0
    extu.w  r0, r10
    add     r5, r10             ! its second, B
    mov     r6, r0
    tst     #MESH_INSIDE, r0
    bf      1f
    mov.b   @(7, r9), r0
    mov     r0, r3
    mov.b   @(7, r10), r0
    or      r0, r3
    mov     r3, r0
    tst     #CLIP_PLANE, r0
    bt      1f
    bra     .LcardNext          ! an end past a plane: left
    nop

1:  mov.l   @(52, r15), r0
    tst     r0, r0
    bt      2f
    bsr     .LcardShade
    nop
2:  mov.b   @(10, r4), r0       ! a facing card's length / 2
    tst     r0, r0
    bt      3f
    bsr     .LcardFacing
    nop

    ! the half width at each end in sixteenths of a pixel, half *
    ! divTable[z >> 4] >> 8: A's to r1, B's to r2
3:  mov.w   @(10, r4), r0
    extu.b  r0, r13             ! half
    mov.w   @(4, r9), r0
    extu.w  r0, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r7), r0
    extu.w  r0, r0
    mulu.w  r0, r13
    sts     macl, r1
    shlr8   r1
    mov.w   @(4, r10), r0
    extu.w  r0, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r7), r0
    extu.w  r0, r0
    mulu.w  r0, r13
    sts     macl, r2
    shlr8   r2
    ! by the view's scale
    mov.l   .LcardFocal, r3
    mov.l   @r3, r3
    mulu.w  r1, r3
    sts     macl, r1
    shll2   r1
    shlr16  r1
    mulu.w  r2, r3
    sts     macl, r2
    shll2   r2
    shlr16  r2

    ! on the frame: the mesh inside it, or both ends M or more inside each
    ! edge, M the wider half width in pixels + 1
    mov     #1, r3
    mov     r6, r0
    tst     #MESH_INSIDE, r0
    bf      .LcardOnFrame
    mov     #0, r3
    mov.b   @(7, r9), r0
    mov     r0, r14
    mov.b   @(7, r10), r0
    or      r0, r14
    mov     r14, r0
    tst     #CLIP_FRAME, r0
    bf      .LcardOnFrame       ! (not)
    mov     r1, r14
    cmp/hs  r2, r14
    bt      2f
    mov     r2, r14
2:  shlr2   r14
    shlr2   r14
    add     #1, r14             ! M
    mov     #-32, r13
    extu.b  r13, r13            ! FRAME_HEIGHT
    sub     r14, r13
    sub     r14, r13            ! FRAME_HEIGHT - 2M
    cmp/pz  r13
    bf      .LcardOnFrame       ! (not)
    mov     #88, r12
    shll2   r12                 ! FRAME_WIDTH
    sub     r14, r12
    sub     r14, r12            ! FRAME_WIDTH - 2M
    mov.w   @r9, r0
    sub     r14, r0
    cmp/hi  r12, r0
    bt      .LcardOnFrame
    mov.w   @r10, r0
    sub     r14, r0
    cmp/hi  r12, r0
    bt      .LcardOnFrame
    mov.w   @(2, r9), r0
    sub     r14, r0
    cmp/hi  r13, r0
    bt      .LcardOnFrame
    mov.w   @(2, r10), r0
    sub     r14, r0
    cmp/hi  r13, r0
    bt      .LcardOnFrame
    mov     #1, r3
.LcardOnFrame:
    mov.l   r3, @r15

    ! the line from A to B: dx to r13, dy to r14, its longer side to r3,
    ! its shorter to r12; past 936 pixels halved until not, dx and dy with it
    mov.w   @r10, r13
    mov.w   @r9, r0
    sub     r0, r13
    mov.w   @(2, r10), r0
    mov     r0, r14
    mov.w   @(2, r9), r0
    sub     r0, r14
    mov     r13, r3
    cmp/pz  r3
    bt      3f
    neg     r3, r3
3:  mov     r14, r12
    cmp/pz  r12
    bt      4f
    neg     r12, r12
4:  cmp/hs  r12, r3
    bt      5f
    mov     r3, r0
    mov     r12, r3
    mov     r0, r12
5:  mov.w   .LcardHalve, r0
    cmp/hi  r0, r3
    bf      6f
    shar    r13
    shar    r14
    shlr    r3
    bra     5b
    shlr    r12

    ! each width times 65536 / 8L: L from 128 L = 123 longer + 51 shorter, in
    ! eighths of a pixel under 112 pixels (rounded, at least 8 x the longer
    ! side and 1), else in pixels (rounded, at least the longer side)
6:  mov     #123, r0
    mulu.w  r0, r3
    sts     macl, r11
    mov     #51, r0
    mulu.w  r0, r12
    sts     macl, r0
    add     r11, r0
    mov     #112, r11
    cmp/hs  r11, r3
    bt      9f
    add     #8, r0
    shlr2   r0
    shlr2   r0
    mov     r3, r11
    shll2   r11
    shll    r11
    cmp/hs  r11, r0
    bt      7f
    mov     r11, r0
7:  tst     r0, r0
    bf      8f
    mov     #1, r0
8:  add     r0, r0
    mov.w   @(r0, r7), r0
    extu.w  r0, r0
    mulu.w  r0, r1
    sts     macl, r1
    mulu.w  r0, r2
    bra     .LcardOffsets
    sts     macl, r2
9:  add     #64, r0
    shll    r0
    shlr8   r0
    cmp/hs  r3, r0
    bt      10f
    mov     r3, r0
10: add     r0, r0
    mov.w   @(r0, r7), r0
    extu.w  r0, r0
    mulu.w  r0, r1
    sts     macl, r1
    shlr2   r1
    shlr    r1
    mulu.w  r0, r2
    sts     macl, r2
    shlr2   r2
    shlr    r2

    ! the offsets: times dy and -dx, >> 16, half pixels, then pixels rounded
.LcardOffsets:
    neg     r13, r13
    mul.l   r14, r1
    sts     macl, r0
    ROUND17 r0
    mov.l   r0, @(4, r15)
    mul.l   r13, r1
    sts     macl, r0
    ROUND17 r0
    mov.l   r0, @(8, r15)
    mul.l   r14, r2
    sts     macl, r0
    ROUND17 r0
    mov.l   r0, @(12, r15)
    mul.l   r13, r2
    sts     macl, r0
    ROUND17 r0
    mov.l   r0, @(16, r15)

    ! A and D from the first end, B and C from the second
    mov.w   @r9, r0
    mov     r0, r13
    mov.w   @(2, r9), r0
    mov     r0, r14
    mov.l   @(4, r15), r1
    mov.l   @(8, r15), r2
    mov     r13, r0
    add     r1, r0
    mov     r14, r3
    add     r2, r3
    bsr     .LcardCorner
    mov     r9, r11
    mov.w   @(6, r4), r0
    extu.w  r0, r11
    add     r5, r11
    mov.l   @(4, r15), r1
    mov.l   @(8, r15), r2
    mov     r13, r0
    sub     r1, r0
    mov     r14, r3
    bsr     .LcardCorner
    sub     r2, r3
    mov.w   @r10, r0
    mov     r0, r13
    mov.w   @(2, r10), r0
    mov     r0, r14
    mov.l   @(12, r15), r1
    mov.l   @(16, r15), r2
    mov     r13, r0
    add     r1, r0
    mov     r14, r3
    add     r2, r3
    bsr     .LcardCorner
    mov     r10, r11
    mov.w   @(4, r4), r0
    extu.w  r0, r11
    add     r5, r11
    mov.l   @(12, r15), r1
    mov.l   @(16, r15), r2
    mov     r13, r0
    sub     r1, r0
    mov     r14, r3
    bsr     .LcardCorner
    sub     r2, r3

.LcardNext:
    dt      r8
    bt      .LcardsDone
    bra     .Lcard
    add     #12, r4
.LcardsDone:
    add     #20, r15
    lds.l   @r15+, pr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

! a corner x (r0), y (r3) into the vertex at r11: unless on the frame
! (@r15), flagged and clamped as the projection does a vertex; r1, r2, r12
.LcardCorner:
    mov.l   @r15, r12
    tst     r12, r12
    bf      .LcornerXY
    mov     #88, r1
    shll2   r1                  ! FRAME_WIDTH
    mov     #-32, r2
    extu.b  r2, r2              ! FRAME_HEIGHT
    cmp/hi  r1, r0
    bt      1f
    cmp/hi  r2, r3
    bf      .LcornerFlags
1:  mov     #CLIP_FRAME, r12
    mov.w   .LcardCoordMax, r1
    cmp/gt  r1, r0
    bf      2f
    mov     r1, r0
2:  cmp/gt  r1, r3
    bf      3f
    mov     r1, r3
3:  not     r1, r1              ! -1024
    cmp/ge  r1, r0
    bt      4f
    mov     r1, r0
4:  cmp/ge  r1, r3
    bt      5f
    mov     r1, r3
5:  cmp/pz  r0
    bt      6f
    add     #CLIP_LEFT, r12
6:  mov     #88, r1
    shll2   r1
    cmp/gt  r1, r0
    bf      7f
    add     #CLIP_RIGHT, r12
7:  cmp/pz  r3
    bt      8f
    add     #CLIP_TOP, r12
8:  cmp/gt  r2, r3
    bf      .LcornerFlags
    add     #CLIP_BOTTOM, r12
.LcornerFlags:
    mov.w   r0, @r11
    mov     r12, r0
    mov.b   r0, @(7, r11)
    mov     r11, r1
    add     #2, r1
    rts
    mov.w   r3, @r1
.LcornerXY:
    mov.w   r0, @r11
    mov     r11, r1
    add     #2, r1
    rts
    mov.w   r3, @r1

    .align 2
.LcardHalve:    .short  936         ! (123 + 51) * 936 >> 7 < DIV_TABLE_SIZE
.LcardCoordMax: .short  1023        ! VDP1_COORD_MAX
    .align  2
.LcardFocal:    .long   _gFocalScale

! shade (r0) added to each end's and its copy's, within 0 .. 31; r1, r2, r3
.LcardShade:
    mov     r0, r1
    mov     #31, r2
    mov.w   @(6, r4), r0
    extu.w  r0, r3
    add     r5, r3              ! D
    mov.b   @(6, r9), r0
    extu.b  r0, r0
    add     r1, r0
    cmp/pz  r0
    bt      1f
    mov     #0, r0
1:  cmp/gt  r2, r0
    bf      2f
    mov     r2, r0
2:  mov.b   r0, @(6, r9)
    mov.b   r0, @(6, r3)
    mov.w   @(4, r4), r0
    extu.w  r0, r3
    add     r5, r3              ! C
    mov.b   @(6, r10), r0
    extu.b  r0, r0
    add     r1, r0
    cmp/pz  r0
    bt      3f
    mov     #0, r0
3:  cmp/gt  r2, r0
    bf      4f
    mov     r2, r0
4:  mov.b   r0, @(6, r10)
    rts
    mov.b   r0, @(6, r3)

! a facing card's first end (r9) placed from its second (r10), which stays:
! its length at the second's depth (length / 2 in r0), in sixteenths of a pixel
! (s), the way the line from the second points on the screen, lifted up the
! screen by about 0.3 s so that it never comes to nothing. r0 .. r3, r11, r13, r14
.LcardFacing:
    extu.b  r0, r1
    add     r1, r1
    mov.w   @(4, r10), r0
    extu.w  r0, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r7), r0
    extu.w  r0, r0
    mulu.w  r0, r1
    sts     macl, r1
    shlr8   r1
    mov.l   .LfacingFocal, r3
    mov.l   @r3, r3
    mulu.w  r1, r3
    sts     macl, r1
    shll2   r1
    shlr16  r1                  ! s
    mov.w   @r9, r13
    mov.w   @r10, r0
    sub     r0, r13             ! dx
    mov.w   @(2, r9), r0
    mov     r0, r14
    mov.w   @(2, r10), r0
    sub     r0, r14
    mov     r1, r0
    shll2   r0
    add     r1, r0
    shlr8   r0
    sub     r0, r14             ! dy, less s * 5 / 256
    mov     r13, r2
    cmp/pz  r2
    bt      1f
    neg     r2, r2
1:  mov     r14, r3
    cmp/pz  r3
    bt      2f
    neg     r3, r3
2:  cmp/hs  r3, r2
    bt      3f
    mov     r2, r0
    mov     r3, r2
    mov     r0, r3              ! the longer side in r2, the shorter in r3
3:  mov.w   .LfacingHalve, r0
    cmp/hi  r0, r2
    bf      4f
    shar    r13
    shar    r14
    shlr    r2
    bra     3b
    shlr    r3
4:  mov     #123, r0
    mulu.w  r0, r2
    sts     macl, r11
    mov     #51, r0
    mulu.w  r0, r3
    sts     macl, r0
    add     r11, r0
    add     #64, r0
    shll    r0
    shlr8   r0                  ! its length L, in pixels
    cmp/hs  r2, r0
    bt      5f
    mov     r2, r0
5:  tst     r0, r0
    bf      6f
    mov     #1, r0
    mov     #-1, r14            ! no way at all: up the screen
6:  add     r0, r0
    mov.w   @(r0, r7), r0
    extu.w  r0, r0
    mulu.w  r0, r1
    sts     macl, r1
    shlr2   r1
    shlr2   r1                  ! s * 4096 / L
    mul.l   r13, r1
    sts     macl, r13
    swap.w  r13, r13
    exts.w  r13, r13
    mul.l   r14, r1
    sts     macl, r14
    swap.w  r14, r14
    exts.w  r14, r14
    mov.w   @r10, r0
    add     r13, r0
    mov.w   r0, @r9
    mov.w   @(2, r10), r0
    add     r14, r0
    rts
    mov.w   r0, @(2, r9)

    .align  2
.LfacingHalve:  .short  936
    .align  2
.LfacingFocal:  .long   _gFocalScale

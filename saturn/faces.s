! Faces into a CPU's ordering table: back faces and faces off the frame are
! dropped, the rest linked into the bucket of their depth. The master adds
! mesh faces (render.cpp faceAddMeshQuads, faceAddMeshTriangles), the slave
! room faces (rooms.cpp faceAddRoomQuads, faceAddRoomTriangles). Far faces
! drawn flat (gFlatDist) stay in C. Both CPUs add meshes' cards here.
!
! void faceAddMeshQuadsAsm(const MeshQuad* polys, int32 count, MeshFaces* mf)
! void faceAddMeshTrianglesAsm(const MeshTriangle* polys, int32 count, MeshFaces* mf)
! void faceAddMeshCardsAsm(const MeshCard* cards, int32 count, MeshFaces* mf, Vertex* ends)
!
! Their bodies, faceAddMeshQuadsBody, faceAddMeshTrianglesBody and
! faceAddMeshCardsBody, take the registers ENTER sets and leave the next face
! in r10 (meshxform.s renderMeshAsm calls them all under one entry).
!
! MeshFaces (render.cpp):
!   +0  const Vertex* vertices  the mesh's
!   +4  int32 base              minus its half of gVertices' address: a
!                               corner's offset (render.h cornerOffset) is
!                               its address + base
!   +8  FaceList* list          next +0, ot +8, groups (uint16) +12
!   +12 uint32 flags            for every face (FACE_WATER)
!   +16 int32 inside            no vertex has clip flags (the fast transform)
!
! MeshQuad, MeshTriangle: uint16 flags; uint8 indices[4] (a triangle's fourth
! says how it is drawn, see faceAddMeshTriangles).
!
! void faceAddRoomQuadsAsm(const RoomQuad* polys, int32 count, RoomFaces* rf)
! void faceAddRoomTrianglesAsm(const RoomTriangle* polys, int32 count, RoomFaces* rf)
!
! RoomFaces (rooms.cpp):
!   +0  const Vertex* vertices  the room's
!   +4  uint32 base             its first vertex's corner offset in the
!                               slave's half, in both halves (an offset pair's)
!   +8  FaceList* list
!   +12 uint32 flags            for every face (FACE_WATER)
!   +16 const Texture* textures
!   +20 int32 detailFaces       counted up
!   +24 int32 detailMisses      counted up
!   +28 Face* end               the list's end
!   +32 const uint8* keys       gDetailKeys[0], each variant's slots + 1
!   +36 DetailSlot* slots       key +0, frame +4; each level's pool from
!                               detailPoolStart
!   +40 uint32 frame            gDetailFrame
!   +44 void (*quad)(const Vertex* v, const RoomQuad* polys, int32 count)
!                               faceAddRoomQuads, for quads a plane cuts or
!                               reaching far off the frame
!   +48 int32 (*miss)(uint32 key, uint32 texSize, uint32 variant)  detailSlotMiss
!   +52 int32 turn              faces may be turned (gTurnLines)
!   +56 int32 across            the face's lines' length in pixels, from TURN
!   +60 int32 lod               4 x the texels a pixel along a line past which a
!                               face takes the next mip level; 0: full res only
!   +64 int32 parity            a quad decides its turn and mip level on frames
!                               of its parity (its index's), keeping them in its
!                               flags' bits 24 .. 27; -1: every frame
!   +68 int32 cached            the variant kept (REUSE), -1 for WHOLE to decide
!   +72 int32 facing            the quads face the camera: no backface test
!
! RoomQuad, RoomTriangle: uint32 flags; uint16 indices[4], byte offsets into
! the room's vertices (pack_level.py).
! Vertex: int16 x, y; int16 z; uint8 g, clip. Face: flags, next, int16
! indices[4], its corners' offsets.
! Texture: uint16 srca, colr, size, attr, color, reserved, altColor, contrast.

    .equ    CLIP_FRAME,     0x01
    .equ    CLIP_DISCARD,   0x3E
    .equ    CLIP_PLANE,     0x20

    .text
    .align 2

! the vertex of index byte 'idx' (in r0's low byte) to pointer r
    .macro  VERTEX r
    shll2   \r
    shll    \r
    add     r8, \r
    .endm

! the face's corner offset of the vertex at r into rd: r + base
    .macro  INDEX r, rd
    mov     \r, \rd
    add     r9, \rd
    .endm

! the room vertex at byte offset r (a room face's corner) to a pointer in r
    .macro  ROOM_VERTEX r
    add     r8, \r
    .endm

! r1, r2, r3 back-facing on the frame: to 'skip'; r0, r13, r14, r7
    .macro  BACKFACE skip
    mov.w   @r1, r13            ! ax
    mov.w   @r2, r14
    sub     r13, r14            ! bx - ax
    mov.w   @r3, r0
    sub     r13, r0
    mov     r0, r13             ! cx - ax
    mov.l   @r1, r0
    exts.w  r0, r0              ! ay
    mov.l   @r3, r7
    exts.w  r7, r7
    sub     r0, r7              ! cy - ay
    muls.w  r14, r7
    mov.l   @r2, r14
    exts.w  r14, r14
    sub     r0, r14             ! by - ay
    sts     macl, r7
    muls.w  r13, r14
    sts     macl, r0
    cmp/gt  r0, r7
    bf      \skip
    .endm

! clip flags of r1, r2, r3 and r (r3 again for triangles) wholly beyond a
! frame edge, or one corner behind the near plane: to 'skip'; r0, r13, r14
    .macro  CULL r, skip
    mov.b   @(7, r1), r0
    mov     r0, r13
    mov     r0, r14
    mov.b   @(7, r2), r0
    and     r0, r13
    or      r0, r14
    mov.b   @(7, r3), r0
    and     r0, r13
    or      r0, r14
    mov.b   @(7, \r), r0
    and     r0, r13
    or      r0, r14
    mov     r13, r0
    tst     #CLIP_DISCARD, r0
    bf      \skip
    mov     r14, r0
    tst     #CLIP_PLANE, r0
    bf      \skip
    .endm

! the face at r10 into bucket r13 (its depth); r0, r14
    .macro  LINK
    mov     r13, r0
    shll2   r0
    mov.l   @(r0, r11), r14
    mov.l   r14, @(4, r10)      ! next: the bucket's head
    mov.l   r10, @(r0, r11)
    mov     r13, r0
    shlr2   r0
    shlr2   r0
    shlr    r0
    add     r0, r0              ! the bucket's group
    mov.w   @(r0, r12), r14
    add     #1, r14
    mov.w   r14, @(r0, r12)
    .endm

! r1..r3 (and r7) from the indices of the face at r4; r0
    .macro  INDICES
    mov.w   @(2, r4), r0
    extu.w  r0, r1
    shlr8   r1
    extu.b  r0, r2
    mov.w   @(4, r4), r0
    extu.w  r0, r3
    shlr8   r3
    VERTEX  r1
    VERTEX  r2
    VERTEX  r3
    .endm

    .macro  ENTER
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    mov.l   @r6, r8             ! vertices
    mov.l   @(4, r6), r9        ! base
    mov.l   @(8, r6), r0        ! list
    mov.l   @r0, r10            ! its next face
    mov.l   @(8, r0), r11       ! ot
    mov     r0, r12
    add     #12, r12            ! groups
    .endm

    .macro  LEAVE
    mov.l   @(8, r6), r0
    mov.l   r10, @r0
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8
    .endm

    .global _faceAddMeshQuadsAsm
_faceAddMeshQuadsAsm:
    ENTER
    sts.l   pr, @-r15
    bsr     _faceAddMeshQuadsBody
    nop
    lds.l   @r15+, pr
    LEAVE

    .global _faceAddMeshQuadsBody
_faceAddMeshQuadsBody:
    tst     r5, r5
    bt      .LquadsDone

.Lquad:
    INDICES
    BACKFACE .LquadNext
    mov.w   @(4, r4), r0
    extu.b  r0, r7
    VERTEX  r7
    mov.l   @(16, r6), r0
    tst     r0, r0
    bf      1f
    CULL    r7, .LquadNext
1:
    ! depth: the corners' mean z >> OT_SHIFT
    mov.w   @(4, r1), r0
    mov     r0, r13
    mov.w   @(4, r2), r0
    add     r0, r13
    mov.w   @(4, r3), r0
    add     r0, r13
    mov.w   @(4, r7), r0
    add     r0, r13
    shlr2   r13
    shlr2   r13
    shlr2   r13
    LINK
    mov.w   @r4, r0
    extu.w  r0, r0
    mov.l   @(12, r6), r14
    or      r14, r0
    mov.l   r0, @r10            ! flags
    INDEX   r1, r13
    INDEX   r2, r14
    shll16  r13
    or      r14, r13
    mov.l   r13, @(8, r10)
    INDEX   r3, r13
    INDEX   r7, r14
    shll16  r13
    or      r14, r13
    mov.l   r13, @(12, r10)
    add     #16, r10
.LquadNext:
    dt      r5
    bt      .LquadsDone
    bra     .Lquad
    add     #6, r4
.LquadsDone:
    rts
    nop

    .global _faceAddMeshTrianglesAsm
_faceAddMeshTrianglesAsm:
    ENTER
    sts.l   pr, @-r15
    bsr     _faceAddMeshTrianglesBody
    nop
    lds.l   @r15+, pr
    LEAVE

    .global _faceAddMeshTrianglesBody
_faceAddMeshTrianglesBody:
    tst     r5, r5
    bf      .Ltri
    bra     .LtrisDone
    nop

.Ltri:
    INDICES
    BACKFACE .LtriNext
    mov.l   @(16, r6), r0
    tst     r0, r0
    bf      1f
    CULL    r3, .LtriNext
1:
    ! flags: drawn as a parallelogram unless coloured, mean-coloured or
    ! marked (0x80); v0 at corner D (1); v1 and v2 swapped (2)
    mov.w   @r4, r0
    extu.w  r0, r13
    mov.l   @(12, r6), r14
    or      r14, r13
    mov.l   .LfaceTriangle, r14
    or      r14, r13
    mov     r13, r0
    shlr8   r0
    shlr2   r0
    shlr2   r0
    shlr2   r0
    and     #15, r0             ! type
    cmp/eq  #1, r0
    bt      .LtriFlags
    cmp/eq  #10, r0
    bt      .LtriFlags
    mov.w   @(4, r4), r0
    extu.b  r0, r14             ! arrangement
    mov     r14, r0
    tst     #0x80, r0
    bf      .LtriFlags
    mov.l   .LfacePara, r0
    or      r0, r13
    mov     r14, r0
    tst     #1, r0
    bt      2f
    mov.l   .LfaceV0AtD, r0
    or      r0, r13
2:  mov     r14, r0
    tst     #2, r0
    bt      .LtriFlags
    mov     r2, r0
    mov     r3, r2
    mov     r0, r3
.LtriFlags:
    mov.l   r13, @r10           ! flags

    ! depth: (z0 + z1 + z2 + z2) >> 2 >> OT_SHIFT
    mov.w   @(4, r1), r0
    mov     r0, r13
    mov.w   @(4, r2), r0
    add     r0, r13
    mov.w   @(4, r3), r0
    add     r0, r13
    add     r0, r13
    shlr2   r13
    shlr2   r13
    shlr2   r13
    LINK
    INDEX   r1, r13
    INDEX   r2, r14
    shll16  r13
    or      r14, r13
    mov.l   r13, @(8, r10)
    INDEX   r3, r13
    mov     r13, r14
    shll16  r13
    or      r14, r13
    mov.l   r13, @(12, r10)
    add     #16, r10
.LtriNext:
    dt      r5
    bt      .LtrisDone
    bra     .Ltri
    add     #6, r4
.LtrisDone:
    rts
    nop

    .align 2
.LfaceTriangle: .long   0x80000000  ! FACE_TRIANGLE
.LfacePara:     .long   0x10000000  ! FACE_PARALLELOGRAM
.LfaceV0AtD:    .long   0x08000000  ! FACE_V0_AT_D

! Cards (render.h MeshCard: int16 half, uint16 flags), after a mesh's
! triangles: each a quad down the line on the screen between its two ends,
! half its width either side at each end's depth (half * 256 / z, as
! PERSPECTIVE projects), so that it always faces the camera. The ends are
! the mesh's last vertices, two a card, transformed and lit with the rest.
! Corners A and B are the ends moved one way across the line and go over
! them; D and C are the ends moved the other way and go in the card's two
! vertices past the mesh's (S = 16 bytes a card on from its ends), with
! their ends' depth and shade. The texture runs along the card from A to B,
! so VDP1 draws a few long lines along it.
!
! The line's length is taken within 4% (123/128 of the longer side and
! 51/128 of the shorter), the width in sixteenths of a pixel until the
! corners are rounded to whole ones: a rope far off is a pixel or two wide.
!
! A card on the frame has no flags or clamps to work out, and moves x and y
! together in one longword: a card of a mesh wholly on the frame
! (MeshFaces.inside; tools/pack_level.py takes the cards into its bound), or
! one whose ends are both farther inside the frame than M, a bound on how
! far its corners lie from them in x or y. Otherwise a card with an end past
! a plane isn't drawn, as a face with such a corner, nor one wholly beyond an
! edge of the frame; its corners are flagged and clamped as the transform
! does a vertex.
!
! faceAddMeshCardsBody: r4 the cards, r5 their count, r7 the first card's
! first end; r6, r8..r12 as ENTER sets, r8 kept.

! r from half pixels and the 16 bits below to whole pixels, rounded
    .macro  ROUND17 r
    swap.w  \r, \r
    exts.w  \r, \r
    add     #1, \r
    shar    \r
    .endm

! the card's half width at each end in sixteenths of a pixel, half *
! divTable[z >> 4] >> 8: end a's (r7) to r1, end d's (r7 + 8) to r2; r0, r8
    .macro  CARD_WIDTHS
    mov.l   .LcardDivTable, r8
    mov.w   @(4, r7), r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r8), r0
    mov.w   @r4, r1
    mulu.w  r0, r1
    mov.w   @(12, r7), r0
    sts     macl, r1
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r8), r0
    mov.w   @r4, r2
    mulu.w  r0, r2
    shlr8   r1
    sts     macl, r2
    shlr8   r2
    .endm

! the line from end a to end d: dx to r13, -dy to r14; and the half widths
! r1, r2 times 65536 / its length, / 8: times dx or dy, >> 16, a corner's
! offset from its end in half pixels; r0, r3, r8
    .macro  CARD_SCALE
    mov.w   @r7, r3
    mov.w   @(8, r7), r0
    sub     r3, r0
    mov     r0, r13
    mov.w   @(2, r7), r0
    mov     r0, r3
    mov.w   @(10, r7), r0
    sub     r0, r3
    mov     r3, r14
    ! the longer of |dx|, |dy| in r3, the shorter in r8
    mov     r13, r3
    cmp/pz  r3
    bt      1f
    neg     r3, r3
1:  mov     r14, r8
    cmp/pz  r8
    bt      2f
    neg     r8, r8
2:  cmp/hs  r8, r3
    bt      3f
    mov     r3, r0
    mov     r8, r3
    mov     r0, r8
3:  mov     #123, r0
    mulu.w  r0, r3
    mov     #51, r0
    sts     macl, r3
    mulu.w  r0, r8
    sts     macl, r0
    add     r0, r3
    shll    r3
    shlr8   r3                  ! (123 longer + 51 shorter) >> 7
    tst     r3, r3
    bf      4f
    mov     #1, r3
4:  mov     #5, r0
    shll8   r0                  ! DIV_TABLE_SIZE - 1
    cmp/hi  r0, r3
    bf      5f
    mov     r0, r3
5:  mov.l   .LcardDivTable, r8
    mov     r3, r0
    add     r0, r0
    mov.w   @(r0, r8), r3
    extu.w  r3, r3              ! 65536 / length
    mulu.w  r1, r3
    sts     macl, r1
    mulu.w  r2, r3
    shlr2   r1
    shlr    r1
    sts     macl, r2
    shlr2   r2
    shlr    r2
    .endm

    .section .text.meshcards, "ax"
    .align 2
    .global _faceAddMeshCardsAsm
_faceAddMeshCardsAsm:
    ENTER
    sts.l   pr, @-r15
    bsr     _faceAddMeshCardsBody
    nop
    lds.l   @r15+, pr
    LEAVE

    .global _faceAddMeshCardsBody
_faceAddMeshCardsBody:
    tst     r5, r5
    bf      1f
    rts
    nop
1:  sts.l   pr, @-r15
    mov.l   r8, @-r15
    mov     r5, r0
    shll2   r0
    shll2   r0
    mov.l   r0, @-r15           ! S, at @r15 while the cards go

.Lcard:
    mov.l   @(16, r6), r0
    tst     r0, r0
    bt      1f
    CARD_WIDTHS
    bra     .LcardOnFrame
    nop

    ! not a mesh wholly on the frame: an end past a plane, not drawn; one off
    ! the frame, flagged and clamped
1:  mov.b   @(7, r7), r0
    mov     r0, r3
    mov.b   @(15, r7), r0
    or      r0, r3
    mov     r3, r0
    tst     #CLIP_PLANE, r0
    bt      2f
    bra     .LcardNext
    nop
2:  CARD_WIDTHS
    tst     r3, r3
    bt      3f
    bra     .LcardFar
    nop
    ! M = max(r1, r2) * 17 / 256 + 2 whole pixels: no corner is farther from
    ! its end in x or y (the half width's sixteenths, the 4% of the length,
    ! the rounding). On the frame if each end is M or more inside each edge:
    ! x - M within 0 .. 352 - 2M, y - M within 0 .. 224 - 2M
3:  mov     r1, r3
    cmp/hi  r1, r2
    bf      4f
    mov     r2, r3
4:  mov     r3, r0
    shll2   r0
    shll2   r0
    add     r0, r3
    shlr8   r3
    add     #2, r3
    mov     #-32, r14
    extu.b  r14, r14            ! FRAME_HEIGHT
    sub     r3, r14
    sub     r3, r14
    cmp/pz  r14
    bf      .LcardFar           ! wider than the frame is tall
    mov     #88, r13
    shll2   r13                 ! FRAME_WIDTH
    sub     r3, r13
    sub     r3, r13
    mov.w   @r7, r0
    sub     r3, r0
    cmp/hi  r13, r0
    bt      .LcardFar
    mov.w   @(8, r7), r0
    sub     r3, r0
    cmp/hi  r13, r0
    bt      .LcardFar
    mov.w   @(2, r7), r0
    sub     r3, r0
    cmp/hi  r14, r0
    bt      .LcardFar
    mov.w   @(10, r7), r0
    sub     r3, r0
    cmp/hi  r14, r0
    bt      .LcardFar

    ! on the frame: each end's offset (dx, dy) across, packed as a vertex's
    ! x and y, added for A and B, taken away for D and C
.LcardOnFrame:
    CARD_SCALE
    mul.l   r14, r1
    sts     macl, r0
    mul.l   r13, r1
    ROUND17 r0
    sts     macl, r1
    ROUND17 r1
    shll16  r0
    add     r1, r0
    mov.l   @r7, r1
    mov     r1, r3
    add     r0, r1
    sub     r0, r3
    mov.l   r1, @r7             ! A
    mov.l   @r15, r8
    add     r7, r8
    mov.l   r3, @r8             ! D
    mov.l   @(4, r7), r0
    mov.l   r0, @(4, r8)        ! with a's depth and shade
    mul.l   r14, r2
    sts     macl, r0
    mul.l   r13, r2
    ROUND17 r0
    sts     macl, r1
    ROUND17 r1
    shll16  r0
    add     r1, r0
    mov.l   @(8, r7), r1
    mov     r1, r3
    add     r0, r1
    sub     r0, r3
    mov.l   r1, @(8, r7)        ! B
    mov.l   r3, @(8, r8)        ! C
    mov.l   @(12, r7), r0
    bra     .LcardFace
    mov.l   r0, @(12, r8)       ! with d's

    ! reaching off the frame: the corners' x and y on their own, then flagged
    ! and clamped
.LcardFar:
    CARD_SCALE
    mul.l   r14, r1
    sts     macl, r0
    mul.l   r13, r1
    ROUND17 r0
    sts     macl, r3
    ROUND17 r3
    mov.l   @r15, r8
    add     r7, r8              ! D
    mov.w   @r7, r1
    add     r0, r1
    mov.w   r1, @r7             ! A.x
    sub     r0, r1
    sub     r0, r1
    mov.w   r1, @r8             ! D.x
    mov.w   @(2, r7), r0
    add     r3, r0
    mov.w   r0, @(2, r7)        ! A.y
    sub     r3, r0
    sub     r3, r0
    mov.w   r0, @(2, r8)        ! D.y
    mov.l   @(4, r7), r0
    mov.l   r0, @(4, r8)
    mul.l   r14, r2
    sts     macl, r0
    mul.l   r13, r2
    ROUND17 r0
    sts     macl, r3
    ROUND17 r3
    mov     r7, r1
    add     #8, r1              ! B
    add     #8, r8              ! C
    mov.w   @r1, r2
    add     r0, r2
    mov.w   r2, @r1             ! B.x
    sub     r0, r2
    sub     r0, r2
    mov.w   r2, @r8             ! C.x
    mov.w   @(2, r1), r0
    add     r3, r0
    mov.w   r0, @(2, r1)        ! B.y
    sub     r3, r0
    sub     r3, r0
    mov.w   r0, @(2, r8)        ! C.y
    mov.l   @(4, r1), r0
    mov.l   r0, @(4, r8)
    mov     #-1, r13            ! the corners' flags, anded
    bsr     .LcardCorner
    mov     r7, r1              ! A
    and     r0, r13
    bsr     .LcardCorner
    add     #8, r1              ! B
    and     r0, r13
    bsr     .LcardCorner
    mov     r8, r1              ! C
    and     r0, r13
    bsr     .LcardCorner
    add     #-8, r1             ! D
    and     r0, r13
    mov     r13, r0
    tst     #CLIP_DISCARD, r0
    bt      .LcardFace
    bra     .LcardNext          ! wholly beyond an edge
    nop

    ! the face, at the ends' mean depth: corners A B C D at the ends' r7 and
    ! r7 + 8, then S on from them
.LcardFace:
    mov.w   @(4, r7), r0
    mov     r0, r13
    mov.w   @(12, r7), r0
    add     r0, r13
    shlr2   r13
    shlr2   r13
    shlr    r13
    LINK
    mov.w   @(2, r4), r0
    extu.w  r0, r0
    mov.l   @(12, r6), r14
    or      r14, r0
    mov.l   r0, @r10            ! flags
    mov     r7, r13
    add     r9, r13             ! A
    mov     r13, r14
    add     #8, r14             ! B
    shll16  r13
    or      r14, r13
    mov.l   r13, @(8, r10)
    mov.l   @r15, r0
    add     r0, r14             ! C
    mov     r14, r13
    add     #-8, r13            ! D
    shll16  r14
    or      r13, r14
    mov.l   r14, @(12, r10)
    add     #16, r10

.LcardNext:
    add     #4, r4
    dt      r5
    bt      .LcardsDone
    bra     .Lcard
    add     #16, r7
.LcardsDone:
    add     #4, r15             ! S
    mov.l   @r15+, r8
    lds.l   @r15+, pr
    rts
    nop

! the corner at r1 flagged and clamped off the frame as the transform does a
! vertex (meshxform.s); its flags to r0; r2, r3, r14
.LcardCorner:
    mov.w   @r1, r2             ! x
    mov.w   @(2, r1), r0
    mov     r0, r3              ! y
    mov     #0, r0
    mov.w   .LcardFrameWidth, r14
    cmp/hi  r14, r2
    bt      1f
    mov.w   .LcardFrameHeight, r14
    cmp/hi  r14, r3
    bf      9f
1:  mov     #CLIP_FRAME, r0
    cmp/pz  r2
    bt      2f
    add     #2, r0              ! CLIP_LEFT
2:  mov.w   .LcardFrameWidth, r14
    cmp/gt  r14, r2
    bf      3f
    add     #4, r0              ! CLIP_RIGHT
3:  cmp/pz  r3
    bt      4f
    add     #8, r0              ! CLIP_TOP
4:  mov.w   .LcardFrameHeight, r14
    cmp/gt  r14, r3
    bf      5f
    add     #16, r0             ! CLIP_BOTTOM
5:  mov.w   .LcardCoordMax, r14
    cmp/gt  r14, r2
    bf      6f
    mov     r14, r2
6:  cmp/gt  r14, r3
    bf      7f
    mov     r14, r3
7:  not     r14, r14            ! VDP1's least, -1024
    cmp/ge  r14, r2
    bt      8f
    mov     r14, r2
8:  cmp/ge  r14, r3
    bt      10f
    mov     r14, r3
10: mov.w   r2, @r1
    mov     r0, r14
    mov     r3, r0
    mov.w   r0, @(2, r1)
    mov     r14, r0
9:  rts
    mov.b   r0, @(7, r1)

    .align 2
.LcardDivTable:     .long   _divTable
.LcardFrameWidth:   .short  352         ! FRAME_WIDTH
.LcardFrameHeight:  .short  224         ! FRAME_HEIGHT
.LcardCoordMax:     .short  1023        ! VDP1_COORD_MAX

    .text

! r1..r3 from the indices of the room face at r4; r0
    .macro  ROOM_INDICES
    mov.l   @(4, r4), r0
    mov     r0, r1
    shlr16  r1
    extu.w  r0, r2
    mov.l   @(8, r4), r0
    mov     r0, r3
    shlr16  r3
    ROOM_VERTEX r1
    ROOM_VERTEX r2
    ROOM_VERTEX r3
    .endm

! r13 = the largest z of r1, r2, r3 and r, >> OT_SHIFT; r0
    .macro  ZMAX r
    mov.w   @(4, r1), r0
    mov     r0, r13
    mov.w   @(4, r2), r0
    cmp/gt  r13, r0
    bf      1f
    mov     r0, r13
1:  mov.w   @(4, r3), r0
    cmp/gt  r13, r0
    bf      2f
    mov     r0, r13
2:  mov.w   @(4, \r), r0
    cmp/gt  r13, r0
    bf      3f
    mov     r0, r13
3:  shlr2   r13
    shlr2   r13
    .endm

! acc = max(acc, |a.x - b.x|, |a.y - b.y|), the steps along edge a b
! (vertexSpan); r0, t
    .macro  SPAN a, b, acc, t
    mov.w   @\a, r0
    mov.w   @\b, \t
    sub     \t, r0
    cmp/pz  r0
    bt      1f
    neg     r0, r0
1:  cmp/gt  \acc, r0
    bf      2f
    mov     r0, \acc
2:  mov.w   @(2, \a), r0
    mov     r0, \t
    mov.w   @(2, \b), r0
    sub     r0, \t
    cmp/pz  \t
    bt      3f
    neg     \t, \t
3:  cmp/gt  \acc, \t
    bf      4f
    mov     \t, \acc
4:
    .endm

! RoomFaces.cached = the quad's mip variant (level * 2 + turned) kept from the
! last frame it decided (its flags' bits 24 .. 27) and r14 its
! FACE_DETAIL_TURNED, on frames not of its parity (RoomFaces.parity, -1:
! none), branching to 'decided'; else RoomFaces.cached = -1 for TURN and WHOLE
! to decide; r0, r13
    .macro  REUSE decided
    mov     #-1, r13
    mov     #68, r0
    mov.l   r13, @(r0, r6)      ! cached: none
    mov     #64, r0
    mov.l   @(r0, r6), r0       ! parity
    cmp/pz  r0
    bf      .Lreuse\@           ! decisions not kept
    xor     r5, r0
    tst     #1, r0
    bt      .Lreuse\@           ! the quad's frame to decide again
    mov.b   @r4, r0             ! flags, bits 24 .. 31
    tst     #8, r0
    bt      .Lreuse\@           ! none kept yet
    and     #7, r0
    mov     r0, r13
    mov     #68, r0
    mov.l   r13, @(r0, r6)      ! cached
    mov     r13, r0
    tst     #1, r0
    bt/s    \decided
    mov     #0, r14
    mov     #1, r14
    shll16  r14
    shll2   r14
    bra     \decided
    shll    r14                 ! FACE_DETAIL_TURNED
.Lreuse\@:
    .endm

! r14 = FACE_DETAIL_TURNED if the face with corners r1, r2, r3, r7 (A B C D)
! takes fewer lines drawn turned (faceTurns), else 0; the lines' length to
! RoomFaces.across; r0, r13
    .macro  TURN
    mov.l   r5, @-r15
    mov     #0, r13
    SPAN    r1, r7, r13, r5     ! AD
    SPAN    r2, r3, r13, r5     ! BC: the lines as it is
    mov     #0, r14
    SPAN    r1, r2, r14, r5     ! AB
    SPAN    r7, r3, r14, r5     ! DC: the lines turned
    mov.l   @r15+, r5
    mov.l   @(52, r6), r0
    tst     r0, r0
    bt      8f                  ! lines never turned
    cmp/gt  r14, r13
    bf      8f
    mov.l   r13, @(56, r6)      ! turned: lines as long as AD, BC
    mov     #1, r14
    shll16  r14
    shll2   r14
    bra     9f
    shll    r14                 ! FACE_DETAIL_TURNED
8:  mov.l   r14, @(56, r6)      ! as it is: as long as AB, DC
    mov     #0, r14
9:
    .endm

! flags r14, with the face's texture whole if the cache has it, transposed
! if r14 has FACE_DETAIL_TURNED (wholeTexture), at the mip level that has
! about a texel a pixel along the face's lines (RoomFaces.across, .lod), to
! the face at r10; r0..r3, r7
    .macro  WHOLE
    mov.w   .LtexMask\@, r0
    and     r14, r0
    shll2   r0
    shll2   r0
    mov.l   @(16, r6), r1
    add     r0, r1              ! the texture
    mov.w   @(10, r1), r0
    extu.w  r0, r0              ! reserved: its full-res block + 1
    tst     r0, r0
    bf      5f
    bra     .Lplain\@
    nop
5:  add     #-1, r0             ! key
    mov.w   .LmaxKeys\@, r2
    cmp/hs  r2, r0
    bf      6f
    bra     .Lmissed\@
    nop
6:  mov     r0, r3              ! key

    ! r7 = level * 2 + turned: kept from last frame (REUSE), or decided now
    mov     #68, r0
    mov.l   @(r0, r6), r0       ! cached
    cmp/pz  r0
    bf      .Ldecide\@
    bra     .Lvariant\@
    mov     r0, r7
.Ldecide\@:
    mov     r14, r0
    shlr16  r0
    shlr2   r0
    shlr    r0
    and     #1, r0
    mov     r0, r7
    mov.l   @(60, r6), r2       ! lod
    tst     r2, r2
    bt      .Ldecided\@
    mov.l   @(56, r6), r0
    mulu.w  r0, r2              ! lod x across
    mov.w   @(4, r1), r0
    tst     r7, r7
    bf/s    1f
    extu.w  r0, r2              ! size: the quadrant's width / 8 << 8 | height
    shlr8   r2
    shll2   r2
    shll2   r2
    bra     2f
    shll2   r2                  ! 4 x the whole texture's width
1:  extu.b  r2, r2
    shll2   r2
    shll    r2                  ! 4 x its height, along a turned face's lines
2:  sts     macl, r0
    cmp/hi  r0, r2
    bf      .Ldecided\@         ! about a texel a pixel at full res
    mov.w   @(4, r1), r0
    tst     #7, r0
    bf      .Ldecided\@         ! half the height not a multiple of 8
    add     #2, r7              ! level 1
    sts     macl, r0
    shll    r0
    cmp/hi  r0, r2
    bf      .Ldecided\@
    mov.w   @(4, r1), r0
    mov.w   .Lmip2Mask\@, r2
    tst     r2, r0
    bf      .Ldecided\@         ! a quarter of the width or height not a multiple of 8
    add     #2, r7              ! level 2
.Ldecided\@:
    mov     #64, r0
    mov.l   @(r0, r6), r0       ! parity
    cmp/pz  r0
    bf      .Lvariant\@         ! decisions not kept
    mov     r7, r0
    or      #8, r0
    mov.b   r0, @r4             ! kept in the quad's flags, bits 24 .. 27
.Lvariant\@:
    mov     r7, r2
    add     r2, r2
    mova    .LvariantKeys\@, r0
    mov.w   @(r0, r2), r2
    mov.l   @(32, r6), r0
    add     r0, r2              ! the variant's slots
    mov     r3, r0
    mov.b   @(r0, r2), r2
    extu.b  r2, r2
    tst     r2, r2
    bt      .Lmiss\@
    add     #-1, r2             ! the slot in its level's pool
.Lslot\@:
    mov     r7, r0
    and     #6, r0
    mov     r0, r1
    mova    .LpoolStart\@, r0
    mov.w   @(r0, r1), r1       ! the pool's first slot
    add     r2, r1
    shll2   r1
    shll    r1
    mov.l   @(36, r6), r3
    add     r1, r3
    mov.l   @(40, r6), r0
    mov.l   r0, @(4, r3)        ! drawn this frame
    shll16  r2
    shll2   r2
    shll2   r2
    mov.l   .LdetailWhole\@, r0
    or      r0, r2
    or      r2, r14             ! FACE_DETAIL | FACE_DETAIL_WHOLE | slot
    mov     r7, r0
    and     #6, r0
    tst     r0, r0
    bt      3f
    cmp/eq  #4, r0
    mov.l   .LdetailMip\@, r1
    bf      4f
    mov.l   .LdetailMip2\@, r1
4:  or      r1, r14             ! its level
3:  mov.l   @(20, r6), r0
    add     #1, r0
    bra     .Lflags\@
    mov.l   r0, @(20, r6)
    .align 2
.LdetailWhole\@:  .long  0x28000000 ! FACE_DETAIL | FACE_DETAIL_WHOLE
.LdetailMip\@:    .long  0x00040000 ! FACE_DETAIL_MIP
.LdetailMip2\@:   .long  0x04040000 ! FACE_DETAIL_MIP | FACE_DETAIL_MIP2
.LvariantKeys\@:  .short 0, 2176, 3264, 4352, 5440, 6528  ! by level * 2 + turned: variant * DETAIL_MAX_KEYS
.LpoolStart\@:    .short 0, 128, 192                   ! detailPoolStart
.LtexMask\@:      .short 0x3FFF     ! FACE_TEXTURE
.LmaxKeys\@:      .short 1088       ! DETAIL_MAX_KEYS
.Lmip2Mask\@:     .short 0x010F     ! an odd width / 8 or height not a multiple of 16
.Lmiss\@:
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   r6, @-r15
    mov.l   r7, @-r15
    mov.l   @(48, r6), r2
    mov     r3, r4              ! key
    mov.w   @(4, r1), r0
    extu.w  r0, r5              ! the texture's size
    mov     r7, r6
    tst     r6, r6
    bt      7f
    add     #1, r6              ! the variant: past full res, level * 2 + turned + 1
7:  jsr     @r2
    nop
    mov.l   @r15+, r7
    mov.l   @r15+, r6
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    cmp/pz  r0
    bf      .Lmissed\@
    bra     .Lslot\@
    mov     r0, r2
.Lmissed\@:
    mov.l   @(24, r6), r0
    add     #1, r0
    mov.l   r0, @(24, r6)
.Lplain\@:
    mov     #-9, r0
    swap.w  r0, r0              ! ~FACE_DETAIL_TURNED
    and     r0, r14             ! drawn in its mean colour
.Lflags\@:
    mov.l   r14, @r10
    .endm

! the face at r10 takes the index pairs at r4 + 4 and in 'pair2', offset; r0
    .macro  ROOM_FACE pair2
    mov.l   @(4, r4), r0
    add     r9, r0
    mov.l   r0, @(8, r10)
    mov     \pair2, r0
    add     r9, r0
    mov.l   r0, @(12, r10)
    add     #16, r10
    .endm

! culled by CULL (r13 its corners' clip flags anded, r1 a corner): wholly
! beyond a frame edge or behind the near plane, to 'gone'; wholly past the far
! plane, on; else (cut by a plane) to 'cut'; r0, r13
    .macro  GONE gone, cut
    mov     r13, r0
    tst     #CLIP_DISCARD & ~CLIP_PLANE, r0
    bf      \gone
    tst     #CLIP_PLANE, r0
    bt      \cut
    mov.w   @(4, r1), r0
    shlr2   r0
    shlr2   r0
    mov     #5, r13
    shll8   r13                 ! the far plane >> OT_SHIFT
    cmp/eq  r13, r0
    bf      \gone
    .endm

! the face at r10 a transparent fill behind everything (rooms.cpp
! faceAddRoomVoid), with the index pairs at r4 + 4 and in 'pair2'; r0, r13,
! r14
    .macro  VOID pair2
    mov     #5, r13
    shll8   r13                 ! the last bucket, OT_SIZE - 1
    LINK
    mov     #11, r0
    shll8   r0
    shll2   r0
    shll2   r0
    shll2   r0                  ! FACE_TYPE_VOID << FACE_TYPE_SHIFT
    mov.l   r0, @r10
    ROOM_FACE \pair2
    .endm

    .macro  ROOM_LEAVE
    mov.l   @(8, r6), r0
    mov.l   r10, @r0
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    mov.l   @r15+, r8
    lds.l   @r15+, pr
    rts
    nop
    .endm

    .global _faceAddRoomQuadsAsm
_faceAddRoomQuadsAsm:
    sts.l   pr, @-r15
    ENTER
    mov     #72, r0
    mov.l   @(r0, r6), r0
    mov.l   r0, @-r15           ! facing, at @r15 while the quads go
    tst     r5, r5
    bf      .Lrquad
    bra     .LrquadsDone
    nop

    ! culled and cut by a plane, or near the camera: to C, which splits the
    ! quad (or fills it), or adds it whole
.LrquadCut:
    mov.l   @(8, r6), r0
    mov.l   r10, @r0            ! the list's next face, for C
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   r6, @-r15
    mov.l   @(44, r6), r0
    mov     r4, r5
    mov     r8, r4
    jsr     @r0
    mov     #1, r6
    mov.l   @r15+, r6
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    mov.l   @(8, r6), r0
    bra     .LrquadNext
    mov.l   @r0, r10

    ! culled: wholly past the far plane, filled; cut by a plane, to C
.LrquadCulled:
    GONE    .LrquadNext, .LrquadCut
    mov.l   @(8, r4), r1
    VOID    r1

.LrquadNext:
    dt      r5
    bf/s    .Lrquad
    add     #12, r4
    bra     .LrquadsDone
    nop

.Lrquad:
    mov.l   @(28, r6), r0
    cmp/eq  r0, r10
    bf      1f
    bra     .LrquadsDone        ! out of faces
    nop
1:  ROOM_INDICES
    mov.l   @r15, r0
    tst     r0, r0
    bf      2f                  ! facing the camera
    BACKFACE .LrquadNext
2:  mov.l   @(8, r4), r0
    extu.w  r0, r7
    ROOM_VERTEX r7
    CULL    r7, .LrquadCulled
    mov.w   @(4, r1), r0
    mov     #8, r13
    shll8   r13                 ! NEAR_SPLIT_DIST
    cmp/ge  r13, r0
    bt      3f
    bra     .LrquadCut          ! corner A near the camera: C decides on a split
    nop
3:
    REUSE   .LrquadDecided
    TURN
.LrquadDecided:
    ZMAX    r7
    mov.l   @r4, r0
    shll8   r0
    shlr8   r0                  ! not the kept decisions (REUSE)
    or      r0, r14
    mov.l   @(12, r6), r0
    or      r0, r14             ! flags
    WHOLE
    LINK
    mov.l   @(8, r4), r1
    ROOM_FACE r1
    bra     .LrquadNext
    nop
.LrquadsDone:
    add     #4, r15             ! facing
    ROOM_LEAVE

    .global _faceAddRoomTrianglesAsm
_faceAddRoomTrianglesAsm:
    sts.l   pr, @-r15
    ENTER
    tst     r5, r5
    bf      .Lrtri
    bra     .LrtrisDone
    nop

    ! culled: cut by a plane or wholly past the far one, filled
.LrtriCulled:
    GONE    .LrtriNext, 1f
1:  BACKFACE .LrtriNext
    mov.l   @(8, r4), r0
    shlr16  r0
    mov     r0, r1
    shll16  r1
    or      r0, r1              ! v2 twice
    VOID    r1

.LrtriNext:
    dt      r5
    bf/s    .Lrtri
    add     #12, r4
    bra     .LrtrisDone
    nop

.Lrtri:
    mov.l   @(28, r6), r0
    cmp/eq  r0, r10
    bf      1f
    bra     .LrtrisDone         ! out of faces
    nop
1:  ROOM_INDICES
    CULL    r3, .LrtriCulled
    BACKFACE .LrtriNext
    mov     r3, r7              ! corner D is C
    TURN
    ZMAX    r3
    mov.l   @r4, r0
    or      r0, r14
    mov.l   @(12, r6), r0
    or      r0, r14
    mov.l   .LfaceTriangle2, r0
    or      r0, r14             ! flags
    WHOLE
    LINK
    mov.l   @(8, r4), r0
    shlr16  r0
    mov     r0, r1
    shll16  r1
    or      r0, r1              ! v2 twice
    ROOM_FACE r1

    bra     .LrtriNext
    nop
.LrtrisDone:
    ROOM_LEAVE

    .align 2
.LfaceTriangle2: .long  0x80000000  ! FACE_TRIANGLE

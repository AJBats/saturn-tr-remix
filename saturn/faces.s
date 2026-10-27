! Faces into a CPU's ordering table: back faces and faces off the frame are
! dropped, the rest linked into the bucket of their depth. The master adds
! mesh faces (render.cpp faceAddMeshQuads, faceAddMeshTriangles), the slave
! room faces (rooms.cpp faceAddRoomQuads, faceAddRoomTriangles). Far faces
! drawn flat (gFlatDist) stay in C.
!
! void faceAddMeshQuadsAsm(const MeshQuad* polys, int32 count, MeshFaces* mf)
! void faceAddMeshTrianglesAsm(const MeshTriangle* polys, int32 count, MeshFaces* mf)
!
! Their bodies, faceAddMeshQuadsBody and faceAddMeshTrianglesBody, take the
! registers ENTER sets and leave the next face in r10 (meshxform.s
! renderMeshAsm calls them both under one entry).
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
!                               faceAddRoomQuads, for quads a plane cuts
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

    ! culled and cut by a plane: to C, which splits the quad or fills it
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

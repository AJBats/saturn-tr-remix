! Faces into a CPU's ordering table: back faces and faces off the frame are
! dropped, the rest linked into the bucket of their depth. The master adds
! mesh faces (render.cpp faceAddMeshQuads, faceAddMeshTriangles), the slave
! room faces (rooms.cpp faceAddRoomQuads, faceAddRoomTriangles). Far faces
! drawn flat (gFlatDist) and the original's texture LOD (gSaturnLod) stay in C.
!
! void faceAddMeshQuadsAsm(const MeshQuad* polys, int32 count, MeshFaces* mf)
! void faceAddMeshTrianglesAsm(const MeshTriangle* polys, int32 count, MeshFaces* mf)
!
! MeshFaces (render.cpp):
!   +0  const Vertex* vertices  the mesh's
!   +4  int32 base              minus gVertices' address / 8: a vertex's
!                               index is its address / 8 + base
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
!   +4  uint32 base             its first vertex's index in gVertices, in
!                               both halves (an index pair's offset)
!   +8  FaceList* list
!   +12 uint32 flags            for every face (FACE_WATER)
!   +16 const Texture* textures
!   +20 int32 detailFaces       counted up
!   +24 int32 detailMisses      counted up
!   +28 Face* end               the list's end
!   +32 const uint8* keys       gDetailKeys[0], whole textures' slots + 1
!   +36 DetailSlot* slots       key +0, frame +4
!   +40 uint32 frame            gDetailFrame
!   +44 void (*quad)(const Vertex* v, const RoomQuad* polys, int32 count)
!                               faceAddRoomQuads, for quads crossing the near plane
!   +48 int32 (*miss)(uint32 key, uint32 texSize, bool quads)  detailSlotMiss
!
! RoomQuad, RoomTriangle: uint32 flags; uint16 indices[4].
! Vertex: int16 x, y; int16 z; uint8 g, clip. Face: flags, next, int16 indices[4].
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

! the face's index of the vertex at r into rd: (r >> 3) + base; r0
    .macro  INDEX r, rd
    mov     \r, \rd
    shlr2   \rd
    shlr    \rd
    add     r9, \rd
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
    tst     r5, r5
    bf      .Lquad
    bra     .LquadsDone
    nop

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
    LEAVE

    .global _faceAddMeshTrianglesAsm
_faceAddMeshTrianglesAsm:
    ENTER
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
    LEAVE

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
    VERTEX  r1
    VERTEX  r2
    VERTEX  r3
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

! flags r14, with the face's full-res texture whole if the cache has it
! (wholeTexture), to the face at r10; r0..r3
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
    bt      .Lflags\@
    add     #-1, r0             ! key
    mov.w   .LmaxKeys\@, r2
    cmp/hs  r2, r0
    bt      .Lmiss\@
    mov.l   @(32, r6), r2
    mov.b   @(r0, r2), r2
    extu.b  r2, r2
    tst     r2, r2
    bt      .Lmiss\@
    add     #-1, r2
.Lslot\@:
    mov     r2, r0
    shll2   r0
    shll    r0
    mov.l   @(36, r6), r3
    add     r0, r3
    mov.l   @(40, r6), r0
    mov.l   r0, @(4, r3)        ! drawn this frame
    shll16  r2
    shll2   r2
    shll2   r2
    mov.l   .LdetailWhole\@, r0
    or      r0, r2
    or      r2, r14             ! FACE_DETAIL | FACE_DETAIL_WHOLE | slot
    mov.l   @(20, r6), r0
    add     #1, r0
    bra     .Lflags\@
    mov.l   r0, @(20, r6)
    .align 2
.LdetailWhole\@: .long  0x28000000  ! FACE_DETAIL | FACE_DETAIL_WHOLE
.LtexMask\@:     .short 0x3FFF      ! FACE_TEXTURE
.LmaxKeys\@:     .short 1088        ! DETAIL_MAX_KEYS
.Lmiss\@:
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   r6, @-r15
    mov.l   @(48, r6), r3
    mov     r0, r4
    mov.w   @(4, r1), r0
    extu.w  r0, r5              ! the texture's size
    jsr     @r3
    mov     #0, r6              ! whole
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
    tst     r5, r5
    bf      .Lrquad
    bra     .LrquadsDone
    nop

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
    BACKFACE .LrquadNext
    mov.l   @(8, r4), r0
    extu.w  r0, r7
    VERTEX  r7
    CULL    r7, .LrquadCulled
    ZMAX    r7
    mov.l   @r4, r14
    mov.l   @(12, r6), r0
    or      r0, r14             ! flags
    WHOLE
    LINK
    mov.l   @(8, r4), r1
    ROOM_FACE r1
    bra     .LrquadNext
    nop

    ! culled: corners behind the near plane but not beyond a frame edge go
    ! to C, which splits the quad
.LrquadCulled:
    mov     r13, r0
    tst     #CLIP_DISCARD, r0
    bt      1f
    bra     .LrquadNext
    nop
1:  mov.l   @(8, r6), r0
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
    mov.l   @r0, r10

    bra     .LrquadNext
    nop
.LrquadsDone:
    ROOM_LEAVE

    .global _faceAddRoomTrianglesAsm
_faceAddRoomTrianglesAsm:
    sts.l   pr, @-r15
    ENTER
    tst     r5, r5
    bf      .Lrtri
    bra     .LrtrisDone
    nop

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
    CULL    r3, .LrtriNext
    BACKFACE .LrtriNext
    ZMAX    r3
    mov.l   @r4, r14
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

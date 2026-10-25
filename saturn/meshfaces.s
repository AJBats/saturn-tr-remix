! Mesh faces into the master's ordering table (render.cpp faceAddMeshQuads,
! faceAddMeshTriangles): back faces and faces off the frame are dropped, the
! rest added at their mean depth. Far faces drawn flat (gFlatDist) stay in C.
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
! Vertex: int16 x, y; int16 z; uint8 g, clip. Face: flags, next, int16 indices[4].

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

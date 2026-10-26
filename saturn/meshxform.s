! Mesh vertex transform for the master CPU (render.cpp transformMesh): the
! same arithmetic as projectView, clipping to the frame, then each vertex's
! shade in a second pass by the mesh's kind of lighting.
!
! void transformMeshAsm(const MeshVertex* src, int32 count, Vertex* dst, MeshXform* xf)
!
! void renderMeshAsm(const Mesh* mesh, Vertex** base, FaceList* list, uint32 flags)
!   the master's whole mesh (render.cpp renderMeshHere without gFlatDist):
!   meshXformSetup's work, the transform to *base, the faces into list with
!   flags (faces.s), *base moved past the vertices; nothing done unless the
!   vertices fit below MASTER_VERTICES and the faces before list->end. One
!   entry and exit and few stores: every store goes to the bus.
!
! MeshXform (render.cpp):
!   +0  int16 rows[9]       rotation rows: m00 m01 m02 m10 m11 m12 m20 m21 m22
!   +20 int32 tx, ty, tz    translation, << FIXED_SHIFT
!   +32 const uint16* divTable
!   +36 int32 ambient       gLightAmbient
!   +40 int32 lx, ly, lz    the light's direction in mesh space, 128 long
!   +52 int32 range         gLightRange
!   +56 const int8* normals int8 x, y, z, pad per vertex, or NULL
!   +60 const uint8* shades per vertex, or NULL
!   +64 int32 g             the shade of every vertex when neither
!   +68 int32 inside        non-zero when every vertex lands inside the frame
!                           and between the planes: no clamps or clip flags
!
! MeshVertex: int16 x, y, z. Vertex (render.h): int16 x, y; int16 z; uint8 g, clip.

    .equ    CLIP_FRAME,     1
    .equ    CLIP_LEFT,      2
    .equ    CLIP_RIGHT,     4
    .equ    CLIP_TOP,       8
    .equ    CLIP_BOTTOM,    16
    .equ    CLIP_PLANE,     32

    .equ    MESH_LIGHT_NORMALS, 1
    .equ    MESH_LIGHT_SHADES,  2

! r >> 14 and r >> 21, arithmetic: logical, the sign bit's place flipped
! back; r10 holds 1 << (31 - N), r11 its negation
    .macro  SAR14 r
    shlr8   \r
    shlr2   \r
    shlr2   \r
    shlr2   \r
    xor     r10, \r
    add     r11, \r
    .endm
    .macro  SAR21 r
    shlr16  \r
    shlr2   \r
    shlr2   \r
    shlr    \r
    xor     r10, \r
    add     r11, \r
    .endm

    .text
    .align 2
    .global _transformMeshAsm
_transformMeshAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    bsr     transformMeshBody
    nop
    lds.l   @r15+, pr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

! transformMeshAsm's work with its arguments, r0..r14 its own
transformMeshBody:
    tst     r5, r5
    bf      .Lbegin
    bra     .Ldone
    nop
.Lbegin:
    mov.l   r5, @-r15           ! count and dst again for the shade pass
    mov.l   r6, @-r15
    mov.l   @(32, r7), r10      ! divTable
    mov     #68, r0
    mov.l   @(r0, r7), r0
    tst     r0, r0
    bt      .Lloop
    bra     .Lfast
    nop

.Lloop:
    ! z = tz + v . row2
    mov.l   @(28, r7), r0
    lds     r0, macl
    mov     r4, r8
    mov     r7, r9
    add     #12, r9
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mov     #0, r14             ! clip flags
    sts     macl, r11

    ! near and far planes: clamp z, flag the vertex
    mov.l   .LviewMin, r0
    cmp/gt  r0, r11
    bt      1f
    mov     r0, r11
    mov     #CLIP_PLANE, r14
1:  mov.l   .LviewMax, r0
    cmp/ge  r0, r11
    bf      2f
    mov     r0, r11
    mov     #CLIP_PLANE, r14
2:
    ! x = tx + v . row0, y = ty + v . row1, each >> FIXED_SHIFT as 16 bits
    mov.l   @(20, r7), r0
    lds     r0, macl
    mov     r4, r8
    mov     r7, r9
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mov.l   @(24, r7), r0
    sts     macl, r12
    lds     r0, macl
    mov     r4, r8
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    shlr8   r12
    sts     macl, r13
    shlr2   r12
    shlr2   r12
    shlr2   r12
    exts.w  r12, r12
    shlr8   r13
    shlr2   r13
    shlr2   r13
    shlr2   r13
    exts.w  r13, r13

    ! d = divTable[z >> (FIXED_SHIFT + PROJ_SHIFT)] (z is at most VIEW_MAX_F,
    ! inside the table); z >> FIXED_SHIFT to r11
    mov     r11, r0
    shlr16  r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r10), r0
    shlr8   r11
    shlr2   r11
    shlr2   r11
    shlr2   r11

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
    ! store x|y, z|g|clip (the shade pass fills g)
    shll16  r12
    extu.w  r13, r13
    or      r13, r12
    mov.l   r12, @r6
    shll16  r11
    or      r14, r11
    mov.l   r11, @(4, r6)

    add     #6, r4
    dt      r5
    bt      .Lshade
    bra     .Lloop              ! the loop is too long for a conditional branch
    add     #8, r6

    ! the mesh is inside the frame: project only, the translation in r1..r3
    ! and the frame's centre in r14
.Lfast:
    mov.l   @(28, r7), r1       ! tz
    mov.l   @(20, r7), r2       ! tx
    mov.l   @(24, r7), r3       ! ty
    mov.l   .Lcentre, r14
.LfastLoop:
    lds     r1, macl            ! z = tz + v . row2
    mov     r7, r9
    add     #12, r9
    mac.w   @r4+, @r9+
    mac.w   @r4+, @r9+
    mac.w   @r4+, @r9+
    add     #-6, r4
    sts     macl, r11
    lds     r2, macl            ! x = tx + v . row0
    mov     r7, r9
    mac.w   @r4+, @r9+
    mac.w   @r4+, @r9+
    mac.w   @r4+, @r9+
    add     #-6, r4
    sts     macl, r12
    lds     r3, macl            ! y = ty + v . row1
    mac.w   @r4+, @r9+
    mac.w   @r4+, @r9+
    mac.w   @r4+, @r9+          ! r4 at the next vertex
    shlr8   r12
    sts     macl, r13
    shlr2   r12
    shlr2   r12
    shlr2   r12
    exts.w  r12, r12
    shlr8   r13
    shlr2   r13
    shlr2   r13
    shlr2   r13
    exts.w  r13, r13
    mov     r11, r0
    shlr16  r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r10), r0      ! d
    shll2   r11
    shlr16  r11
    shll16  r11                 ! z >> FIXED_SHIFT << 16 (z < 1 << 30)
    muls.w  r0, r12
    sts     macl, r12
    muls.w  r0, r13
    shlr8   r12
    sts     macl, r13
    shlr2   r12
    shlr2   r12
    exts.w  r12, r12
    shlr8   r13
    shlr2   r13
    shlr2   r13
    exts.w  r13, r13
    shll16  r12
    add     r13, r12            ! x << 16 + y, y borrowing from x when below 0,
    add     r14, r12            ! which the centre's carry repays (y + 112 > 0)
    mov.l   r12, @r6
    mov.l   r11, @(4, r6)
    dt      r5
    bf/s    .LfastLoop
    add     #8, r6

.Lshade:
    mov.l   @r15+, r6           ! dst
    mov.l   @r15+, r5           ! count
    add     #6, r6              ! the g byte
    mov.l   @(36, r7), r11      ! ambient
    mov.l   @(56, r7), r4       ! normals
    tst     r4, r4
    bt      .Lnotnormals

    ! by normal: shade = ambient - ((n . l) * range >> 14), >> 8, within 0 .. 31
    mov.l   @(40, r7), r8
    mov.l   @(44, r7), r9
    mov.l   @(48, r7), r10
    mov.l   @(52, r7), r12
    mov     #31, r13
.Lnormal:
    mov.b   @r4+, r0
    muls.w  r0, r8
    mov.b   @r4+, r0
    sts     macl, r1
    muls.w  r0, r9
    mov.b   @r4+, r0
    sts     macl, r2
    muls.w  r0, r10
    add     r2, r1
    sts     macl, r2
    add     r2, r1
    muls.w  r1, r12
    add     #1, r4              ! pad
    sts     macl, r1
    shlr8   r1
    shlr2   r1
    shlr2   r1
    shlr2   r1
    exts.w  r1, r1
    mov     r11, r0
    sub     r1, r0
    shlr8   r0
    exts.w  r0, r0
    cmp/pz  r0
    bt      20f
    mov     #0, r0
20: cmp/gt  r13, r0
    bf      21f
    mov     r13, r0
21: mov.b   r0, @r6
    dt      r5
    bf/s    .Lnormal
    add     #8, r6
    bra     .Ldone
    nop

.Lnotnormals:
    mov.l   @(60, r7), r4       ! shades
    tst     r4, r4
    bt      .Lflat

    ! by the vertex's own shade: (ambient + shade << 5) >> 8, within 0 .. 31
    mov     #31, r13
.Lshades:
    mov.b   @r4+, r0
    extu.b  r0, r0
    shll2   r0
    shll2   r0
    shll    r0
    add     r11, r0
    shlr8   r0
    exts.w  r0, r0
    cmp/pz  r0
    bt      22f
    mov     #0, r0
22: cmp/gt  r13, r0
    bf      23f
    mov     r13, r0
23: mov.b   r0, @r6
    dt      r5
    bf/s    .Lshades
    add     #8, r6
    bra     .Ldone
    nop

.Lflat:
    mov     #64, r0
    mov.l   @(r0, r7), r0
.Lflatloop:
    mov.b   r0, @r6
    dt      r5
    bf/s    .Lflatloop
    add     #8, r6

.Ldone:
    rts
    nop


    .align 2
.LviewMin:      .long   0x00100000  ! VIEW_MIN_F
.LviewMax:      .long   0x14000000  ! VIEW_MAX_F
.LpMax:         .long   0x007FF000  ! projected 2047 << 12
.Lcentre:       .long   0x00B00070  ! FRAME_WIDTH / 2 << 16 | FRAME_HEIGHT / 2
.LhalfWidth:    .short  176         ! FRAME_WIDTH / 2
.LhalfHeight:   .short  112         ! FRAME_HEIGHT / 2
.LframeWidth:   .short  352         ! FRAME_WIDTH
.LframeHeight:  .short  224         ! FRAME_HEIGHT
.LcoordMax:     .short  1023        ! VDP1_COORD_MAX

    .align 2
    .global _renderMeshAsm
_renderMeshAsm:
    mov.b   @(10, r4), r0
    extu.b  r0, r1              ! vCount
    tst     r1, r1
    bt      .LrmNone
    mov.l   @r5, r2
    mov     r1, r3
    shll2   r3
    shll    r3
    add     r2, r3              ! the end of its vertices
    mov.l   .LvertexEnd, r0
    cmp/hi  r0, r3
    bt      .LrmNone
    mov.w   @(12, r4), r0       ! quads and triangles
    mov     r0, r3
    mov.w   @(14, r4), r0
    add     r0, r3
    shll2   r3
    shll2   r3
    mov.l   @r6, r0             ! the list's next face
    add     r0, r3
    mov.l   @(4, r6), r0        ! its end
    cmp/hi  r0, r3
    bf      .LrmGo
.LrmNone:
    rts
    nop

    ! the frame: +0 mesh, +4 base, +8 list, +12 flags, +16 quads, +20 the
    ! MeshXform (r14)
.LrmGo:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    add     #-92, r15
    mov.l   r4, @r15
    mov.l   r5, @(4, r15)
    mov.l   r6, @(8, r15)
    mov.l   r7, @(12, r15)
    mov     r15, r14
    add     #20, r14

    ! rows and translation from the matrix (int32 e00 e01 e02 e03 e10 ..)
    mov.l   .LmatrixPtr, r0
    mov.l   @r0, r3
    mov     r14, r1
    add     #18, r1
    mov.l   @(40, r3), r0
    mov.w   r0, @-r1
    mov.l   @(36, r3), r0
    mov.w   r0, @-r1
    mov.l   @(32, r3), r0
    mov.w   r0, @-r1
    mov.l   @(24, r3), r0
    mov.w   r0, @-r1
    mov.l   @(20, r3), r0
    mov.w   r0, @-r1
    mov.l   @(16, r3), r0
    mov.w   r0, @-r1
    mov.l   @(8, r3), r0
    mov.w   r0, @-r1
    mov.l   @(4, r3), r0
    mov.w   r0, @-r1
    mov.l   @r3, r0
    mov.w   r0, @-r1
    mov.l   @(12, r3), r10      ! tx
    mov.l   @(28, r3), r11      ! ty
    mov.l   @(44, r3), r12      ! tz
    mov.l   r10, @(20, r14)
    mov.l   r11, @(24, r14)
    mov.l   r12, @(28, r14)
    mov.l   .LdivTableAddr, r0
    mov.l   r0, @(32, r14)
    mov.l   .LlightAmbient, r0
    mov.l   @r0, r5
    mov.l   r5, @(36, r14)
    mov.l   .LlightRange, r0
    mov.l   @r0, r6
    mov.l   r6, @(52, r14)

    ! g = clamp(int16((intensity + ambient) >> 8), 0, 31)
    mov.w   @(8, r4), r0
    extu.w  r0, r0
    add     r5, r0
    shlr8   r0
    exts.w  r0, r0
    cmp/pz  r0
    bt      1f
    mov     #0, r0
1:  mov     #31, r1
    cmp/gt  r1, r0
    bf      2f
    mov     r1, r0
2:  mov     r14, r2
    add     #64, r2
    mov.l   r0, @r2             ! g

    ! the light's array, then the quads
    mov.b   @(10, r4), r0
    extu.b  r0, r8              ! vCount
    mov     r4, r7
    add     #20, r7             ! vertices
    mov     r8, r9
    add     r9, r9
    add     r8, r9
    add     r9, r9
    add     r7, r9              ! past them
    mov     #0, r13             ! normals
    mov     #0, r1              ! shades
    mov.b   @(11, r4), r0
    cmp/eq  #MESH_LIGHT_NORMALS, r0
    bf      3f
    mov     r9, r13
    mov     r8, r0
    shll2   r0
    bra     4f
    add     r0, r9
3:  cmp/eq  #MESH_LIGHT_SHADES, r0
    bf      4f
    mov     r9, r1
    mov     r8, r0
    add     #1, r0
    shlr    r0
    add     r0, r0
    add     r0, r9
4:  mov.l   r9, @(16, r15)      ! quads

    ! by normal while there is a range: the light in mesh space, 128 long
    tst     r13, r13
    bt      .LrmFlat
    tst     r6, r6
    bt      .LrmFlat
    mov.l   r13, @(56, r14)
    mov     #0, r0
    mov.l   r0, @(60, r14)
    mov.l   .LlightView, r0
    mov.l   @r0, r5
    mov.l   @(4, r0), r6
    mov.l   @(8, r0), r1
    mov.w   .Lsar21Bit, r10
    neg     r10, r11
    mov.l   @r3, r0             ! e00 e10 e20
    muls.w  r0, r5
    mov.l   @(16, r3), r0
    sts     macl, r2
    muls.w  r0, r6
    mov.l   @(32, r3), r0
    sts     macl, r12
    muls.w  r0, r1
    add     r12, r2
    sts     macl, r12
    add     r12, r2
    SAR21   r2
    mov.l   r2, @(40, r14)
    mov.l   @(4, r3), r0        ! e01 e11 e21
    muls.w  r0, r5
    mov.l   @(20, r3), r0
    sts     macl, r2
    muls.w  r0, r6
    mov.l   @(36, r3), r0
    sts     macl, r12
    muls.w  r0, r1
    add     r12, r2
    sts     macl, r12
    add     r12, r2
    SAR21   r2
    mov.l   r2, @(44, r14)
    mov.l   @(8, r3), r0        ! e02 e12 e22
    muls.w  r0, r5
    mov.l   @(24, r3), r0
    sts     macl, r2
    muls.w  r0, r6
    mov.l   @(40, r3), r0
    sts     macl, r12
    muls.w  r0, r1
    add     r12, r2
    sts     macl, r12
    add     r12, r2
    SAR21   r2
    bra     .LrmInside
    mov.l   r2, @(48, r14)
.LrmFlat:
    mov     #0, r0
    mov.l   r0, @(56, r14)
    mov.l   r1, @(60, r14)

    ! inside: the centre's view position, its bounding radius wholly in the
    ! frame and between the planes
.LrmInside:
    mov.l   .Lsar14Bit, r10
    neg     r10, r11
    mov.l   @r15, r4            ! mesh: its centre +0, bound +16
    mov     r14, r2             ! the rows
    mov.l   @(20, r14), r0
    lds     r0, macl
    mov     r4, r1
    mac.w   @r1+, @r2+
    mac.w   @r1+, @r2+
    mac.w   @r1+, @r2+
    mov.l   @(24, r14), r0
    sts     macl, r5            ! cx
    lds     r0, macl
    mov     r4, r1
    mac.w   @r1+, @r2+
    mac.w   @r1+, @r2+
    mac.w   @r1+, @r2+
    mov.l   @(28, r14), r0
    sts     macl, r6            ! cy
    lds     r0, macl
    mov     r4, r1
    mac.w   @r1+, @r2+
    mac.w   @r1+, @r2+
    mac.w   @r1+, @r2+
    SAR14   r5
    SAR14   r6
    sts     macl, r12           ! cz
    SAR14   r12
    mov.w   @(16, r4), r0
    mov     r0, r1              ! radius
    mov     #0, r13             ! inside
    mov     r12, r2
    sub     r1, r2              ! zNear
    mov     #64, r0
    cmp/gt  r0, r2
    bf      .LrmTested
    mov     r12, r0
    add     r1, r0
    mov.w   .LviewDist, r3
    cmp/ge  r3, r0
    bt      .LrmTested
    cmp/pz  r5
    bt      5f
    neg     r5, r5
5:  add     r1, r5
    shll8   r5
    mov     #-84, r0
    extu.b  r0, r0              ! 172: FRAME_WIDTH / 2 - 4
    muls.w  r0, r2
    sts     macl, r0
    cmp/gt  r5, r0
    bf      .LrmTested
    cmp/pz  r6
    bt      6f
    neg     r6, r6
6:  add     r1, r6
    shll8   r6
    mov     #108, r0            ! FRAME_HEIGHT / 2 - 4
    muls.w  r0, r2
    sts     macl, r0
    cmp/gt  r6, r0
    movt    r13
.LrmTested:
    mov     r14, r2
    add     #64, r2
    mov.l   r13, @(4, r2)       ! inside

    ! the vertices
    mov     r7, r4
    mov     r8, r5
    mov.l   @(4, r15), r0
    mov.l   @r0, r6
    bsr     transformMeshBody
    mov     r14, r7

    ! the faces, as faces.s's bodies take them: vertices r8, base r9, the
    ! next face r10, ot r11, groups r12; flags +12 and inside +16 from r6
    mov.l   @(4, r15), r0
    mov.l   @r0, r8
    mov.l   .LvertexBase, r9
    shlr2   r9
    shlr    r9
    neg     r9, r9
    mov.l   @(8, r15), r0
    mov.l   @r0, r10
    mov.l   @(8, r0), r11
    mov     r0, r12
    add     #12, r12
    mov     r15, r6
    add     #72, r6             ! +16: the MeshXform's inside
    mov.l   @(12, r15), r0
    mov.l   r0, @(12, r6)       ! over g, which the transform has used
    mov.l   @r15, r1
    mov.w   @(12, r1), r0
    mov     r0, r5
    cmp/pl  r5
    bf      7f
    mov.l   .LquadsBody, r0
    jsr     @r0
    mov.l   @(16, r15), r4
7:  mov.l   @r15, r1
    mov.w   @(12, r1), r0
    mov     r0, r2
    add     r2, r2
    add     r0, r2
    add     r2, r2              ! the quads' bytes
    mov.l   @(16, r15), r4
    add     r2, r4
    mov.w   @(14, r1), r0
    cmp/pl  r0
    bf      8f
    mov.l   .LtrianglesBody, r1
    jsr     @r1
    mov     r0, r5
8:  mov.l   @(8, r15), r0
    mov.l   r10, @r0            ! the list's next face
    mov.l   @r15, r1
    mov.b   @(10, r1), r0
    extu.b  r0, r0
    shll2   r0
    shll    r0
    add     r8, r0
    mov.l   @(4, r15), r1
    mov.l   r0, @r1             ! base past the vertices

    add     #92, r15
    lds.l   @r15+, pr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    .align 2
.LvertexEnd:    .long   _gVertices + 2048 * 8   ! MASTER_VERTICES
.LvertexBase:   .long   _gVertices
.LmatrixPtr:    .long   _gMatrixPtr
.LdivTableAddr: .long   _divTable
.LlightAmbient: .long   _gLightAmbient
.LlightRange:   .long   _gLightRange
.LlightView:    .long   _gLightView
.LquadsBody:    .long   _faceAddMeshQuadsBody
.LtrianglesBody: .long  _faceAddMeshTrianglesBody
.Lsar14Bit:     .long   0x00020000  ! 1 << (31 - 14)
.Lsar21Bit:     .short  0x0400      ! 1 << (31 - 21)
.LviewDist:     .short  20480       ! VIEW_DIST

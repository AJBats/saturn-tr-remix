! Stage 3 the new way (faces.h): a room's faces straight to VDP1 commands.
!
! void facesRoomAsm(const FaceRoom* room, RoomRun* run)
!
! A room's faces as faces.cpp facesRoomC adds them: its quads in their runs
! (of the runs square to an axis, those facing the camera, without a back
! face test), then its triangles. Each face kept has its command and gouraud
! table written at once, chained into its depth bucket. A quad decides its turned lines and mip
! level on frames of its parity and keeps them in its flags' top byte in
! between (faces.s REUSE); triangles decide every frame. A texture the cache
! holds is drawn from it, else in its mean colour. A quad cut by a plane or
! near the camera goes to C (RoomRun.careful), which may split it; a face
! wholly past the far plane is a transparent fill behind everything. Near the
! camera a face is split as the original split it (.Lnear, faces.cpp); one a
! plane cuts, or with a corner beyond the frame's coordinates, in C. The run
! is at GBR as the faces go, the list's cursors in registers.
!
! FaceRoom, RoomRun, RoomVariant, RoomMaterial: faces.h. A material keeps its
! characters a frame's faces found in the cache for the frame's other faces.
! RoomQuad: uint32 flags; uint16 indices[4], byte offsets into the room's
! vertices. Vertex: int16 x, y; int16 z; uint8 g, clip.
! CommandList: +0 cmd, +4 end, +8 grd, +12 link, +16 grdLink.
! ShadeCache: +0 frame, +4 validFrom, +8 vram, +12 miss(cache, key, slot),
! +16 keys[256], +1040 frames[256].
!
! Through a face: r1, r2, r3, r7 its corners A B C D; r4 the face; r5 the
! faces left; r9 its command, r10 its gouraud table, r11 its link, r12 its
! last word; r0, r6, r8, r13, r14 as each step says. The stack: +0 the
! corners' clip flags ored, +4 a material's word 2's place while it is
! looked up, +8 the room, +12 the run's first quad (its index), +16 the
! run, +20 its quads.

    .equ    CLIP_FRAME,     0x01
    .equ    CLIP_DISCARD,   0x3E
    .equ    CLIP_EDGES,     0x1E
    .equ    CLIP_PLANE,     0x20

    .equ    RUN_VERTICES,   0
    .equ    RUN_MATERIALS,  4
    .equ    RUN_FRAME,      8
    .equ    RUN_LOD,        12
    .equ    RUN_PARITY,     16
    .equ    RUN_END,        20
    .equ    RUN_HEADS,      24
    .equ    RUN_TAILS,      28
    .equ    RUN_SHADE_GOURAUD, 32
    .equ    RUN_SHADE,      36
    .equ    RUN_SHADE_LEVEL, 40
    .equ    RUN_LIST,       44
    .equ    RUN_MISS,       48
    .equ    RUN_CAREFUL,    52
    .equ    RUN_PLAIN,      56
    .equ    RUN_VARIANTS,   60
    .equ    RUN_FAR_BUCKET, 268
    .equ    RUN_CAREFUL_TRI, 272
    .equ    RUN_NEAR,       332
    .equ    RUN_VOID_WAY,   336     ! faces.h FaceVoidWay
    .equ    RUN_BOX_NEAR,   340
    .equ    RUN_BOX_COUNT,  344
    .equ    RUN_BOXES,      348
    .equ    RUN_POINTS,     352
    .equ    RUN_FULL_OUT,   356
    .equ    RUN_MARGIN,     360
    .equ    RUN_PIECE_MASK, 364
    .equ    RUN_SPLIT_FAR,  368
    .equ    BOX_BYTES,      20      ! RoomBox
    .equ    VOIDS_NONE,     2
    .equ    CLIP_LEFT,      2
    .equ    CLIP_RIGHT,     4
    .equ    CLIP_TOP,       8
    .equ    CLIP_BOTTOM,    16

    .equ    ROOM_QUADS,     0
    .equ    ROOM_TRIANGLES, 4
    .equ    ROOM_POINTS,    8
    .equ    ROOM_COUNTS,    16  ! quads, triangles (uint16)
    .equ    ROOM_FACING,    20  ! uint16 [6]
    .equ    ROOM_CAMERA,    32  ! int32 [3]

    .equ    SPARSE_LINES,   64  ! faces.cpp's

    .equ    SHADING_FAR,    1
    .equ    SHADING_PLAIN,  2

! the face at r4's corners: A B C to r1 r2 r3, D to r7 (a triangle's C); r0
    .macro  CORNERS tri
    mov.l   @(RUN_VERTICES, gbr), r0
    mov.l   @(4, r4), r1        ! A << 16 | B
    mov.l   @(8, r4), r7        ! C << 16 | D
    extu.w  r1, r2
    shlr16  r1
    add     r0, r1
    add     r0, r2
    mov     r7, r3
    shlr16  r3
    add     r0, r3
    .if \tri
    mov     r3, r7
    .else
    extu.w  r7, r7
    add     r0, r7
    .endif
    .endm

! A (r1), B (r2), C (r3) wind the wrong way on the frame: to 'skip'; r0, r6,
! r8, r13
    .macro  BACKFACE skip
    mov.w   @r1, r6             ! ax
    mov.w   @r2, r13
    mov.w   @r3, r8
    sub     r6, r13             ! bx - ax
    sub     r6, r8              ! cx - ax
    mov.l   @r1, r6             ! ay, in its low word: multiplies read no more
    mov.l   @r3, r0
    sub     r6, r0              ! cy - ay
    muls.w  r13, r0
    mov.l   @r2, r13
    sub     r6, r13             ! by - ay
    sts     macl, r6
    muls.w  r8, r13
    sts     macl, r0
    cmp/gt  r0, r6
    bf      \skip
    .endm

! culled wholly beyond a frame edge or with a corner behind the near plane:
! to 'culled', r6's low byte the corners' clip flags anded; else r13's them
! ored: each corner's last longword (z, shade, clip flags) anded and ored,
! loaded ahead of its use; r0, r8
    .macro  CULL tri, culled
    mov.l   @(4, r1), r6
    mov.l   @(4, r2), r0
    mov     r6, r13
    and     r0, r6
    or      r0, r13
    mov.l   @(4, r3), r0
    .ifeq \tri
    mov.l   @(4, r7), r8
    .endif
    and     r0, r6
    or      r0, r13
    .ifeq \tri
    and     r8, r6
    or      r8, r13
    .endif
    mov     r6, r0
    tst     #CLIP_DISCARD, r0
    bf      \culled
    mov     r13, r0
    tst     #CLIP_PLANE, r0
    bf      \culled
    .endm

! acc = max(acc, |a.x - b.x|, |a.y - b.y|), the steps along edge a b; r0, t
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

! r13 the variant (mip level * 2 + turned) the face at r4 with corners r1,
! r2, r3, r7 and material r14 takes: turned when that makes fewer lines, at
! the first mip level with about a texel a pixel along them (faces.s TURN and
! WHOLE); r0, r6, r8. With a decision kept in the face's top byte, the other
! way must save an eighth of the lines: a face about square on the frame
! would otherwise turn back and forth as the camera moves, and the texels
! Fast leaves out streak along its lines, so its grain would turn with them.
! A sparse texture over too many lines at full res: to 'drop', nothing kept
! for it.
    .macro  DECIDE drop
    mov     #0, r13
    SPAN    r1, r7, r13, r8     ! AD
    SPAN    r2, r3, r13, r8     ! BC: the lines' length turned
    mov     #0, r6
    SPAN    r1, r2, r6, r8      ! AB
    SPAN    r7, r3, r6, r8      ! DC: ... as it is
    mov     r13, r8             ! the lines: as many as AD, BC span
    mov.l   @(RUN_MARGIN, gbr), r0
    tst     r0, r0
    bt      63f                 ! no margin (the debug menu's Turn margin)
    mov.b   @r4, r0
    tst     #8, r0
    bt      63f                 ! none kept: the fewer lines
    tst     #1, r0
    bt/s    62f                 ! kept as it is
    mov     r13, r0
    mov     r6, r0              ! kept turned: as it is must save an eighth
    shlr2   r0
    shlr    r0
    bra     61f
    add     r13, r0             ! AD, BC's span + an eighth of AB, DC's
62: shlr2   r0                  ! kept as it is: turned must save an eighth
    shlr    r0
    neg     r0, r0
    bra     61f
    add     r13, r0             ! AD, BC's span less an eighth
63: mov     r13, r0
61: cmp/gt  r6, r0
    bf/s    1f
    mov     #0, r0
    mov     r6, r8              ! turned: as many as AB, DC span
    mov     r13, r6             ! turned: the lines as long as AD, BC
    mov     #1, r0
1:  mov     r0, r13
    mov.l   @(RUN_LOD, gbr), r0
    tst     r0, r0
    bt      9f                  ! full res only
    mulu.w  r0, r6              ! lod x the lines' length
    mov     r13, r0
    add     r0, r0
    add     #14, r0
    mov.w   @(r0, r14), r0
    extu.w  r0, r6              ! 4 x the texels along a line
    sts     macl, r0
    cmp/hi  r0, r6
    bf      8f                  ! about a texel a pixel at full res
    mov.b   @(12, r14), r0      ! the mip levels it takes
    tst     r0, r0
    bt      9f
    add     #2, r13             ! level 1
    sts     macl, r0
    shll    r0
    cmp/hi  r0, r6
    bf      9f
    mov.b   @(12, r14), r0
    cmp/eq  #1, r0
    bt      9f
    bra     9f
    add     #2, r13             ! level 2
8:  mov.b   @(13, r14), r0
    tst     #2, r0
    bt      9f
    mov     #SPARSE_LINES, r0
    cmp/gt  r8, r0
    bt      9f
    mov     #0, r0
    mov.b   r0, @r4             ! not kept: decided again next frame
    bra     \drop
    nop
9:
    .endm

! r14 the material of the face at r4; r0
    .macro  MATERIAL
    mov.l   @r4, r14
    shll16  r14
    shll2   r14
    shlr8   r14
    shlr2   r14
    shlr2   r14                 ! its texture * 64
    mov.l   @(RUN_MATERIALS, gbr), r0
    add     r0, r14
    .endm

! r6 the bucket of the corners' largest z, doubled (a triangle's C twice,
! wherever turning and mirroring put it); r0
    .macro  BUCKET
    mov.w   @(4, r1), r0
    mov     r0, r6
    mov.w   @(4, r2), r0
    cmp/gt  r6, r0
    bf      1f
    mov     r0, r6
1:  mov.w   @(4, r3), r0
    cmp/gt  r6, r0
    bf      2f
    mov     r0, r6
2:  mov.w   @(4, r7), r0
    cmp/gt  r6, r0
    bf      3f
    mov     r0, r6
3:  shlr2   r6
    shlr2   r6
    add     r6, r6
    .endm

! the command's first word r8 linked to the last in bucket r6 (doubled), and
! the bucket's last made this; its last word; r0, r14
    .macro  LINK
    mov.l   @(RUN_HEADS, gbr), r0
    add     r6, r0              ! the bucket's head
    mov.w   @r0, r14
    extu.w  r14, r14
    mov.w   r11, @r0            ! this the bucket's last
    tst     r14, r14
    or      r14, r8
    bf/s    1f
    mov.l   r8, @r9             ! w0
    mov.l   @(RUN_TAILS, gbr), r0
    add     r6, r0
    mov.w   r11, @r0            ! and its first
1:  mov.l   r12, @(28, r9)      ! w7: the gouraud table
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

! corner v's shade to its gouraud colour in place, r6 shadeGouraud; r0
    .macro  SHADE v
    mov.b   @(6, \v), r0
    extu.b  r0, r0
    add     r0, r0
    mov.w   @(r0, r6), \v
    .endm

! the gouraud table of colours r1, r2, r3, r7
    .macro  TABLE
    shll16  r1
    or      r2, r1
    mov.l   r1, @r10
    shll16  r3
    or      r7, r3
    mov.l   r3, @(4, r10)
    .endm

! on to the next command: T set when the list is full; r0
    .macro  NEXT
    add     #32, r9
    add     #8, r10
    add     #4, r11
    mov     #1, r0
    shll16  r0
    add     r0, r12
    mov.l   @(RUN_END, gbr), r0
    cmp/eq  r0, r9
    .endm

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

! corner v's gouraud colour under water in place (render.cpp waterGouraud):
! its shade's level, red losing 2 for every 1024 beyond the first 1024 of
! depth; r6 shadeLevel, r14 1024; r0, r8
    .macro  WATER v
    mov.b   @(6, \v), r0
    extu.b  r0, r0
    mov.b   @(r0, r6), r8       ! the level
    mov.w   @(4, \v), r0
    sub     r14, r0             ! z - 1024
    cmp/pl  r0
    bf/s    1f
    mov     r8, \v              ! red
    shlr8   r0
    shlr2   r0
    add     r0, r0
    sub     r0, \v
    cmp/pz  \v
    bt      1f
    mov     #0, \v
1:  mov     r8, r0
    shll2   r0
    shll2   r0
    shll    r0
    or      r8, r0
    shll2   r0
    shll2   r0
    shll    r0
    or      r0, \v              ! level << 10 | level << 5 | red
    .endm

! least (lo) and most (hi) of the coordinate at 'disp' of vertex v; r0
    .macro  RANGE v, disp, lo, hi
    mov.w   @(\disp, \v), r0
    cmp/gt  r0, \lo
    bf      1f
    mov     r0, \lo
1:  cmp/gt  \hi, r0
    bf      2f
    mov     r0, \hi
2:
    .endm

! A run of quads (tri 0: facing, the camera on their front side, so no back
! face test) or the triangles (tri 1), labels ending in sfx; when they're
! done, on to 'done'.
    .macro  FACES sfx, tri, facing, done

    ! culled: wholly past the far plane, filled; a quad cut by a plane, to C;
    ! a triangle cut by one, filled (back faces gone before)
.Lculled\sfx:                   ! r6 the corners' clip flags anded
    mov     r6, r0
    tst     #CLIP_EDGES, r0
    bf      .Lnext\sfx          ! wholly beyond a frame edge
    tst     #CLIP_PLANE, r0
    bt      .Lcut\sfx
    mov.l   @(RUN_FAR_BUCKET, gbr), r0
    mov     r0, r6              ! the far plane's bucket
    mov.w   @(4, r1), r0
    shlr2   r0
    shlr2   r0
    cmp/eq  r6, r0
    bf      .Lwithin\sfx        ! all at the near plane
1:  bsr     .Lvoid
    nop
    bf      .Lnext\sfx
    bra     .LdoneNear
    nop

.Lwithin\sfx:                   ! left out, unless C splits what's near
    .ifdef RENDER_LAB
    mov.l   @(RUN_CAREFUL_TRI, gbr), r0
    tst     r0, r0
    bt      .Lnext\sfx
    .endif
    .if \tri
    bra     .LcarefulTri\sfx
    nop
.Lcut\sfx:                      ! filled, unless C splits it
    .ifdef RENDER_LAB
    mov.l   @(RUN_CAREFUL_TRI, gbr), r0
    tst     r0, r0
    bt      1b
    .endif
.LcarefulTri\sfx:
    bsr     .LcarefulTri
    nop
    bf      .Lnext\sfx
    bra     .LdoneNear
    nop
    .else
.Lcut\sfx:
.Lcareful\sfx:
    bsr     .Lcareful
    nop
    bf      .Lnext\sfx
    bra     .LdoneNear
    nop
    .endif

    ! wound the wrong way on the frame: it is, unless the near plane holds a
    ! corner, which can turn a quad over (the original tests only those with
    ! none near); then C asks in room space
.LfaceClose\sfx:                 ! r0 corner A's z >> 8
    .ifdef RENDER_LAB
    mov     r0, r6
    .endif
    mov.l   @(RUN_NEAR, gbr), r0
    .ifdef RENDER_LAB
    tst     r0, r0
    bt      .LfaceOurs\sfx
    .endif
    jsr     @r0
    nop
    tst     r0, r0
    bf      1f
    bra     .Lnext\sfx          ! split
    nop
1:  cmp/pz  r0
    bt      2f
    bra     .LdoneNear          ! and the list is full
    nop
2:  cmp/eq  #1, r0
    bf      3f
    bra     .LFaceMaterial\sfx
    nop
3:
    .if \tri
    bra     .LcarefulTri\sfx
    nop
    .else
    bra     .Lcareful\sfx
    nop
    .endif
    .ifdef RENDER_LAB
.LfaceOurs\sfx:
    .ifeq \tri
    mov     r6, r0
    tst     #0xF8, r0
    bt      .Lcareful\sfx       ! corner A nearer than 2048: C decides on a split
    .endif
    bra     .LFaceMaterial\sfx
    nop
    .endif

.LfaceWound\sfx:
    .ifdef RENDER_LAB
    mov.l   @(RUN_NEAR, gbr), r0
    tst     r0, r0
    bt      .Lnext\sfx
    .endif
    mov.l   @(4, r1), r6
    mov.l   @(4, r2), r0
    or      r0, r6
    mov.l   @(4, r3), r0
    or      r0, r6
    mov.l   @(4, r7), r0
    or      r6, r0
    tst     #CLIP_PLANE, r0
    bt      .Lnext\sfx
    .if \tri
    bra     .LcarefulTri\sfx
    nop
    .else
    bra     .Lcareful\sfx
    nop
    .endif

    ! corner A far enough to be behind a solid box
.LfaceBoxes\sfx:
    mov.l   .LbehindAt\sfx, r0
    jsr     @r0
    nop
    bf      1f
    bra     .Lnext\sfx
    nop
1:  mov.w   @(4, r1), r0
    bra     .LFaceSeen\sfx
    nop
    .align 2
.LbehindAt\sfx: .long   .Lbehind

.LtoPlain\sfx:
    bra     .Lplain\sfx
    nop

.Lnext\sfx:
    dt      r5
    bf/s    .Lface\sfx
    add     #12, r4
    bra     \done
    nop

.Lface\sfx:
    CORNERS \tri
    .ifeq \facing
.LFaceBack\sfx:
    BACKFACE .LfaceWound\sfx
    .endif
.LFaceCull\sfx:
    CULL    \tri, .Lculled\sfx
.LFaceNear\sfx:
    mov.l   r13, @r15           ! the clip flags ored
    mov.l   @(RUN_BOX_NEAR, gbr), r0
    mov     r0, r6
    mov.w   @(4, r1), r0
    cmp/ge  r6, r0
    bt      .LfaceBoxes\sfx
.LFaceSeen\sfx:
    shlr8   r0
    tst     #0xF0, r0
    bt      .LfaceClose\sfx     ! corner A within the distance faces.cpp splits at
.LFaceMaterial\sfx:
    MATERIAL
    ! too dark to see? Corner A settles it for most faces
    mov.b   @(13, r14), r0
    extu.b  r0, r0
    shlr2   r0
    mov     r0, r8
    mov.b   @(6, r1), r0
    extu.b  r0, r0
    cmp/hs  r8, r0
    bf      .LFaceLit\sfx
    mov.l   .LdarkAt\sfx, r0
    jsr     @r0
    nop
    tst     r0, r0
    bt      .LFaceLit\sfx
    cmp/pz  r0
    bt      1f
    bra     .LdoneNear
    nop
1:  bra     .Lnext\sfx
    nop
    .align 2
.LdarkAt\sfx:   .long   .Ldark
.LFaceLit\sfx:
    mov.w   @(8, r14), r0
    cmp/pz  r0
    bf      .LtoPlain\sfx      ! a texture the cache can't take

    .if \tri
    DECIDE  .Lnext\sfx
    mov     r13, r0
    or      #8, r0
    mov.b   r0, @r4             ! kept for DECIDE's margin (a triangle decides every frame)
    mov     r13, r6
    .else
.LFaceReuse\sfx:
    ! the turn and mip level kept from the quad's last frame to decide, or
    ! decided now and kept
    mov.l   @(RUN_PARITY, gbr), r0
    xor     r5, r0
    tst     #1, r0
    bt      .Ldecide\sfx        ! the quad's frame to decide
    mov.b   @r4, r0
    tst     #8, r0
    bt      .Ldecide\sfx        ! none kept yet
    and     #7, r0
    bra     .Lvariant\sfx
    mov     r0, r6
.Ldecide\sfx:
    DECIDE  .Lnext\sfx
    mov     r13, r0
    or      #8, r0
    mov.b   r0, @r4             ! kept in the flags' top byte
    mov     r13, r6
    .endif

    ! the texture from the cache as the frame's faces before found it, else
    ! looked up
.Lvariant\sfx:                  ! r6 the variant
    mov.l   @(32, r14), r8      ! the frame the material's characters are of
    mov.l   @(RUN_FRAME, gbr), r0
    cmp/eq  r0, r8
    bf      .Lfresh\sfx
.Lmemo\sfx:
    mov.l   @(RUN_VARIANTS, gbr), r0
    mov     r6, r13
    shll2   r13
    shll2   r13
    add     r0, r13             ! the variant's
    shll2   r6
    add     #36, r6             ! its word 2's place in the material
    mov     r6, r0
    mov.l   @(r0, r14), r8      ! this frame's, 0 not looked up yet
    tst     r8, r8
    bt      .Llookup\sfx
.Lcached\sfx:
    mov.l   r8, @(8, r9)        ! w2
    mov     r6, r0
    add     #-36, r0            ! 4 x the variant
    and     #0xF8, r0
    shlr    r0                  ! 4 x its mip level: its colour table follows the texture's
    mov.l   @(4, r14), r8
    add     r0, r8
    mov.l   r8, @(4, r9)        ! w1
    mov.b   @(13, r13), r0
    tst     r0, r0
    bt/s    1f
    mov.l   @r14, r8            ! w0
    mov     r2, r0              ! turned: A D C B
    mov     r7, r2
    mov     r0, r7
1:  mov.b   @(14, r13), r0
    bra     .Ltail\sfx
    mov     r0, r13             ! the shading

.Lfresh\sfx:                    ! r0 the frame
    mov.l   r0, @(32, r14)
    mov     #0, r0
    mov.l   r0, @(36, r14)
    mov.l   r0, @(40, r14)
    mov.l   r0, @(44, r14)
    mov.l   r0, @(48, r14)
    mov.l   r0, @(52, r14)
    mov.l   r0, @(60, r14)
    bra     .Lmemo\sfx
    mov.l   r0, @(56, r14)

    ! the variant's slot for the texture in the cache, drawn this frame;
    ! else its loader's
.Llookup\sfx:                   ! r6 the word's place, r13 the variant's
    mov.l   r6, @(4, r15)
    mov.l   @r13, r6            ! the slots + 1 by key
    mov.w   @(8, r14), r0
    mov.b   @(r0, r6), r0
    extu.b  r0, r0
    tst     r0, r0
    bt      .Lmiss\sfx
.Lslot\sfx:                     ! r0 the slot + 1
    add     #-1, r0
    mov     r0, r6
    shll2   r0
    shll    r0
    mov.l   @(4, r13), r8
    add     r0, r8
    mov.l   @(RUN_FRAME, gbr), r0
    mov.l   r0, @r8             ! drawn this frame
    mov.w   @(10, r13), r0      ! a slot's size / 8
    mulu.w  r0, r6
    mov.w   @(8, r13), r0
    extu.w  r0, r8              ! the pool / 8
    sts     macl, r0
    add     r0, r8              ! the character / 8
    shll16  r8
    mov.b   @(12, r13), r0
    mov.w   @(r0, r14), r0
    extu.w  r0, r0              ! the variant's size
    or      r0, r8              ! w2
    mov.l   @(4, r15), r6
    mov     r6, r0
    bra     .Lcached\sfx
    mov.l   r8, @(r0, r14)      ! for the frame's other faces

.Lmiss\sfx:
    bsr     .Lmiss
    nop
    tst     r0, r0
    bf      .Lslot\sfx
    ! no room at this level: the next one down, if the texture has one; past
    ! the last, full res if the frame's full-res pool hasn't run out
    mov.l   @(4, r15), r6
    add     #-36, r6
    shlr2   r6                  ! the variant
    mov     r6, r8
    shlr    r8                  ! its level
    mov.b   @(12, r14), r0
    cmp/ge  r0, r8
    bf      1f
    tst     r8, r8
    bt      4f                  ! full res was the last it has
    mov.l   @(RUN_FULL_OUT, gbr), r0
    mov     r0, r8
    mov.l   @(RUN_FRAME, gbr), r0
    cmp/eq  r0, r8
    bt      4f
    mov     r6, r0
    and     #1, r0
    bra     .Lmemo\sfx
    mov     r0, r6
4:  bra     .Lplain\sfx
    nop
1:  tst     r8, r8
    bf      5f
    mov.l   @(RUN_FRAME, gbr), r0
    mov.l   r0, @(RUN_FULL_OUT, gbr)
5:  add     #2, r6
    bra     .Lmemo\sfx
    nop

    ! the command's corners, mirrored when further off the frame on its AD
    ! side, linked, shaded
.Ltail\sfx:                     ! r8 w0, r13 the shading
    mov.l   @r15, r0
    tst     #CLIP_FRAME, r0
    bt      1f
    bsr     .Lmirror
    nop
.LTailBucket\sfx:
1:  BUCKET
.LTailLink\sfx:
    LINK
.LTailCorners\sfx:
    QUAD_CORNERS
.LTailShade\sfx:
    cmp/pz  r13
    bf      .Lwater\sfx
    mov.l   @(RUN_SHADE_GOURAUD, gbr), r0
    mov     r0, r6
    SHADE   r1
    SHADE   r2
    SHADE   r3
    SHADE   r7
    tst     r13, r13
    bf      .LfarOrPlain\sfx
.LTailOne\sfx:
    cmp/eq  r1, r2              ! near: one shade, perhaps a shaded colour table
    bf      .Ltable\sfx
    cmp/eq  r1, r3
    bf      .Ltable\sfx
    cmp/eq  r1, r7
    bf      .Ltable\sfx
    bsr     .LshadedCopy
    nop
    tst     r0, r0
    bf      .Lshaded\sfx
    mov     r1, r2
    mov     r1, r3
    mov     r1, r7
.Ltable\sfx:
    TABLE
.Lshaded\sfx:
.LTailNext\sfx:
    NEXT
    bt      .Lfull\sfx
    bra     .Lnext\sfx
    nop
.Lfull\sfx:
    bra     .LdoneNear
    nop

.LfarOrPlain\sfx:
    mov     r13, r0
    cmp/eq  #SHADING_PLAIN, r0
    bt      .Ltable\sfx
    bsr     .LfarShade
    nop
    bra     .Lshaded\sfx
    nop

.Lwater\sfx:
    bsr     .LwaterShade
    nop
    bra     .Lshaded\sfx
    nop

    ! without the cache: in its mean colour, or left out
.Lplain\sfx:                    ! r14 the material
    mov.b   @(13, r14), r0
    tst     #3, r0
    bf      .Lskip\sfx
    mov.w   @(10, r14), r0
    extu.w  r0, r6              ! the colour
    mov     #-60, r0
    extu.b  r0, r0
    shll16  r0
    or      r6, r0
    mov.l   r0, @(4, r9)        ! w1: (PMOD_NO_ECD_SPD | PMOD_GOURAUD) << 16 | colour
    mov     #0x10, r8
    shll8   r8
    add     #4, r8
    shll16  r8                  ! w0: (CMD_POLYGON | CMD_JUMP) << 16
    mov.l   @(RUN_PLAIN, gbr), r0
    bra     .Ltail\sfx
    mov     r0, r13
.Lskip\sfx:
    mov     #0x50, r8
    shll8   r8
    shll16  r8                  ! w0: CMD_SKIP_JUMP << 16
    BUCKET
    LINK
    NEXT
    bt      .Lfull\sfx
    bra     .Lnext\sfx
    nop
    .endm

! --- the room -----------------------------------------------------------------

    .text
    .align 2
    .global _facesRoomAsm
_facesRoomAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    stc.l   gbr, @-r15
    add     #-24, r15
    ldc     r5, gbr
    mov.l   @(RUN_LIST, gbr), r0
    mov.l   @r0, r9
    mov.l   @(8, r0), r10
    mov.l   @(12, r0), r11
    mov.l   @(16, r0), r12
    mov.l   @(RUN_END, gbr), r0
    cmp/eq  r0, r9
    bt      .Ldone              ! no room left
    mov.l   r4, @(8, r15)
    mov     #0, r0
    mov.l   r0, @(12, r15)
    mov.l   r0, @(16, r15)

    ! the runs square to an axis: of each, the quads the camera lies on the
    ! front side of, the run sorted by its plane's coordinate (rooms.cpp
    ! facingQuads): those above it for a run facing down its axis (odd),
    ! below for the others; then the rest
.Lrun:                          ! r1 the room, r2 the run, r3 its first quad
    mov.l   @(8, r15), r1
    mov.l   @(16, r15), r2
    mov.l   @(12, r15), r3
    mov.l   @(ROOM_QUADS, r1), r4
    mov     r3, r0
    add     r0, r0
    add     r3, r0
    shll2   r0
    add     r0, r4              ! the run's quads
    mov     #6, r0
    cmp/eq  r0, r2
    bf      1f
    mov.w   @(ROOM_COUNTS, r1), r0
    extu.w  r0, r5
    sub     r3, r5              ! the rest
    mov.l   r5, @(20, r15)
    tst     r5, r5
    bt      .LrunDone
    mov.l   @(RUN_FRAME, gbr), r0
    and     #1, r0
    mov.l   r0, @(RUN_PARITY, gbr)
    bra     .LfaceAny
    nop
1:  mov     r2, r0
    add     r0, r0
    add     #ROOM_FACING, r0
    mov.w   @(r0, r1), r0
    extu.w  r0, r5
    mov.l   r5, @(20, r15)      ! the run's quads
    tst     r5, r5
    bt      .LrunDone
    mov     r2, r7
    shlr    r7                  ! its axis
    mov     r7, r0
    shll2   r0
    add     #ROOM_CAMERA, r0
    mov.l   @(r0, r1), r6       ! the camera's coordinate on it
    mov     r2, r0
    tst     #1, r0
    bf      2f
    add     #1, r6
2:  mov.l   @(ROOM_POINTS, r1), r8
    add     r7, r8              ! the points' coordinates on the axis
    mov     #0, r13             ! the first quad not below the camera
    mov     r5, r14             ! ... the search's end
.Lsearch:
    cmp/ge  r14, r13
    bt      .Lfound
    mov     r13, r3
    add     r14, r3
    shlr    r3                  ! the middle
    mov     r3, r0
    add     r0, r0
    add     r3, r0
    shll2   r0
    add     #4, r0
    mov.w   @(r0, r4), r0       ! its corner A: a byte offset in 8-byte vertices
    extu.w  r0, r0
    shlr    r0                  ! ... in the 4-byte points
    mov.b   @(r0, r8), r0
    extu.b  r0, r0
    shll8   r0
    cmp/ge  r6, r0
    bf      5f
    bra     .Lsearch
    mov     r3, r14             ! at or past the camera: the end now
5:  mov     r3, r13
    bra     .Lsearch
    add     #1, r13             ! short of it: past the middle
.Lfound:                        ! r13 the first quad the camera lies below
    mov.l   @(16, r15), r2
    mov.l   @(12, r15), r3
    mov     r2, r0
    tst     #1, r0
    bt      3f
    mov     r13, r5             ! facing down the axis: those below it
    bra     4f
    add     r13, r3             ! the run's end, of all the room's quads
3:  mov     r13, r0             ! the others: those from it
    add     r0, r0
    add     r13, r0
    shll2   r0
    add     r0, r4
    sub     r13, r5
    mov.l   @(20, r15), r0
    add     r0, r3
4:  tst     r5, r5
    bt      .LrunDone
    ! a quad's parity is its index's in the room, as one pass over them all
    ! gave (from the room's end)
    mov.l   @(8, r15), r1
    mov.w   @(ROOM_COUNTS, r1), r0
    xor     r3, r0
    mov     r0, r3
    mov.l   @(RUN_FRAME, gbr), r0
    xor     r3, r0
    and     #1, r0
    mov.l   r0, @(RUN_PARITY, gbr)
    bra     .LfaceFacing
    nop

    ! the run done: on to the next, then the triangles
.LrunDone:
    mov.l   @(12, r15), r3
    mov.l   @(20, r15), r0
    add     r0, r3
    mov.l   r3, @(12, r15)
    mov.l   @(16, r15), r2
    add     #1, r2
    mov.l   r2, @(16, r15)
    mov     #7, r0
    cmp/eq  r0, r2
    bf      .Lrun
    mov.l   @(8, r15), r1
    mov.w   @(ROOM_COUNTS + 2, r1), r0
    extu.w  r0, r5
    tst     r5, r5
    bt      .Ldone
    mov.l   @(ROOM_TRIANGLES, r1), r4
    bra     .LfaceTri
    nop

.Ldone:
    mov.l   @(RUN_LIST, gbr), r0
    mov.l   r9, @r0
    mov.l   r10, @(8, r0)
    mov.l   r11, @(12, r0)
    mov.l   r12, @(16, r0)
    add     #24, r15
    ldc.l   @r15+, gbr
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
    FACES   Facing, 0, 1, .LrunDone

! --- what the faces share ------------------------------------------------------

    ! offFrame(A) + offFrame(D) > offFrame(B) + offFrame(C): the corners
    ! swapped to B A D C, w0 (r8) mirrored; r0, r6, r14
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
    mov     #16, r0
    shll16  r0
    xor     r0, r8              ! CMD_FLIP_H << 16
1:  rts
    nop

    ! a shaded copy of the command's colour table (w1) for gouraud value r1
    ! in the list's cache (flush.s .LshadeTable): w1 written through it, r0
    ! not 0; else r0 0, r1 kept. r0, r2, r3, r6 .. r8
.LshadedCopy:
    mov.l   @(RUN_SHADE, gbr), r0
    tst     r0, r0
    bt      9f
    mov     r0, r6              ! the cache
    mov.l   @(4, r9), r0
    extu.w  r0, r7              ! the table / 8
    mov     r1, r2
    shll2   r2
    shll    r2
    xor     r7, r2
    extu.b  r2, r2              ! its slot, or the other of the pair
    shll8   r7
    shll2   r7
    shll2   r7
    shll2   r7
    shll    r7
    or      r1, r7              ! the key: table << 15 | gouraud value
    mov     r2, r0
    shll2   r0
    add     r6, r0
    mov.l   @(16, r0), r0       ! keys[slot]
    cmp/eq  r7, r0
    bt      5f
    mov     #1, r0
    xor     r0, r2              ! the other
    mov     r2, r0
    shll2   r0
    add     r6, r0
    mov.l   @(16, r0), r0
    cmp/eq  r7, r0
    bf      7f
5:  mov     r2, r3              ! a hit: if still valid, drawn this frame
    shll2   r3
    add     r6, r3
    mov.w   .LframesAt, r0
    add     r0, r3              ! &frames[slot]
    mov.l   @r3, r0
    mov.l   @(4, r6), r8        ! validFrom
    cmp/hs  r8, r0
    bf      7f
    mov.l   @r6, r0
    mov.l   r0, @r3
    mov     r2, r0
    shll2   r0
    shll2   r0
    shll    r0                  ! slot * 32
    mov.l   @(8, r6), r3
    add     r3, r0
    shlr2   r0
    bra     8f
    shlr    r0                  ! the copy / 8
7:  sts.l   pr, @-r15           ! shadeTableMiss(cache, key, slot)
    mov.l   r1, @-r15
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov     r6, r4
    mov     r7, r5
    mov     r2, r6
    mov.l   @(12, r4), r0
    jsr     @r0
    nop
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    mov.l   @r15+, r1
    lds.l   @r15+, pr
    tst     r0, r0
    bt      9f
8:  mov.l   @(4, r9), r2
    mov.l   .LnoGouraud, r3
    and     r3, r2
    or      r0, r2
    rts
    mov.l   r2, @(4, r9)        ! w1 through the copy
9:  rts
    mov     #0, r0

    .align 2
.LnoGouraud:    .long   0xFFF80000  ! w1 without the colour calculation and table
.LframesAt:     .short  1040        ! ShadeCache.frames
    .align 2

    ! a far face's colours r1, r2, r3, r7: within 2 levels (the shades are
    ! greys), their mean at every corner, perhaps a shaded colour table
    ! (flush.s .LfarShade); else their gouraud table; r0, r2, r3, r6 .. r8,
    ! r13, r14
.LfarShade:
    mov.l   @(RUN_SHADE, gbr), r0
    tst     r0, r0
    bt      .LfarTable          ! no shaded tables
    mov     r1, r0
    and     #31, r0
    mov     r0, r6              ! the levels' sum
    mov     r0, r13             ! the darkest
    mov     r0, r14             ! the brightest
    mov     r2, r0
    and     #31, r0
    add     r0, r6
    cmp/gt  r0, r13
    bf      1f
    mov     r0, r13
1:  cmp/gt  r14, r0
    bf      2f
    mov     r0, r14
2:  mov     r3, r0
    and     #31, r0
    add     r0, r6
    cmp/gt  r0, r13
    bf      3f
    mov     r0, r13
3:  cmp/gt  r14, r0
    bf      4f
    mov     r0, r14
4:  mov     r7, r0
    and     #31, r0
    add     r0, r6
    cmp/gt  r0, r13
    bf      5f
    mov     r0, r13
5:  cmp/gt  r14, r0
    bf      6f
    mov     r0, r14
6:  sub     r13, r14
    mov     #2, r0
    cmp/hi  r0, r14
    bt      .LfarTable          ! too far apart
    add     #2, r6
    shlr2   r6                  ! the mean level
    mov     r6, r1
    shll2   r6
    shll2   r6
    shll    r6
    or      r6, r1
    shll2   r6
    shll2   r6
    shll    r6
    or      r6, r1              ! a grey of that level
    sts.l   pr, @-r15
    bsr     .LshadedCopy
    nop
    lds.l   @r15+, pr
    tst     r0, r0
    bf      7f
    mov     r1, r2
    mov     r1, r3
    mov     r1, r7
.LfarTable:
    TABLE
7:  rts
    nop

    ! the gouraud table of corners r1, r2, r3, r7 under water; r0, r6, r8,
    ! r14
.LwaterShade:
    mov.l   @(RUN_SHADE_LEVEL, gbr), r0
    mov     r0, r6
    mov     #4, r14
    shll8   r14                 ! 1024
    WATER   r1
    WATER   r2
    WATER   r3
    WATER   r7
    TABLE
    rts
    nop

.LvoidNone:
    rts
    clrt

    ! the box on the frame of corners r1, r2, r3, r7 (faces.cpp faceVoid): added
    ! to the frame's box and area, then a transparent fill over it behind
    ! everything if that's the way (FaceVoidWay); T set when the list is then
    ! full; r0 .. r3, r6, r8, r13, r14
.Lvoid:
    mov.l   @(RUN_VOID_WAY, gbr), r0
    cmp/eq  #VOIDS_NONE, r0
    bt      .LvoidNone
    mov.w   @r1, r6             ! x: least
    mov     r6, r8              ! ... most
    RANGE   r2, 0, r6, r8
    RANGE   r3, 0, r6, r8
    RANGE   r7, 0, r6, r8
    mov.w   @(2, r1), r0
    mov     r0, r13             ! y: least
    mov     r0, r14             ! ... most
    RANGE   r2, 2, r13, r14
    RANGE   r3, 2, r13, r14
    RANGE   r7, 2, r13, r14
    cmp/pz  r6                  ! on the frame
    bt      1f
    mov     #0, r6
1:  cmp/pz  r13
    bt      2f
    mov     #0, r13
2:  mov     #88, r0
    shll2   r0
    add     #-1, r0             ! FRAME_WIDTH - 1
    cmp/gt  r0, r8
    bf      3f
    mov     r0, r8
3:  mov     #56, r0
    shll2   r0
    add     #-1, r0             ! FRAME_HEIGHT - 1
    cmp/gt  r0, r14
    bf      4f
    mov     r0, r14
4:  cmp/gt  r8, r6
    bt      5f
    cmp/gt  r14, r13
    bt      5f
    mov.l   @(RUN_LIST, gbr), r0
    mov     r0, r1
    mov.l   @(48, r1), r2       ! CommandList's box
    cmp/ge  r2, r6
    bt      1f
    mov.l   r6, @(48, r1)
1:  mov.l   @(52, r1), r2
    cmp/ge  r2, r13
    bt      2f
    mov.l   r13, @(52, r1)
2:  mov.l   @(56, r1), r2
    cmp/gt  r2, r8
    bf      3f
    mov.l   r8, @(56, r1)
3:  mov.l   @(60, r1), r2
    cmp/gt  r2, r14
    bf      4f
    mov.l   r14, @(60, r1)
4:  mov     r8, r2
    sub     r6, r2
    add     #1, r2
    mov     r14, r3
    sub     r13, r3
    add     #1, r3
    mulu.w  r2, r3
    add     #64, r1
    mov.l   @r1, r2
    sts     macl, r3
    add     r3, r2
    mov.l   r2, @r1             ! its area
    mov.l   @(RUN_VOID_WAY, gbr), r0
    tst     r0, r0
    bf      .LvoidNone          ! in the box alone
    bra     6f
    mov     #0x10, r0           ! (CMD_POLYGON | CMD_JUMP) >> 8
5:  mov.l   @(RUN_VOID_WAY, gbr), r0
    tst     r0, r0
    bf      .LvoidNone
    mov     #0x50, r0           ! empty: skipped
6:  shll8   r0
    or      #4, r0
    shll16  r0
    mov.l   r0, @r9             ! w0, for LINK
    mov     #-64, r0
    extu.b  r0, r0
    shll16  r0
    mov.l   r0, @(4, r9)        ! w1: PMOD_NO_ECD_SPD << 16
    extu.w  r14, r14
    shll16  r6
    shll16  r8
    mov     r6, r0
    or      r13, r0
    mov.l   r0, @(12, r9)       ! x0 y0
    mov     r8, r0
    or      r13, r0
    mov.l   r0, @(16, r9)       ! x1 y0
    mov     r8, r0
    or      r14, r0
    mov.l   r0, @(20, r9)       ! x1 y1
    mov     r6, r0
    or      r14, r0
    mov.l   r0, @(24, r9)       ! x0 y1
    mov.l   @r9, r8
    mov     #5, r6
    shll8   r6
    add     r6, r6              ! the far plane's bucket, doubled
    LINK
    NEXT
    rts
    nop

    ! the quad (or triangle) at r4 to C (RoomRun.careful, carefulTri), the
    ! cursors through the list; T set when it is then full; r0 .. r3, r6, r7
.LcarefulTri:
    mov.l   @(RUN_CAREFUL_TRI, gbr), r0
    bra     1f
    mov     r0, r6
.Lcareful:
    mov.l   @(RUN_CAREFUL, gbr), r0
    mov     r0, r6
1:  sts.l   pr, @-r15
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   @(RUN_LIST, gbr), r0
    mov.l   r9, @r0
    mov.l   r10, @(8, r0)
    mov.l   r11, @(12, r0)
    mov.l   r12, @(16, r0)
    mov     r4, r5
    stc     gbr, r4
    jsr     @r6
    nop
    mov.l   @(RUN_LIST, gbr), r0
    mov.l   @r0, r9
    mov.l   @(8, r0), r10
    mov.l   @(12, r0), r11
    mov.l   @(16, r0), r12
    mov.l   @(RUN_END, gbr), r0
    cmp/eq  r0, r9
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    lds.l   @r15+, pr
    rts
    nop

    ! the cache missing material r14's texture in variant r13: its loader
    ! (RoomRun.miss) given its key, size and the cache's variant; r0 its slot
    ! + 1, 0 if it can't take it; r6
.Lmiss:
    sts.l   pr, @-r15
    mov.l   r1, @-r15
    mov.l   r2, @-r15
    mov.l   r3, @-r15
    mov.l   r7, @-r15
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.w   @(8, r14), r0
    extu.w  r0, r4              ! the key
    mov.w   @(22, r14), r0
    extu.w  r0, r5              ! the texture's size: its half size's, as it is
    mov.b   @(15, r13), r0
    extu.b  r0, r6
    mov.l   @(RUN_MISS, gbr), r0
    jsr     @r0
    nop
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    mov.l   @r15+, r7
    mov.l   @r15+, r3
    mov.l   @r15+, r2
    mov.l   @r15+, r1
    lds.l   @r15+, pr
    cmp/pz  r0
    bt      1f
    mov     #-1, r0
1:  rts
    add     #1, r0

! --- the faces ------------------------------------------------------------------

    .align 2
    .align 2
    FACES   Any, 0, 0, .LrunDone

    .align 2
.LdoneNear:                     ! .Ldone in reach of all three
    bra     .Ldone
    nop
    .align 2
    FACES   Tri, 1, 0, .Ldone

! --- near the camera, as the original (faces.cpp, issue 263) --------------------

    ! out of the hot code's fixed order (saturn.ld), so that it doesn't move what
    ! follows in the cache
    .section .text.facesnear, "ax"

    ! A quad every corner of it within 4096 (faces.cpp NEAR_SPLIT), none nearer than the plane:
    ! split here. A new corner lies where the view's midpoint does, from the
    ! ends' screen places and depths alone: a + (b - a) zb / (za + zb), one
    ! divide for x and y, ends in address order so that two quads sharing an
    ! edge agree on it.

    .equ    CLIP_HELD,      0x80    ! over 128 px off the frame
    .equ    NEAR_PLANE,     80

    .equ    RUN_QUADS,      276
    .equ    RUN_PIECE_W0,   292
    .equ    RUN_PIECE_W1,   296
    .equ    RUN_PIECE_MATERIAL, 300
    .equ    RUN_WIBBLE,     304
    .equ    RUN_PHASE,      308
    .equ    RUN_WINDOW,     312
    .equ    RUN_TILES,      328

    ! a level's frame on the stack (r4)
    .equ    F_WORD,     0       ! the next piece's word 2
    .equ    F_STEP,     4       ! ... to the one after, << 16
    .equ    F_A,        8       ! its corners
    .equ    F_B,        12
    .equ    F_C,        16
    .equ    F_D,        20
    .equ    F_CLIP,     24      ! not 0: pieces may lie off the frame
    .equ    F_LEVEL,    28      ! 0 quadrants, 1 tiles
    .equ    F_TILES,    32      ! the tiles' character / 8, 0 not looked up, -1 none
    .equ    F_QUARTER,  36      ! the pieces so far
    .equ    F_POINTS,   40      ! AB BC CD DA, centre (8 bytes each)
    .equ    F_CORNERS,  80      ! the quad's corners with their held flags, off the frame
    .equ    F_BYTES,    112

! v within the coordinates a corner's projection can trust, else to C; r0
    .macro  TRUSTED v
    mov.w   @\v, r0
    add     r6, r0
    cmp/hi  r8, r0
    bt      .LnearToC
    mov.w   @(2, \v), r0
    add     r6, r0
    cmp/hi  r8, r0
    bt      .LnearToC
    .endm

! v's place held near the frame, its last word kept, to the stack at 'at'; r0, r6
    .macro  HOLD v, at
    mov.w   @\v, r0
    mov     #-128, r6
    cmp/gt  r0, r6
    bf      1f
    mov     r6, r0
1:  mov.w   .LnearHoldX, r6
    cmp/gt  r6, r0
    bf      2f
    mov     r6, r0
2:  mov.w   r0, @(\at, r15)
    mov.w   @(2, \v), r0
    mov     #-128, r6
    cmp/gt  r0, r6
    bf      3f
    mov     r6, r0
3:  mov.w   .LnearHoldY, r6
    cmp/gt  r6, r0
    bf      4f
    mov     r6, r0
4:  mov.w   r0, @(\at + 2, r15)
    mov.l   @(4, \v), r0
    mov.l   r0, @(\at + 4, r15)
    .endm

    .align 2
    ! whole unless a corner lies past the coordinates VDP1 can be trusted
    ! with: then C splits it
.LnearUnsplit:
    mov     r13, r0
    tst     #CLIP_FRAME, r0
    bt      .LnearWhole
    mov.w   .LnearTrust, r6
    mov     r6, r8
    add     r8, r8
    TRUSTED r1
    TRUSTED r2
    TRUSTED r3
    TRUSTED r7
    bra     .LnearWhole
    nop

.LnearWhole:
    lds.l   @r15+, pr
    rts
    mov     #1, r0
.LnearToC:
    lds.l   @r15+, pr
    rts
    mov     #2, r0

    ! the face at r4, corners r1 r2 r3 r7, clip flags ored r13, split if it is
    ! one to split: r0 0 drawn, -1 and the list is full, 1 not one to split, 2
    ! one for C; r1 r2 r3 r7 r4 r5 kept but when drawn
    .global _facesRoomNear
_facesRoomNear:
    sts.l   pr, @-r15
    mov.l   @(RUN_SPLIT_FAR, gbr), r0
    tst     r0, r0
    bt/s    .LnearHold          ! 0: only where whole would break (faces.cpp facesSplitFar)
    mov     r0, r6              ! the distance every corner must be within
    mov.w   @(4, r1), r0
    cmp/gt  r6, r0
    bt      .LnearWhole
    mov.w   @(4, r2), r0
    cmp/gt  r6, r0
    bt      .LnearWhole
    mov.w   @(4, r3), r0
    cmp/gt  r6, r0
    bt      .LnearWhole
    mov.w   @(4, r7), r0
    cmp/gt  r6, r0
    bt      .LnearWhole
.LnearHold:
    mov     #NEAR_PLANE, r6
    mov.w   @(4, r1), r0
    cmp/gt  r0, r6
    bt      .LnearToC
    mov.w   @(4, r2), r0
    cmp/gt  r0, r6
    bt      .LnearToC
    mov.w   @(4, r3), r0
    cmp/gt  r0, r6
    bt      .LnearToC
    mov.w   @(4, r7), r0
    cmp/gt  r0, r6
    bt      .LnearToC
    mov.l   @(RUN_SPLIT_FAR, gbr), r0
    tst     r0, r0
    bt      .LnearUnsplit       ! only where whole would break
    MATERIAL
    mov.w   @(8, r14), r0
    cmp/pz  r0
    bf      .LnearWhole         ! a texture the cache can't take
    mov.b   @(13, r14), r0
    tst     #2, r0
    bf      .LnearWhole         ! sparse: not worth pieces
    mov     r13, r0
    and     #CLIP_FRAME | CLIP_EDGES, r0
    mov     r0, r13             ! not 0: pieces may lie off the frame
    tst     #CLIP_FRAME, r0
    bt      1f
    mov.w   .LnearTrust, r6
    mov     r6, r8
    add     r8, r8
    TRUSTED r1
    TRUSTED r2
    TRUSTED r3
    TRUSTED r7

    ! its quadrants' word 2 from the cache as the frame's faces before found
    ! it, else looked up
1:  mov.l   @(32, r14), r8
    mov.l   @(RUN_FRAME, gbr), r0
    cmp/eq  r0, r8
    bf      .LnearFresh
    mov.l   @(60, r14), r8
    tst     r8, r8
    bf      .LnearGo
.LnearLookup:
    mov.l   r13, @-r15
    mov.w   .LnearQuads, r13
    stc     gbr, r0
    add     r0, r13             ! the cache's variant
    mov.l   @r13, r6
    mov.w   @(8, r14), r0
    mov.b   @(r0, r6), r0
    extu.b  r0, r0
    tst     r0, r0
    bt      .LnearMiss
.LnearSlot:                     ! r0 the slot + 1
    add     #-1, r0
    mov     r0, r6
    shll2   r0
    shll    r0
    mov.l   @(4, r13), r8
    add     r0, r8
    mov.l   @(RUN_FRAME, gbr), r0
    mov.l   r0, @r8             ! drawn this frame
    mov.w   @(10, r13), r0
    mulu.w  r0, r6
    mov.w   @(8, r13), r0
    extu.w  r0, r8
    sts     macl, r0
    add     r0, r8
    shll16  r8
    mov.w   @(22, r14), r0
    extu.w  r0, r0              ! a quadrant's size: the texture's half size
    or      r0, r8
    mov.l   r8, @(60, r14)
    mov.l   @r15+, r13
.LnearGo:                       ! r8 word 2, r13 off the frame
    mov.l   @r14, r0
    mov.l   r0, @(RUN_PIECE_W0, gbr)
    mov.l   @(RUN_PIECE_MASK, gbr), r0
    mov     r0, r6
    mov.l   @(4, r14), r0
    and     r6, r0              ! Fast's bit as the run has it (faces.cpp facesPiecesFast)
    mov.l   r0, @(RUN_PIECE_W1, gbr)
    mov     r14, r0
    mov.l   r0, @(RUN_PIECE_MATERIAL, gbr)
    mov.w   @(30, r14), r0
    extu.w  r0, r6
    shll16  r6                  ! a quadrant's characters / 8, << 16
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    bsr     .LnearSplit
    mov     #0, r5
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    lds.l   @r15+, pr
    bf/s    2f
    mov     #0, r0
    mov     #-1, r0
2:  rts
    nop

.LnearFresh:                    ! r0 the frame
    mov.l   r0, @(32, r14)
    mov     #0, r0
    mov.l   r0, @(36, r14)
    mov.l   r0, @(40, r14)
    mov.l   r0, @(44, r14)
    mov.l   r0, @(48, r14)
    mov.l   r0, @(52, r14)
    mov.l   r0, @(56, r14)
    bra     .LnearLookup
    mov.l   r0, @(60, r14)

.LnearMiss:
    mov.l   .LnearMissAt, r0
    jsr     @r0
    nop
    tst     r0, r0
    bf      .LnearSlot
    mov.l   @r15+, r13
    bra     .LnearWhole
    nop

    .align 2
.LnearMissAt:   .long   .Lmiss
.LnearQuads:    .short  RUN_QUADS
.LnearTrust:    .short  1000

    ! a level: corners r1 r2 r3 r7, the first piece's word 2 r8, the step r6,
    ! off the frame r13, level r5: its pieces, T set when the list is full
.LnearSplit:
    sts.l   pr, @-r15
    mov.l   r4, @-r15
    add     #-F_BYTES, r15
    mov     r15, r4
    mov.l   r8, @(F_WORD, r4)
    mov.l   r6, @(F_STEP, r4)
    mov.l   r1, @(F_A, r4)
    mov.l   r2, @(F_B, r4)
    mov.l   r3, @(F_C, r4)
    mov.l   r7, @(F_D, r4)
    mov.l   r13, @(F_CLIP, r4)
    mov.l   r5, @(F_LEVEL, r4)
    mov     #0, r0
    mov.l   r0, @(F_TILES, r4)
    mov.l   r0, @(F_QUARTER, r4)
    mov     r4, r13
    add     #F_POINTS, r13
    mov     r1, r6
    bsr     .LnearMid
    mov     r2, r8
    mov     r2, r6
    bsr     .LnearMid
    mov     r3, r8
    mov     r3, r6
    bsr     .LnearMid
    mov     r7, r8
    mov     r7, r6
    bsr     .LnearMid
    mov     r1, r8
    mov     r4, r6
    add     #F_POINTS, r6
    mov     r6, r8
    bsr     .LnearMid
    add     #16, r8

    mov.l   @(F_CLIP, r4), r0
    tst     r0, r0
    bt      .LnearWib
    mov     r4, r13
    add     #F_POINTS, r13
    bsr     .LnearClip
    nop
    bsr     .LnearClip
    nop
    bsr     .LnearClip
    nop
    bsr     .LnearClip
    nop
    bsr     .LnearClip
    nop
    mov.l   @(F_LEVEL, r4), r0
    tst     r0, r0
    bf      .LnearWib

    ! the quad's own corners, with what's held flagged
    mov     r4, r13
    mov     #F_CORNERS, r0
    add     r0, r13
    mov.l   @(F_A, r4), r6
    bsr     .LnearCorner
    mov.l   r13, @(F_A, r4)
    mov.l   @(F_B, r4), r6
    bsr     .LnearCorner
    mov.l   r13, @(F_B, r4)
    mov.l   @(F_C, r4), r6
    bsr     .LnearCorner
    mov.l   r13, @(F_C, r4)
    mov.l   @(F_D, r4), r6
    bsr     .LnearCorner
    mov.l   r13, @(F_D, r4)

.LnearWib:
    mov.l   @(RUN_WIBBLE, gbr), r0
    tst     r0, r0
    bt      .LnearPieces
    mov     r4, r13
    add     #F_POINTS, r13
    bsr     .LnearWibble
    nop
    bsr     .LnearWibble
    nop
    bsr     .LnearWibble
    nop
    bsr     .LnearWibble
    nop
    bsr     .LnearWibble
    nop

.LnearPieces:                   ! A AB centre DA, AB B BC centre, centre BC C CD, DA centre CD D
    mov.l   @(F_A, r4), r1
    mov     r4, r2
    add     #F_POINTS, r2
    mov     r2, r3
    add     #32, r3
    mov     r2, r7
    bsr     .LnearPiece
    add     #24, r7
    bt      .LnearOut
    mov     r4, r1
    add     #F_POINTS, r1
    mov.l   @(F_B, r4), r2
    mov     r1, r3
    add     #8, r3
    mov     r1, r7
    bsr     .LnearPiece
    add     #32, r7
    bt      .LnearOut
    mov     r4, r1
    add     #F_POINTS + 32, r1
    mov     r4, r2
    add     #F_POINTS + 8, r2
    mov.l   @(F_C, r4), r3
    mov     r2, r7
    bsr     .LnearPiece
    add     #8, r7
    bt      .LnearOut
    mov     r4, r1
    add     #F_POINTS + 24, r1
    mov     r1, r2
    add     #8, r2
    mov     r1, r3
    add     #-8, r3
    bsr     .LnearPiece
    mov.l   @(F_D, r4), r7
.LnearOut:
    add     #F_BYTES, r15
    mov.l   @r15+, r4
    lds.l   @r15+, pr
    rts
    nop

    ! the view's midpoint of r6 and r8 to the point at r13, its clip flags
    ! none, r13 past it; r0, r5, r6, r8, r14
.LnearMid:
    cmp/hs  r6, r8
    bt      1f
    mov     r6, r0
    mov     r8, r6
    mov     r0, r8
1:  mov.w   @(4, r6), r0
    mov     r0, r14
    mov.w   @(4, r8), r0
    add     r0, r14             ! za + zb
    mov     #-1, r5
    shll8   r5                  ! the divider at 0xFFFFFF00
    mov.l   r14, @r5            ! DVSR
    shlr    r14
    shll16  r0
    shlr    r0                  ! zb << 15
    mov.l   r0, @(4, r5)        ! DVDNT: divides as what's next goes on
    mov     r14, r0
    mov.w   r0, @(4, r13)       ! z
    mov.b   @(6, r6), r0
    extu.b  r0, r14
    mov.b   @(6, r8), r0
    extu.b  r0, r0
    add     r14, r0
    shlr    r0
    mov.b   r0, @(6, r13)       ! its shade
    mov     #0, r0
    mov.b   r0, @(7, r13)
    mov.l   @(4, r5), r14       ! zb << 15 / (za + zb)
    mov.w   @r6, r5
    mov.w   @r8, r0
    sub     r5, r0
    muls.w  r0, r14
    sts     macl, r0
    shll2   r0
    swap.w  r0, r0
    exts.w  r0, r0
    add     #1, r0
    shar    r0                  ! (b - a) u >> 15, rounded
    add     r5, r0
    mov.w   r0, @r13
    mov.w   @(2, r6), r0
    mov     r0, r5
    mov.w   @(2, r8), r0
    sub     r5, r0
    muls.w  r0, r14
    sts     macl, r0
    shll2   r0
    swap.w  r0, r0
    exts.w  r0, r0
    add     #1, r0
    shar    r0
    add     r5, r0
    mov.w   r0, @(2, r13)
    rts
    add     #8, r13

    ! corner r6 copied to the point at r13 and flagged, r13 past it
.LnearCorner:
    mov.l   @r6, r0
    mov.l   r0, @r13
    mov.l   @(4, r6), r0
    bra     .LnearClip
    mov.l   r0, @(4, r13)

    ! the point at r13's clip flags (faces.cpp nearHold, the place itself left
    ! as it is), r13 past it; r0, r5, r6, r8, r14
.LnearClip:
    mov.w   @r13, r6            ! x
    mov.w   @(2, r13), r0
    mov     r0, r8              ! y
    mov     #0, r14
    mov     #88, r0
    shll2   r0                  ! FRAME_WIDTH
    cmp/hi  r0, r6
    bt      1f
    mov     #56, r0
    shll2   r0                  ! FRAME_HEIGHT
    cmp/hi  r0, r8
    bf      2f
1:  mov     #CLIP_FRAME, r14
    mov     #-128, r0
    cmp/gt  r6, r0
    bt      3f
    cmp/gt  r8, r0
    bt      3f
    mov.w   .LnearMaxX, r0
    cmp/gt  r0, r6
    bt      3f
    mov.w   .LnearMaxY, r0
    cmp/gt  r0, r8
    bf      2f
3:  mov     #CLIP_FRAME, r14
    mov     #-128, r5
    extu.b  r5, r5
    or      r5, r14             ! CLIP_HELD
2:  mov.l   @(RUN_WINDOW, gbr), r0
    cmp/gt  r6, r0
    bf      4f
    add     #CLIP_LEFT, r14
4:  mov.l   @(RUN_WINDOW + 8, gbr), r0
    cmp/gt  r0, r6
    bf      5f
    add     #CLIP_RIGHT, r14
5:  mov.l   @(RUN_WINDOW + 4, gbr), r0
    cmp/gt  r8, r0
    bf      6f
    add     #CLIP_TOP, r14
6:  mov.l   @(RUN_WINDOW + 12, gbr), r0
    cmp/gt  r0, r8
    bf      7f
    add     #CLIP_BOTTOM, r14
7:  mov     r14, r0
    mov.b   r0, @(7, r13)
    rts
    add     #8, r13

.LnearMaxX:     .short  480
.LnearMaxY:     .short  352

    ! the point at r13 wibbled, if on the frame, r13 past it; r0, r5, r6, r8
.LnearWibble:
    mov.b   @(7, r13), r0
    tst     #CLIP_FRAME, r0
    bf      1f
    mov.l   @(RUN_WIBBLE, gbr), r0
    mov     r0, r5
    mov.l   @(RUN_PHASE, gbr), r0
    mov     r0, r8
    mov.w   @r13, r6
    add     r6, r0
    and     #31, r0
    mov.b   @(r0, r5), r0
    add     r6, r0
    mov.w   r0, @r13
    mov.w   @(2, r13), r0
    mov     r0, r6
    add     r8, r0
    and     #31, r0
    mov.b   @(r0, r5), r0
    add     r6, r0
    mov.w   r0, @(2, r13)
1:  rts
    add     #8, r13

    ! a piece r1 r2 r3 r7 of the level at r4: off the frame left out, held
    ! split again or drawn held; T set when the list is full
.LnearPiece:
    mov.l   @(F_WORD, r4), r8
    mov.l   @(F_STEP, r4), r0
    add     r8, r0
    mov.l   r0, @(F_WORD, r4)
    mov.l   @(F_CLIP, r4), r0
    tst     r0, r0
    bf      1f
    bra     .LnearDraw
    mov     #0, r13
1:  mov.l   @(F_QUARTER, r4), r0
    add     #1, r0
    mov.l   r0, @(F_QUARTER, r4)
    mov.l   @(4, r1), r6
    mov.l   @(4, r2), r0
    mov     r6, r13
    and     r0, r6
    or      r0, r13
    mov.l   @(4, r3), r0
    and     r0, r6
    or      r0, r13
    mov.l   @(4, r7), r0
    and     r0, r6
    or      r0, r13
    mov     r6, r0
    tst     #CLIP_EDGES, r0
    bf      .LnearLeft
    mov     r13, r0
    tst     #CLIP_HELD, r0
    bf      2f
    bra     .LnearDraw
    nop
2:  mov.l   @(F_LEVEL, r4), r0
    tst     r0, r0
    bf      .LnearHeld

    ! in tiles, if its texture can have them
    mov.l   @(RUN_PIECE_MATERIAL, gbr), r0
    mov     r0, r14
    mov.w   @(22, r14), r0
    extu.w  r0, r6              ! a quadrant's size
    tst     #1, r0
    bf      .LnearHeld
    mov     r6, r0
    shlr8   r0
    tst     #1, r0
    bf      .LnearHeld          ! VDP1 takes widths by 8; a tile's texels must start on a byte
    mov.l   @(F_TILES, r4), r0
    tst     r0, r0
    bf      3f
    sts.l   pr, @-r15           ! the tiles' character: RoomRun.tiles(run, key, size)
    mov.l   r1, @-r15
    mov.l   r2, @-r15
    mov.l   r3, @-r15
    mov.l   r7, @-r15
    mov.l   r8, @-r15
    mov.l   r13, @-r15
    mov.l   r4, @-r15
    mov.w   @(8, r14), r0
    extu.w  r0, r5
    stc     gbr, r4
    mov.l   @(RUN_TILES, gbr), r0
    jsr     @r0
    nop
    mov.l   @r15+, r4
    mov.l   @r15+, r13
    mov.l   @r15+, r8
    mov.l   @r15+, r7
    mov.l   @r15+, r3
    mov.l   @r15+, r2
    mov.l   @r15+, r1
    lds.l   @r15+, pr
    tst     r0, r0
    bf      4f
    mov     #-1, r0
4:  mov.l   r0, @(F_TILES, r4)
3:  cmp/pl  r0
    bf      .LnearHeld          ! no room for them
    mov     r0, r8
    shll16  r8                  ! their character << 16
    mov.l   @(RUN_PIECE_MATERIAL, gbr), r0
    mov     r0, r14
    mov.w   @(22, r14), r0
    extu.w  r0, r6
    shlr8   r0
    shlr    r0
    shll8   r0                  ! a tile's width
    mov     r0, r5
    mov     r6, r0
    extu.b  r0, r0
    shlr    r0                  ! ... and height
    or      r5, r0
    or      r0, r8              ! the first tile of all's word 2
    mov.l   @(F_STEP, r4), r6
    shlr2   r6                  ! a tile's characters / 8, << 16
    mov.l   @(F_QUARTER, r4), r0
    add     #-1, r0
    shll2   r0                  ! the tiles before this quadrant's
    mul.l   r0, r6
    sts     macl, r0
    add     r0, r8
    mov     #CLIP_HELD, r13
    bra     .LnearSplit
    mov     #1, r5

.LnearLeft:
    rts
    clrt

    ! drawn with its corners held, a hole left clean if that turns it over
.LnearHeld:
    sts.l   pr, @-r15
    mov.l   r8, @-r15
    add     #-32, r15
    HOLD    r1, 0
    HOLD    r2, 8
    HOLD    r3, 16
    HOLD    r7, 24
    mov     r15, r1
    mov     r15, r2
    add     #8, r2
    mov     r15, r3
    add     #16, r3
    mov     r15, r7
    add     #24, r7
    BACKFACE 8f
    mov.l   @(32, r15), r8
    bsr     .LnearDraw
    mov     #CLIP_FRAME, r13
9:  add     #36, r15
    lds.l   @r15+, pr
    rts
    nop
8:  mov.l   .LnearVoid, r0
    jsr     @r0
    nop
    bra     9b
    nop

    .align 2
.LnearVoid:     .long   .Lvoid
.LnearHoldX:    .short  480
.LnearHoldY:    .short  352
    .align 2

    ! a room face, corners r1, r2, r3, r7, corner A at the shade r8 from which its
    ! texture can't be seen: with every corner there, a plain black polygon in its place
    ! among the faces, which hides what's behind it for less than its texture costs either
    ! CPU. Never under water, whose colours are worked another way. r0 1 made, -1 and the
    ! list is full; else 0, r14 kept. r0, r6, r8, r13, r14
.Ldark:
    mov.b   @(6, r2), r0
    extu.b  r0, r0
    cmp/hs  r8, r0
    bf      8f
    mov.b   @(6, r3), r0
    extu.b  r0, r0
    cmp/hs  r8, r0
    bf      8f
    mov.b   @(6, r7), r0
    extu.b  r0, r0
    cmp/hs  r8, r0
    bf      8f
    mov.l   @(RUN_PLAIN, gbr), r0
    cmp/pz  r0
    bf      8f
    mov.l   @r15, r0            ! the clip flags ored
    sts.l   pr, @-r15
    mov     #0x10, r8
    shll8   r8
    add     #4, r8
    shll16  r8                  ! w0: (CMD_POLYGON | CMD_JUMP) << 16
    tst     #CLIP_FRAME, r0
    bt      5f
    mov.l   .LnearMirror, r0
    jsr     @r0
    nop
5:  BUCKET
    LINK
    QUAD_CORNERS
    mov     #-64, r0
    extu.b  r0, r0
    shll16  r0
    mov     #-128, r6
    extu.b  r6, r6
    shll8   r6
    or      r6, r0
    mov.l   r0, @(4, r9)        ! w1: PMOD_NO_ECD_SPD << 16 | black
    NEXT
    lds.l   @r15+, pr
    bt      9f
    rts
    mov     #1, r0
8:  rts
    mov     #0, r0
9:  rts
    mov     #-1, r0

    ! the face with corners r1 r2 r3 r7 wholly behind a solid box (faces.h RoomBox): every
    ! corner in the box's rectangle and past its sides that face the camera: T set.
    ! r0, r6, r8, r13, r14
    .macro  BOX_INSIDE v, miss
    mov.w   @\v, r6
    mov.w   @r13, r0
    cmp/ge  r0, r6
    bf      \miss
    mov.w   @(4, r13), r0
    cmp/ge  r6, r0
    bf      \miss
    mov.w   @(2, \v), r0
    mov     r0, r6
    mov.w   @(2, r13), r0
    cmp/ge  r0, r6
    bf      \miss
    mov.w   @(6, r13), r0
    cmp/ge  r6, r0
    bf      \miss
    .endm

    ! a vertex's byte offset by half is its point's
    .macro  BOX_PAST v, miss
    mov.l   @(RUN_VERTICES, gbr), r0
    mov     \v, r6
    sub     r0, r6
    shlr    r6
    mov.l   @(RUN_POINTS, gbr), r0
    add     r0, r6
    .irp    k, 0, 1, 2
    mov.b   @(\k, r6), r0
    extu.b  r0, r8
    mov.w   @(8 + 2 * \k, r13), r0
    cmp/gt  r0, r8
    bf      \miss
    mov.w   @(14 + 2 * \k, r13), r0
    cmp/gt  r8, r0
    bf      \miss
    .endr
    .endm

.Lbehind:
    mov.l   @(RUN_BOX_COUNT, gbr), r0
    mov     r0, r14
    mov.l   @(RUN_BOXES, gbr), r0
    mov     r0, r13
1:  BOX_INSIDE r1, 5f
    BOX_INSIDE r2, 5f
    BOX_INSIDE r3, 5f
    BOX_INSIDE r7, 5f
    bra     6f
    nop
5:  dt      r14
    bf/s    1b
    add     #BOX_BYTES, r13
    rts
    clrt
6:  BOX_PAST r1, 7f
    BOX_PAST r2, 7f
    BOX_PAST r3, 7f
    BOX_PAST r7, 7f
    rts
    sett
7:  bra     5b
    nop

    ! a piece's command: corners r1 r2 r3 r7, word 2 r8, clip flags ored r13;
    ! T set when the list is full
.LnearDraw:
    sts.l   pr, @-r15
    mov.l   r8, @(8, r9)
    mov.l   @(RUN_PIECE_W1, gbr), r0
    mov.l   r0, @(4, r9)
    mov.l   @(RUN_PIECE_W0, gbr), r0
    mov     r0, r8
    mov     r13, r0
    tst     #CLIP_FRAME, r0
    bt      5f
    mov.l   .LnearMirror, r0
    jsr     @r0
    nop
5:  BUCKET
    LINK
    QUAD_CORNERS
    mov.l   @(RUN_PLAIN, gbr), r0
    cmp/pz  r0
    bf      8f
    mov.l   @(RUN_SHADE_GOURAUD, gbr), r0
    mov     r0, r6
    SHADE   r1
    SHADE   r2
    SHADE   r3
    SHADE   r7
    cmp/eq  r1, r2
    bf      6f
    cmp/eq  r1, r3
    bf      6f
    cmp/eq  r1, r7
    bf      6f
    mov.l   .LnearShaded, r0
    jsr     @r0
    nop
    tst     r0, r0
    bf      7f
    mov     r1, r2
    mov     r1, r3
    mov     r1, r7
6:  TABLE
7:  NEXT
    lds.l   @r15+, pr
    rts
    nop
8:  mov.l   .LnearWater, r0
    jsr     @r0
    nop
    bra     7b
    nop

    .align 2
.LnearMirror:   .long   .Lmirror
.LnearShaded:   .long   .LshadedCopy
.LnearWater:    .long   .LwaterShade


! Stage 3 the new way (faces.h): a room's faces straight to VDP1 commands.
!
! void facesRoomQuadsAsm(RoomQuad* quads, int32 count, RoomRun* run)
! void facesRoomTrianglesAsm(const RoomQuad* triangles, int32 count, RoomRun* run)
!
! A run of a room's quads, or its triangles, as faces.cpp facesRoomC adds
! them: each face kept has its command and gouraud table written at once,
! chained into its depth bucket. A quad decides its turned lines and mip
! level on frames of its parity and keeps them in its flags' top byte in
! between (faces.s REUSE); triangles decide every frame. A texture the cache
! holds is drawn from it, else in its mean colour. A quad cut by a plane or
! near the camera goes to C (RoomRun.careful), which may split it; a face
! wholly past the far plane is a transparent fill behind everything. The run
! is at GBR as the faces go, the list's cursors in registers.
!
! RoomRun, RoomVariant, RoomMaterial: faces.h.
! RoomQuad: uint32 flags; uint16 indices[4], byte offsets into the room's
! vertices. Vertex: int16 x, y; int16 z; uint8 g, clip.
! CommandList: +0 cmd, +4 end, +8 grd, +12 link, +16 grdLink.
! ShadeCache: +0 frame, +4 validFrom, +8 vram, +12 miss(cache, key, slot),
! +16 keys[256], +1040 frames[256].
!
! Through a face: r1, r2, r3, r7 its corners A B C D; r4 the face; r5 the
! faces left; r9 its command, r10 its gouraud table, r11 its link, r12 its
! last word; r0, r6, r8, r13, r14 as each step says. The stack: +0 the
! corners' clip flags ored.

    .equ    CLIP_FRAME,     0x01
    .equ    CLIP_DISCARD,   0x3E
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
    .equ    RUN_FACING,     56
    .equ    RUN_PLAIN,      60
    .equ    RUN_VARIANTS,   64

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
    sub     r6, r13             ! bx - ax
    mov.w   @r3, r8
    sub     r6, r8              ! cx - ax
    mov.l   @r1, r6
    exts.w  r6, r6              ! ay
    mov.l   @r3, r0
    exts.w  r0, r0
    sub     r6, r0              ! cy - ay
    muls.w  r13, r0
    mov.l   @r2, r13
    exts.w  r13, r13
    sub     r6, r13             ! by - ay
    sts     macl, r6
    muls.w  r8, r13
    sts     macl, r0
    cmp/gt  r0, r6
    bf      \skip
    .endm

! culled wholly beyond a frame edge or with a corner behind the near plane:
! to 'culled', r6 the corners' clip flags anded; else r13 them ored; r0
    .macro  CULL tri, culled
    mov.b   @(7, r1), r0
    mov     r0, r6
    mov     r0, r13
    mov.b   @(7, r2), r0
    and     r0, r6
    or      r0, r13
    mov.b   @(7, r3), r0
    and     r0, r6
    or      r0, r13
    .ifeq \tri
    mov.b   @(7, r7), r0
    and     r0, r6
    or      r0, r13
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

! r13 the variant (mip level * 2 + turned) the face with corners r1, r2, r3,
! r7 and material r14 takes: turned when that makes fewer lines, at the
! first mip level with about a texel a pixel along them (faces.s TURN and
! WHOLE); r0, r6, r8
    .macro  DECIDE
    mov     #0, r13
    SPAN    r1, r7, r13, r8     ! AD
    SPAN    r2, r3, r13, r8     ! BC: the lines' length turned
    mov     #0, r6
    SPAN    r1, r2, r6, r8      ! AB
    SPAN    r7, r3, r6, r8      ! DC: ... as it is
    cmp/gt  r6, r13
    bf/s    1f
    mov     #0, r0
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
    bf      9f                  ! about a texel a pixel at full res
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
    add     #2, r13             ! level 2
9:
    .endm

! r14 the material of the face at r4; r0
    .macro  MATERIAL
    mov.l   @r4, r14
    shll16  r14
    shll2   r14
    shlr8   r14
    shlr2   r14
    shlr2   r14
    shlr    r14                 ! its texture * 32
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
! face test) or the triangles (tri 1), labels ending in sfx.
    .macro  FACES sfx, tri, facing

    ! culled: wholly past the far plane, filled; a quad cut by a plane, to C;
    ! a triangle cut by one, filled (back faces gone before)
.Lculled\sfx:                   ! r6 the corners' clip flags anded
    mov     r6, r0
    tst     #CLIP_DISCARD & ~CLIP_PLANE, r0
    bf      .Lnext\sfx          ! wholly beyond a frame edge
    tst     #CLIP_PLANE, r0
    .if \tri
    bt      1f
    .else
    bt      .Lcareful\sfx
    .endif
    mov.w   @(4, r1), r0
    shlr2   r0
    shlr2   r0
    mov     #5, r6
    shll8   r6                  ! the far plane's bucket
    cmp/eq  r6, r0
    bf      .Lnext\sfx          ! behind the near plane
1:  bsr     .Lvoid
    nop
    bf      .Lnext\sfx
    bra     .Ldone
    nop

    .ifeq \tri
.Lcareful\sfx:
    bsr     .Lcareful
    nop
    bf      .Lnext\sfx
    bra     .Ldone
    nop
    .endif

.LtoPlain\sfx:
    bra     .Lplain\sfx
    nop

.Lnext\sfx:
    dt      r5
    bf/s    .Lface\sfx
    add     #12, r4
    bra     .Ldone
    nop

.Lface\sfx:
    CORNERS \tri
    .ifeq \facing
.LFaceBack\sfx:
    BACKFACE .Lnext\sfx
    .endif
.LFaceCull\sfx:
    CULL    \tri, .Lculled\sfx
.LFaceNear\sfx:
    mov.l   r13, @r15           ! the clip flags ored
    .ifeq \tri
    mov.w   @(4, r1), r0
    shlr8   r0
    tst     #0xF8, r0
    bt      .Lcareful\sfx       ! corner A nearer than 2048: C decides on a split
    .endif
.LFaceMaterial\sfx:
    MATERIAL
    mov.w   @(8, r14), r0
    cmp/pz  r0
    bf      .LtoPlain\sfx      ! a texture the cache can't take

    .if \tri
    DECIDE
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
    DECIDE
    mov     r13, r0
    or      #8, r0
    mov.b   r0, @r4             ! kept in the flags' top byte
    mov     r13, r6
    .endif

    ! the texture from the cache: the variant's slot for it, drawn this
    ! frame; else its loader's
.Lvariant\sfx:                  ! r6 the variant
    stc     gbr, r13
    shll2   r6
    shll2   r6
    add     r6, r13
    add     #RUN_VARIANTS, r13  ! the variant's
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
    or      r0, r8
    mov.l   r8, @(8, r9)        ! w2
    mov.l   @(4, r14), r0
    mov.l   r0, @(4, r9)        ! w1
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

.Lmiss\sfx:
    bsr     .Lmiss
    nop
    tst     r0, r0
    bf      .Lslot\sfx
    bra     .Lplain\sfx
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
    bra     .Ldone
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
    tst     r0, r0
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

! --- the entries ------------------------------------------------------------

    .macro  ENTER
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    stc.l   gbr, @-r15
    add     #-4, r15            ! the clip flags ored
    ldc     r6, gbr
    mov.l   @(RUN_LIST, gbr), r0
    mov.l   @r0, r9
    mov.l   @(8, r0), r10
    mov.l   @(12, r0), r11
    mov.l   @(16, r0), r12
    mov.l   @(RUN_END, gbr), r0
    cmp/eq  r0, r9
    bt      .Ldone              ! no room left
    tst     r5, r5
    bt      .Ldone
    .endm

    .text
    .align 2
    .global _facesRoomQuadsAsm
_facesRoomQuadsAsm:
    ENTER
    mov.l   @(RUN_FACING, gbr), r0
    tst     r0, r0
    bt      1f
    bra     .LfaceFacing
    nop
1:  bra     .LfaceAny
    nop

    .global _facesRoomTrianglesAsm
_facesRoomTrianglesAsm:
    ENTER
    bra     .LfaceTri
    nop

.Ldone:
    mov.l   @(RUN_LIST, gbr), r0
    mov.l   r9, @r0
    mov.l   r10, @(8, r0)
    mov.l   r11, @(12, r0)
    mov.l   r12, @(16, r0)
    add     #4, r15
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

    ! a transparent fill over the box on the frame of corners r1, r2, r3, r7,
    ! behind everything (faces.cpp roomVoid); T set when the list is then
    ! full; r0, r6, r8, r13, r14
.Lvoid:
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
4:  mov     #0x10, r0           ! (CMD_POLYGON | CMD_JUMP) >> 8
    cmp/gt  r8, r6
    bt      5f
    cmp/gt  r14, r13
    bf      6f
5:  mov     #0x50, r0           ! empty: skipped
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

    ! the quad at r4 to C (RoomRun.careful), the cursors through the list;
    ! T set when it is then full; r0 .. r3, r6, r7
.Lcareful:
    sts.l   pr, @-r15
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   @(RUN_LIST, gbr), r0
    mov.l   r9, @r0
    mov.l   r10, @(8, r0)
    mov.l   r11, @(12, r0)
    mov.l   r12, @(16, r0)
    mov     r4, r5
    stc     gbr, r4
    mov.l   @(RUN_CAREFUL, gbr), r0
    jsr     @r0
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
    FACES   Facing, 0, 1
    .align 2
    FACES   Any, 0, 0
    .align 2
    FACES   Tri, 1, 0

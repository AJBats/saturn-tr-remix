! Flush's command building for the master CPU (render.cpp flush_c): walks the
! two ordering tables far to near, emptying them, and builds each face's VDP1
! command and gouraud table as faceCommand does. Faces of the common kinds
! (coloured, mean-coloured and textured, out of water) are built here; the
! rest go to the C function.
!
! void flushAsm(FlushXform* xf)
!
! FlushXform (render.cpp):
!   +0  Face** roomOT           the slave's ordering table
!   +4  Face** meshOT           the master's
!   +8  int32 buckets           OT_SIZE
!   +12 const Vertex* vertices  gVertices
!   +16 uint32* cmd             the next command; returned
!   +20 uint32* cmdEnd          the last command slot, kept for the end
!   +24 uint32* grd             the next gouraud table; returned
!   +28 uint32 grdLink          the next command's last word, its gouraud
!                               table's address / 8 << 16; returned
!   +32 const Texture* textures
!   +36 const uint16* palette
!   +40 const uint16* shadeGouraud
!   +44 const uint8* shadeLevel
!   +48 const uint8* halfLevel
!   +52 int32 dark              gDarkFaces: 0 add, 1 half luminance, 2 contrast
!   +56 uint32 detailVram       gDetailVramStart
!   +60 void (*slow)(const Face* face, uint32* w, uint32* grd, int32 room)
!   +64 uint32 mipVram[2]       the mip levels' pools (gDetailPoolVram[1], [2])
!   +72 ShadeCache* shade       the CPU's shaded colour tables, 0 for none
!   +76 int32 farShade          room faces drawn with a mip level take
!                               one shade, their corners' mean, when the corners
!                               are this many levels apart at most
!
! ShadeCache (render.cpp): +0 frame, +4 validFrom, +8 vram, +12 miss,
! +16 keys[256], +1040 frames[256].
!
! Face: uint32 flags; Face* next; int16 indices[4].
! Vertex: int16 x, y; int16 z; uint8 g, clip.
! Texture: uint16 srca, colr, size, attr, color, reserved, altColor, contrast.
! Room textures have no character (srca 0) but in the cache (FACE_DETAIL).

    .equ    FACE_WATER_BIT,     30
    .equ    CLIP_FRAME,         1

    .text
    .align 2
    .global _flushAsm

! acc (r14) op= how far vertex v lies off the frame (offFrame); r0, r1, r3
    .macro  OFF v, op, inv
    mov.l   @\v, r3
    mov     r3, r1
    shlr16  r1
    exts.w  r1, r1              ! x
    cmp/pz  r1
    bt      1f
    bra     2f
    \inv    r1, r14             ! left of the frame by -x
1:  mov     #88, r0
    shll2   r0                  ! FRAME_WIDTH
    cmp/gt  r0, r1
    bf      2f
    sub     r0, r1
    \op     r1, r14             ! right of it
2:  exts.w  r3, r1              ! y
    cmp/pz  r1
    bt      3f
    bra     4f
    \inv    r1, r14
3:  mov     #60, r0
    shll2   r0                  ! FRAME_HEIGHT
    cmp/gt  r0, r1
    bf      4f
    sub     r0, r1
    \op     r1, r14
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

! vertex index 'disp' of the face (r9) to a vertex pointer in r; r0
    .macro  VERTEX disp, r
    mov.w   @(\disp, r9), r0
    shll2   r0
    shll    r0
    mov     r0, \r
    add     r12, \r
    .endm

! level L (doubled) to the gouraud colour that keeps it at half luminance:
! r1, r2, r14 point into halfLevel by the texture's mean red, green, blue; r0, r3
    .macro  HALF L
    mov     \L, r0
    mov.b   @(r0, r14), r3      ! blue
    mov.b   @(r0, r2), \L       ! green
    shll2   r3
    shll2   r3
    shll    r3
    or      \L, r3
    mov.b   @(r0, r1), \L       ! red
    shll2   r3
    shll2   r3
    shll    r3
    or      r3, \L
    .endm

! the gouraud colour of shade g (r) in place; r1 = shadeGouraud, r0
    .macro  SHADE r
    add     \r, \r
    mov     \r, r0
    mov.w   @(r0, r1), \r
    .endm

_flushAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    add     #-12, r15           ! the bucket's meshes, its offset, room or not

    mov     r4, r8
    mov.l   @(12, r8), r12      ! vertices
    mov.l   @(16, r8), r10      ! cmd
    mov.l   @(24, r8), r11      ! grd
    mov.l   @(28, r8), r13      ! grdLink
    mov.l   @(8, r8), r14
    add     #-1, r14
    shll2   r14                 ! the last bucket's offset
    mov.l   @(0, r8), r4
    mov.l   @(4, r8), r5

    ! far to near, skipping buckets empty in both tables
.Lscan:
    mov     r14, r0
    mov.l   @(r0, r4), r1
    mov.l   @(r0, r5), r2
    mov     r1, r3
    or      r2, r3
    tst     r3, r3
    bf      .Lbucket
.Lnext:
    add     #-4, r14
    cmp/pz  r14
    bt      .Lscan
    bra     .Lend
    nop

.Lbucket:
    mov     #0, r3
    mov.l   r3, @(r0, r4)       ! empty for the next frame
    mov.l   r3, @(r0, r5)
    mov.l   r2, @r15
    mov.l   r14, @(4, r15)
    mov     #1, r0
    mov.l   r0, @(8, r15)       ! rooms first
    mov     r1, r9

.Llist:
    tst     r9, r9
    bt      .LlistDone
.Lface:
    mov.l   @(20, r8), r0
    cmp/eq  r0, r10
    bt      .LlistDone          ! out of commands
    bra     .Lemit
    nop
.Lfaced:
    add     #32, r10
    add     #8, r11
    mov     #1, r0
    shll16  r0
    add     r0, r13
    mov.l   @(4, r9), r9        ! next
    tst     r9, r9
    bf      .Lface
.LlistDone:
    mov.l   @(8, r15), r0
    tst     r0, r0
    bt      .LbucketDone
    mov     #0, r0
    mov.l   r0, @(8, r15)       ! then the meshes
    bra     .Llist
    mov.l   @r15, r9
.LbucketDone:
    mov.l   @(4, r15), r14
    mov.l   @(0, r8), r4
    bra     .Lnext
    mov.l   @(4, r8), r5

.Lend:
    mov.l   r10, @(16, r8)
    mov.l   r11, @(24, r8)
    mov.l   r13, @(28, r8)
    add     #12, r15
    lds.l   @r15+, pr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    ! r0 = w[1] for the face drawn through a copy of its colour table shaded
    ! by gouraud value r4 instead of with gouraud, or 0 (no ShadeCache, not
    ! a colour table with gouraud, or no slot free) with r5..r7 = r4 again;
    ! r0..r3, r5..r7, r14
.LshadeTable:
    mov     #72, r0
    mov.l   @(r0, r8), r3       ! ShadeCache
    tst     r3, r3
    bt/s    .LshadeNone
    mov.l   @(4, r10), r14      ! w[1]
    mov     r14, r0
    shlr16  r0
    and     #0x3F, r0
    cmp/eq  #0x0C, r0           ! PMOD_LUT16 | PMOD_GOURAUD
    bf      .LshadeNone
    extu.w  r14, r5             ! the table / 8
    mov     r5, r6
    shll8   r6
    shll2   r6
    shll2   r6
    shll2   r6
    shll    r6
    or      r4, r6              ! the key: table << 15 | gouraud
    mov     r4, r0
    shll2   r0
    shll    r0
    xor     r5, r0
    extu.b  r0, r7              ! the slot, or the other of its pair
    mov     r7, r2
    shll2   r2
    mov     r2, r0
    add     #16, r0
    mov.l   @(r0, r3), r1       ! keys[slot]
    cmp/eq  r6, r1
    bt      1f
    mov     #1, r0
    xor     r0, r7
    mov     r7, r2
    shll2   r2
    mov     r2, r0
    add     #16, r0
    mov.l   @(r0, r3), r1
    cmp/eq  r6, r1
    bf      .LshadeMiss
1:  mov.w   .LshadeFrames, r0
    add     r2, r0
    mov.l   @(r0, r3), r1       ! frames[slot]
    mov.l   @(4, r3), r5        ! validFrom
    cmp/hs  r5, r1
    bf      .LshadeMiss
    mov.l   @r3, r1
    mov.l   r1, @(r0, r3)       ! drawn this frame
    mov.l   @(8, r3), r0        ! vram
    shll2   r7
    shll2   r7
    shll    r7                  ! the slot's copy
    add     r7, r0
    shlr2   r0
    bra     .LshadeW1
    shlr    r0
.LshadeMiss:
    sts.l   pr, @-r15
    mov.l   r4, @-r15
    mov.l   @(12, r3), r0
    mov     r3, r4
    mov     r6, r5
    jsr     @r0                 ! shadeTableMiss(cache, key, slot)
    mov     r7, r6
    mov.l   @r15+, r4
    lds.l   @r15+, pr
    tst     r0, r0
    bt      .LshadeNone
.LshadeW1:
    mov.l   .LnoGouraud, r1
    and     r1, r14
    rts
    or      r14, r0             ! w[1]: the copy, no gouraud
.LshadeNone:
    mov     r4, r5              ! the corners' gouraud values again
    mov     r4, r6
    mov     r4, r7
    rts
    mov     #0, r0

    .align 2
.LnoGouraud:    .long   0xFFF80000  ! w[1] without the colour calculation and table
.LshadeFrames:  .short  1040        ! ShadeCache.frames
    .align 2

    ! anything else: faceCommand
.Lslow:
    mov     r9, r4
    mov     r10, r5
    mov     r11, r6
    mov.l   @(8, r15), r7
    mov.l   @(60, r8), r0
    jsr     @r0
    nop
    bra     .Lfaced
    nop

    ! a room face drawn with a mip level: its slot in its pool
.Lmip:
    mov     r1, r0
    shlr16  r0
    shlr2   r0
    shlr2   r0
    tst     #0x40, r0           ! FACE_DETAIL_MIP2
    bf/s    1f
    and     #0x3F, r0
    shll8   r0
    shll    r0                  ! * 512
    mov     r0, r4
    mov     #64, r0
    mov.l   @(r0, r8), r0       ! mipVram[0]
    bra     .Lturned
    add     r0, r4              ! half size: the quadrant's
1:  shll2   r0
    shll2   r0
    shll2   r0
    shll    r0                  ! * 128
    mov     r0, r4
    mov     #68, r0
    mov.l   @(r0, r8), r0       ! mipVram[1]
    add     r0, r4
    bra     .Lturned
    shlr    r5                  ! a quarter: half the quadrant's

    ! one face (r9) to the command at r10 and the gouraud table at r11
    .align 2
.Lemit:
    mov.l   r13, @(28, r10)     ! w[7]
    mov.l   @r9, r1             ! flags
    mov     r1, r0
    shll    r0
    cmp/pz  r0
    bf      .Lslow              ! underwater
    mov     r1, r0
    shlr8   r0
    shlr2   r0
    shlr2   r0
    shlr2   r0
    and     #15, r0             ! type
    mov     r0, r7
    mov.w   .LtexMask, r0
    and     r1, r0
    mov     r0, r2              ! its texture, or colour for FACE_TYPE_F
    mov     r7, r0
    cmp/eq  #1, r0
    bt      .Lpalette

    mov     r2, r0
    shll2   r0
    shll2   r0
    mov.l   @(32, r8), r2
    add     r0, r2              ! tex
    mov     r7, r0
    cmp/eq  #10, r0
    bt      .LtexColour         ! FACE_TYPE_AVG
    add     #-2, r0
    mov     #3, r7
    cmp/hi  r7, r0
    bt      .Lslow              ! not FT, FTA, GT or GTA

    mov.l   @r2, r3             ! srca << 16 | colr
    mov.l   .LfaceDetail, r5
    tst     r5, r1
    bf      .Lcharacter         ! a room texture in the cache
    mov     r3, r4
    shlr16  r4                  ! srca
    tst     r4, r4
    bt      .Luncached          ! a room texture the cache missed

.Lcharacter:
    mov.l   @(4, r2), r6        ! size << 16 | attr
    mov     r6, r0
    tst     #1, r0              ! TEX_ATTR_AKILL
    mov.l   .LtexturePmod, r0
    mov.l   @r0, r0             ! PMOD_LUT16 | PMOD_GOURAUD, and high-speed shrink
    bf      1f
    or      #0x40, r0           ! colour 0 is opaque
1:  mov.l   .LfacePara, r7
    tst     r7, r1
    bf      2f
    or      #0x80, r0           ! end codes trim only parallelograms
2:  shll16  r0
    extu.w  r3, r3
    or      r0, r3              ! w[1]
    mov.l   r3, @(4, r10)
    mov.w   .LdirMask, r0
    and     r6, r0
    or      #2, r0              ! CMD_DISTORTED_SPRITE
    shll16  r0
    mov.l   r0, @r10            ! w[0]

    tst     r5, r1              ! FACE_DETAIL
    mov     r6, r5
    bt/s    .Lw2
    shlr16  r5                  ! size

    ! a cached full-res texture: its slot
    mov     r1, r0
    shlr16  r0
    shlr2   r0
    shlr2   r0
    and     #0x7F, r0           ! FACE_DETAIL_SLOT
    shll8   r0
    shll2   r0
    shll    r0                  ! * DETAIL_SLOT_SIZE
    mov.l   @(56, r8), r4
    add     r0, r4
    mov.l   .LfaceBit27, r0
    tst     r0, r1
    bt      .Lquadrant
    mov.l   .LfaceMip, r0
    tst     r0, r1
    bf      .Lmip
    shll    r5                  ! whole: twice the quadrant's width and height
.Lturned:
    mov.l   .LfaceTurned, r0
    tst     r0, r1
    bt      .LdetailSrca
    extu.b  r5, r0              ! turned: transposed
    shlr2   r0
    shlr    r0
    shll8   r0                  ! its height / 8 << 8
    shlr8   r5
    shll2   r5
    shll    r5                  ! its width
    bra     .LdetailSrca
    or      r0, r5
.Lquadrant:
    mov     r1, r0
    shlr16  r0
    shlr2   r0
    and     #3, r0              ! FACE_DETAIL_QUAD
    mov     r5, r7
    shlr8   r7
    extu.b  r5, r6
    mulu.w  r7, r6
    sts     macl, r6
    shll2   r6                  ! a quadrant's bytes
    mulu.w  r0, r6
    sts     macl, r0
    add     r0, r4
.LdetailSrca:
    shlr2   r4
    shlr    r4
.Lw2:
    shll16  r4
    or      r5, r4
    bra     .Lgeometry
    mov.l   r4, @(8, r10)       ! w[2]

    .align 2
.LfaceDetail:   .long   0x20000000  ! FACE_DETAIL
.LtexturePmod:  .long   _gTexturePmod
.LfacePara:     .long   0x10000000  ! FACE_PARALLELOGRAM
.LfaceBit27:    .long   0x08000000  ! FACE_DETAIL_WHOLE
.LfaceTurned:   .long   0x00080000  ! FACE_DETAIL_TURNED
.LfaceMip:      .long   0x00040000  ! FACE_DETAIL_MIP
.LtexMask:      .short  0x3FFF      ! FACE_TEXTURE
.LdirMask:      .short  0x0030      ! TEX_ATTR_DIR

.Lpalette:
    mov     r2, r0
    add     r0, r0
    mov.l   @(36, r8), r3
    mov.w   @(r0, r3), r0
    extu.w  r0, r0
    bra     .Lpolygon
    mov     #0, r2              ! no half luminance

.Luncached:
    mov.w   @(6, r2), r0
    tst     #1, r0              ! TEX_ATTR_AKILL
    bt      .LtexColour         ! drawn in its colour
    mov     #0x40, r0
    shll8   r0
    shll16  r0
    bra     .Lfaced
    mov.l   r0, @r10            ! w[0]: CMD_SKIP, see-through texels can't be one colour

.LtexColour:
    mov.w   @(14, r2), r0
    tst     r0, r0
    mov.l   @(8, r2), r0
    bt/s    .Lpolygon
    shlr16  r0                  ! colour
    mov     #0, r2              ! a room texture: its tables of less contrast are for texels
.Lpolygon:
    mov     #4, r3
    shll16  r3
    mov.l   r3, @r10            ! w[0]: CMD_POLYGON
    mov.l   .LpolyW1, r3
    or      r0, r3
    mov.l   r3, @(4, r10)       ! w[1]

    ! r1 flags, r2 the texture for half luminance or 0; the corners to
    ! r4 .. r7 for their shades
.Lgeometry:
    VERTEX  8, r4
    VERTEX  10, r5
    VERTEX  12, r6
    mov.l   .LfacePara2, r0
    tst     r0, r1
    bt      .Lquad
    bra     .Lparallelogram
    nop
.Lquad:
    VERTEX  14, r7
    mov.l   .LturnedFace, r3
    mov     r1, r0
    and     r3, r0
    cmp/eq  r3, r0
    bf      1f
    mov     r5, r0
    mov     r7, r5
    mov     r0, r7              ! turned: A D C B
1:
    mov.b   @(7, r4), r0
    mov     r0, r14
    mov.b   @(7, r5), r0
    or      r0, r14
    mov.b   @(7, r6), r0
    or      r0, r14
    mov.b   @(7, r7), r0
    or      r14, r0
    tst     #CLIP_FRAME, r0
    bt      .LquadCorners

    ! further off the frame on the AD side: B A D C, mirrored
    mov     #0, r14
    OFF     r4, add, sub
    OFF     r7, add, sub
    OFF     r5, sub, add
    OFF     r6, sub, add
    cmp/pl  r14
    bf      .LquadCorners
    mov     r4, r0
    mov     r5, r4
    mov     r0, r5
    mov     r6, r0
    mov     r7, r6
    mov     r0, r7
    mov.l   @r10, r0
    mov.l   .LflipH, r1
    xor     r1, r0
    mov.l   r0, @r10
.LquadCorners:
    mov.l   @r4, r0
    mov.l   r0, @(12, r10)
    mov.l   @r5, r0
    mov.l   r0, @(16, r10)
    mov.l   @r6, r0
    mov.l   r0, @(20, r10)
    mov.l   @r7, r0
    bra     .Lgouraud
    mov.l   r0, @(24, r10)

    ! v0, v1, v2; the fourth corner is v1 + v2 - v0
.Lparallelogram:
    mov.w   @r5, r3
    mov.w   @r6, r0
    add     r0, r3
    mov.w   @r4, r0
    sub     r0, r3
    CLAMP   r3
    mov.l   @r5, r0
    exts.w  r0, r1
    mov.l   @r6, r0
    exts.w  r0, r0
    add     r0, r1
    mov.l   @r4, r0
    exts.w  r0, r0
    sub     r0, r1
    CLAMP   r1
    shll16  r3
    extu.w  r1, r1
    or      r1, r3              ! the fourth corner
    mov.l   @r9, r1
    mov.l   .LfaceV0AtD, r0
    tst     r0, r1
    bt      .LparaA
    ! v0 at D: v1, p, v2, v0
    mov.l   @r5, r0
    mov.l   r0, @(12, r10)
    mov.l   r3, @(16, r10)
    mov.l   @r6, r0
    mov.l   r0, @(20, r10)
    mov.l   @r4, r0
    mov.l   r0, @(24, r10)
    mov     r4, r7
    bra     .Lgouraud
    mov     r5, r4
    ! v0 at A: v0, v1, p, v2
.LparaA:
    mov.l   @r4, r0
    mov.l   r0, @(12, r10)
    mov.l   @r5, r0
    mov.l   r0, @(16, r10)
    mov.l   r3, @(20, r10)
    mov.l   @r6, r0
    mov.l   r0, @(24, r10)
    mov     r6, r7

    ! the corners' shades
.Lgouraud:
    mov.b   @(6, r4), r0
    extu.b  r0, r4
    mov.b   @(6, r5), r0
    extu.b  r0, r5
    mov.b   @(6, r6), r0
    extu.b  r0, r6
    mov.b   @(6, r7), r0
    extu.b  r0, r7
    tst     r2, r2
    bt      .Lplain
    mov.l   @(52, r8), r0
    tst     r0, r0
    bf      .Lhalf

    ! the corners' shades as they are
.Lplain:
    mov.l   @(40, r8), r1       ! shadeGouraud
    SHADE   r4
    SHADE   r5
    SHADE   r6
    SHADE   r7

    ! one shade at every corner, or a far face: perhaps a shaded colour table
    ! instead
    mov.l   @r9, r0
    mov.l   .LfarFace, r3
    and     r3, r0
    cmp/eq  r3, r0
    bt      .LfarShadeNear
    cmp/eq  r4, r5
    bf      .LgouraudTable
    cmp/eq  r4, r6
    bf      .LgouraudTable
    cmp/eq  r4, r7
    bf      .LgouraudTable
.LshadeIt:
    bsr     .LshadeTable
    nop
    tst     r0, r0
    bt      .LgouraudTable
    bra     .Lfaced
    mov.l   r0, @(4, r10)       ! w[1]: the copy, no gouraud

.LgouraudTable:
    shll16  r4
    or      r5, r4
    mov.l   r4, @r11
    shll16  r6
    or      r7, r6
    bra     .Lfaced
    mov.l   r6, @(4, r11)

.LfarShadeNear:                 ! .LfarShade, out of the way
    bra     .LfarShade
    nop



    ! the table of less contrast, by eighths as the darkening (r3) passes
    ! a twelfth of the mean, a sixth, a quarter and a third (shadeDark)
.Lcontrast:
    mov     r0, r14             ! the 7/8 table
    mov.l   @(4, r2), r0
    extu.w  r0, r0
    shlr8   r0
    shll2   r0                  ! mean luma * 4: a third of it, in r3's units
    shll2   r3
    mov     r0, r1
    cmp/ge  r1, r3
    bf      .Lplain
    add     r0, r1
    cmp/ge  r1, r3
    bf      .Lcontrasted
    add     #4, r14
    add     r0, r1
    cmp/ge  r1, r3
    bf      .Lcontrasted
    add     #4, r14
    add     r0, r1
    cmp/ge  r1, r3
    bf      .Lcontrasted
    add     #4, r14
.Lcontrasted:
    mov.l   @(4, r10), r0
    shlr16  r0
    shll16  r0
    or      r14, r0
    bra     .Lplain
    mov.l   r0, @(4, r10)       ! w[1]: the table

.LplainFar:                     ! .Lplain, for the branches below
    bra     .Lplain
    nop

.Lhalf:
    ! half luminance when darkened by a third of the texture's mean luma
    mov.l   @(44, r8), r1       ! shadeLevel
    mov     r4, r0
    mov.b   @(r0, r1), r3
    mov     r3, r14             ! the brightest corner's level
    mov     r5, r0
    mov.b   @(r0, r1), r0
    add     r0, r3
    cmp/gt  r14, r0
    bf      1f
    mov     r0, r14
1:  mov     r6, r0
    mov.b   @(r0, r1), r0
    add     r0, r3
    cmp/gt  r14, r0
    bf      2f
    mov     r0, r14
2:  mov     r7, r0
    mov.b   @(r0, r1), r0
    add     r0, r3
    cmp/gt  r14, r0
    bf      3f
    mov     r0, r14
3:  mov     #64, r0
    sub     r3, r0
    mov     r0, r3
    shll2   r0
    shll    r0
    add     r0, r3              ! (64 - levels) * 9
    mov.l   @(52, r8), r0
    cmp/eq  #2, r0
    bf      .LhalfTest
    mov.w   @(14, r2), r0
    extu.w  r0, r0
    tst     r0, r0
    bf      .Lcontrast          ! a room texture with tables of less contrast
.LhalfTest:
    mov.l   @(4, r2), r0
    extu.w  r0, r0
    shlr8   r0
    shll2   r0                  ! mean luma * 4
    cmp/gt  r3, r0
    bt      .LplainFar

    ! and no channel of the brightest corner past gouraud 31: m + 2L - 16
    add     r14, r14
    mov     #47, r0
    sub     r14, r0
    mov     r0, r14             ! the brightest channel the corners allow
    mov.l   @(8, r2), r3
    shlr16  r3                  ! the texture's mean colour
    mov     r3, r0
    and     #31, r0
    cmp/gt  r14, r0
    bt      .LplainFar
    mov     r3, r0
    shlr2   r0
    shlr2   r0
    shlr    r0
    and     #31, r0
    cmp/gt  r14, r0
    bt      .LplainFar
    mov     r3, r0
    shlr8   r0
    shlr2   r0
    and     #31, r0
    cmp/gt  r14, r0
    bt      .LplainFar

    mov     r4, r0
    mov.b   @(r0, r1), r4
    mov     r5, r0
    mov.b   @(r0, r1), r5
    mov     r6, r0
    mov.b   @(r0, r1), r6
    mov     r7, r0
    mov.b   @(r0, r1), r7
    add     r4, r4
    add     r5, r5
    add     r6, r6
    add     r7, r7
    mov.l   @(8, r2), r3
    shlr16  r3                  ! the texture's mean colour
    mov.l   @(48, r8), r14      ! halfLevel
    mov     r3, r0
    and     #31, r0
    mov     r14, r1
    add     r0, r1
    mov     r3, r0
    shlr2   r0
    shlr2   r0
    shlr    r0
    and     #31, r0
    mov     r14, r2
    add     r0, r2
    mov     r3, r0
    shlr8   r0
    shlr2   r0
    and     #31, r0
    add     r0, r14
    HALF    r4
    HALF    r5
    HALF    r6
    HALF    r7
    shll16  r4
    or      r5, r4
    mov.l   r4, @r11
    shll16  r6
    or      r7, r6
    mov.l   r6, @(4, r11)
    mov.l   @(4, r10), r0
    mov     #2, r3
    shll16  r3
    or      r3, r0              ! PMOD_GOURAUD to PMOD_GOURAUD_HALF
    bra     .Lfaced
    mov.l   r0, @(4, r10)


    .align 2
.LfarFace:      .long   0x28040000  ! FACE_DETAIL | FACE_DETAIL_WHOLE | FACE_DETAIL_MIP
.LfacePara2:    .long   0x10000000  ! FACE_PARALLELOGRAM
.LfaceV0AtD:    .long   0x08000000  ! FACE_V0_AT_D
.LturnedFace:   .long   0x28080000  ! FACE_DETAIL | FACE_DETAIL_WHOLE | FACE_DETAIL_TURNED
.LflipH:        .long   0x00100000  ! CMD_FLIP_H << 16
.LpolyW1:       .long   0x00C40000  ! (PMOD_NO_ECD_SPD | PMOD_GOURAUD) << 16

    ! a far face with farShade: its corners' mean gouraud value at every corner
.LfarShade:
    mov     #72, r0
    mov.l   @(r0, r8), r0
    tst     r0, r0
    bf      8f
    bra     .LgouraudTable      ! no shaded tables
    nop
8:  mov     r4, r0
    and     #31, r0
    mov     r0, r3              ! the levels' sum
    mov     r0, r1              ! the darkest
    mov     r0, r14             ! the brightest
    mov     r5, r0
    and     #31, r0
    add     r0, r3
    cmp/gt  r0, r1
    bf      1f
    mov     r0, r1
1:  cmp/gt  r14, r0
    bf      2f
    mov     r0, r14
2:
    mov     r6, r0
    and     #31, r0
    add     r0, r3
    cmp/gt  r0, r1
    bf      3f
    mov     r0, r1
3:  cmp/gt  r14, r0
    bf      4f
    mov     r0, r14
4:
    mov     r7, r0
    and     #31, r0
    add     r0, r3
    cmp/gt  r0, r1
    bf      5f
    mov     r0, r1
5:  cmp/gt  r14, r0
    bf      6f
    mov     r0, r14
6:
    sub     r1, r14
    mov     #76, r0
    mov.l   @(r0, r8), r0
    cmp/hi  r0, r14
    bf      7f
    bra     .LgouraudTable      ! too far apart
    nop
7:  add     #2, r3
    shlr2   r3                  ! the mean level, the shades being grey
    mov     r3, r4
    shll2   r3
    shll2   r3
    shll    r3
    or      r3, r4              ! level << 5 | level
    shll2   r3
    shll2   r3
    shll    r3
    or      r3, r4              ! a grey of that level
    mov     r4, r5
    mov     r4, r6
    bra     .LshadeIt
    mov     r4, r7

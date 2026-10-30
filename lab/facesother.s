! Stage 3 the new way (faces.h): a face the game makes whole, straight to
! its VDP1 command, as faces.cpp facesOther makes it: a sprite's, a
! rectangle's or line's (renderRect; a fill past the draw distance), a
! void's.
!
! void facesOtherAsm(const Face* face, const Vertex* half, int32 depth, CommandList* list)
!
! Face (game.h): +0 flags (type << 14, a sprite's index in the low 14 bits),
! +8 int16 corners: byte offsets into half.
! Vertex: int16 x, y; int16 z; uint8 g, clip. A rectangle's first corner is
! its place, the vertex after it its size; a line's colour is its z.
! Sprite (game.h): uint16 srca, colr, size, attr; 16 bytes.
! CommandList (faces.h): +0 cmd, +4 end, +8 grd, +12 link, +16 grdLink,
! +20 heads, +24 tails, +32 shadeGouraud, +44 sprites.
!
! r1 the command, r3 corner A, r6 the bucket (doubled from .Llink), r7 the
! list, r8 the first word.

    .equ    FACE_TYPE_SPRITE, 6
    .equ    FACE_TYPE_FILL_S, 7
    .equ    FACE_TYPE_LINE_V, 9
    .equ    W0_SPRITE,      0x1001      ! CMD_SCALED_SPRITE | CMD_JUMP, >> 0
    .equ    PMOD_SPRITE,    0x8C        ! PMOD_ECD_OFF | PMOD_LUT16 | PMOD_GOURAUD
    .equ    PMOD_RECT,      0xC0        ! PMOD_NO_ECD_SPD
    .equ    FRAME_WIDTH,    352
    .equ    FRAME_HEIGHT,   224

! corner k's x into r0, y into r2: then the box (r8 r9 left top, r10 r3
! right bottom) takes it in
    .macro  BOX k
    mov.w   @(8 + 2 * \k, r4), r0
    mov.l   @(r0, r5), r0
    exts.w  r0, r2              ! y
    shlr16  r0
    exts.w  r0, r0              ! x
    cmp/gt  r0, r8
    bf      1f
    mov     r0, r8
1:  cmp/gt  r10, r0
    bf      2f
    mov     r0, r10
2:  cmp/gt  r2, r9
    bf      3f
    mov     r2, r9
3:  cmp/gt  r3, r2
    bf      4f
    mov     r2, r3
4:
    .endm

    .text
    .align 2
    .global _facesOtherAsm
_facesOtherAsm:
    mov.l   @r7, r1
    mov.l   @(4, r7), r0
    cmp/eq  r0, r1
    bf      1f
    rts                         ! no room left
    nop
1:  mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   @r4, r2             ! the flags
    mov.w   @(8, r4), r0
    mov     r5, r3
    add     r0, r3              ! corner A
    mov     r2, r0
    shlr8   r0
    shlr2   r0
    shlr2   r0
    shlr2   r0
    and     #15, r0             ! the type
    cmp/eq  #FACE_TYPE_SPRITE, r0
    bt      .Lsprite
    mov     #FACE_TYPE_FILL_S, r8
    cmp/hs  r8, r0
    bf      .Lvoid
    mov     #FACE_TYPE_LINE_V, r8
    cmp/hi  r8, r0
    bt      .Lvoid

    ! a rectangle: its place, then its size; filled with shadow, or a line
    ! of its colour
    cmp/eq  #FACE_TYPE_FILL_S, r0
    bf/s    1f
    mov.w   @(4, r3), r0        ! a line's colour
    mov.w   .LfillW1, r0        ! RGB(0, 0, 0), with PMOD_SHADOW below
    extu.w  r0, r0
    mov     #PMOD_RECT + 1 - 256, r2
    bra     2f
    extu.b  r2, r2
1:  extu.w  r0, r0
    mov     #PMOD_RECT - 256, r2
    extu.b  r2, r2
2:  shll16  r2
    or      r2, r0
    mov.l   r0, @(4, r1)        ! w1
    mov.l   @r3, r8             ! A: x0 << 16 | y0
    mov.l   @(8, r3), r9        ! the size: w << 16 | h
    mov.l   r8, @(12, r1)       ! w3: x0, y0
    mov     #-1, r0
    shll16  r0
    mov     r8, r2
    and     r0, r2              ! x0 << 16
    and     r9, r0
    add     r2, r0
    mov     #-1, r10
    shll16  r10
    add     r10, r0             ! (x0 + w - 1) << 16 (r10 -1 << 16)
    mov     r8, r10
    add     r9, r10
    add     #-1, r10
    extu.w  r10, r10            ! y0 + h - 1
    extu.w  r8, r9              ! y0
    or      r0, r9
    mov.l   r9, @(16, r1)       ! w4: x1, y0
    or      r10, r0
    mov.l   r0, @(20, r1)       ! w5: x1, y1
    or      r10, r2
    mov.l   r2, @(24, r1)       ! w6: x0, y1
    mov.l   .LpolygonW0, r8
    bra     .Llink
    nop

    ! a scaled sprite between the two corners, one shade
.Lsprite:
    mov     r2, r0
    shll16  r0
    shll2   r0
    shlr16  r0
    shlr2   r0                  ! its index
    shll2   r0
    shll2   r0
    mov.l   @(44, r7), r2
    add     r0, r2              ! the sprite
    mov.w   @(6, r2), r0
    and     #0x30, r0           ! TEX_ATTR_DIR
    mov.w   .LspriteW0, r8
    or      r0, r8
    shll16  r8                  ! w0, unlinked
    mov.w   @(2, r2), r0
    extu.w  r0, r0
    mov     #PMOD_SPRITE - 256, r9
    extu.b  r9, r9
    shll16  r9
    or      r9, r0
    mov.l   r0, @(4, r1)        ! w1: the mode, its colour table
    mov.w   @(4, r2), r0
    extu.w  r0, r9
    mov.w   @r2, r0
    shll16  r0
    or      r9, r0
    mov.l   r0, @(8, r1)        ! w2: its characters, their size
    mov.l   @r3, r0
    mov.l   r0, @(12, r1)       ! w3: corner A
    mov.l   @(8, r3), r0
    mov.l   r0, @(20, r1)       ! w5: corner C
    mov.b   @(6, r3), r0        ! the shade
    extu.b  r0, r0
    add     r0, r0
    mov.l   @(32, r7), r2
    mov.w   @(r0, r2), r0       ! its gouraud colour
    extu.w  r0, r0
    mov     r0, r2
    shll16  r2
    or      r0, r2
    mov.l   @(8, r7), r0
    mov.l   r2, @r0             ! the gouraud table, all four corners
    mov.l   r2, @(4, r0)
    bra     .Llink
    nop

    ! a void: the corners' box on the frame, transparent; skipped when the
    ! box lies off the frame
.Lvoid:
    mov.l   @r3, r0
    exts.w  r0, r9              ! top
    mov     r9, r3              ! bottom
    shlr16  r0
    exts.w  r0, r8              ! left
    mov     r8, r10             ! right
    BOX     1
    BOX     2
    BOX     3
    cmp/pz  r8
    bt      1f
    mov     #0, r8
1:  cmp/pz  r9
    bt      2f
    mov     #0, r9
2:  mov.w   .LlastX, r0
    cmp/gt  r0, r10
    bf      3f
    mov     r0, r10
3:  mov     #FRAME_HEIGHT - 1 - 256, r0
    extu.b  r0, r0
    cmp/gt  r0, r3
    bf      4f
    mov     r0, r3
4:  mov     #PMOD_RECT - 256, r0
    extu.b  r0, r0
    shll16  r0
    mov.l   r0, @(4, r1)        ! w1: no colour
    extu.w  r9, r9
    extu.w  r3, r3
    shll16  r8
    shll16  r10
    mov     r8, r0
    or      r9, r0
    mov.l   r0, @(12, r1)       ! w3: left, top
    mov     r10, r0
    or      r9, r0
    mov.l   r0, @(16, r1)       ! w4: right, top
    mov     r10, r0
    or      r3, r0
    mov.l   r0, @(20, r1)       ! w5: right, bottom
    mov     r8, r0
    or      r3, r0
    mov.l   r0, @(24, r1)       ! w6: left, bottom
    cmp/gt  r10, r8             ! left past right (both << 16, signed)
    bt      5f
    exts.w  r9, r9
    exts.w  r3, r3
    cmp/gt  r3, r9              ! top past bottom
5:  mov.l   .LpolygonW0, r8
    bf      .Llink
    mov.l   .LskipW0, r0
    or      r0, r8

    ! the command into bucket r6: linked to the bucket's last, this the
    ! bucket's last (and its first when it had none); its last word the
    ! gouraud table's place; the list on to the next
.Llink:
    mov.l   @(20, r7), r2
    add     r6, r6
    add     r6, r2              ! the bucket's head
    mov.w   @r2, r0
    extu.w  r0, r0
    or      r0, r8
    mov.l   r8, @r1             ! w0
    mov.l   @(16, r7), r3
    mov.l   r3, @(28, r1)       ! w7: the gouraud table
    mov.l   @(12, r7), r9       ! this command's link
    tst     r0, r0
    bf/s    1f
    mov.w   r9, @r2
    mov.l   @(24, r7), r2
    add     r6, r2
    mov.w   r9, @r2             ! the bucket's first
1:  add     #32, r1
    mov.l   r1, @r7
    mov.l   @(8, r7), r0
    add     #8, r0
    mov.l   r0, @(8, r7)
    add     #4, r9
    mov.l   r9, @(12, r7)
    mov     #1, r0
    shll16  r0
    add     r0, r3
    mov.l   r3, @(16, r7)
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    .align 1
.LfillW1:       .short  0x8000          ! RGB(0, 0, 0)
.LspriteW0:     .short  W0_SPRITE
.LlastX:        .short  FRAME_WIDTH - 1
    .align 2
.LpolygonW0:    .long   0x10040000      ! (CMD_POLYGON | CMD_JUMP) << 16
.LskipW0:       .long   0x40000000      ! CMD_SKIP << 16

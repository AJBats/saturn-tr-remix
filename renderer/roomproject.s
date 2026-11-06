! Stage 2 the new way (vertices.h): a room's vertices projected from their
! rotation, which the DSP makes: saturn/xform.s from its multiplies on, the
! arithmetic unchanged.
!
! void roomProjectAsm(const RoomRotated* src, int32 count, Vertex* dst, RoomProject* rp)
!
! RoomRotated: int32 x, y, z, each the room's row times the vertex (its bytes
! times 4) plus the translation, as xform.s sums them.
! RoomProject (vertices.h), at GBR:
!   +0  const RoomVertex* points    the room's, for their shades (byte 3)
!   +4  int32 x0, y0, x1, y1        viewport (inside the frame)
!   +20 const int16* divScaled      divTable << 4 from Z_FAST on: the loops' d
!   +24 int32 zMin, zMax            near and far planes << 8
!   +32 const int8* wibble          ripple offsets (32), or NULL
!   +36 int32 phase                 ripple phase
!   +40 const int16* caustics       shade offsets (32), by vertex index, or NULL
!   +44 int32 count                 the room's vertex count
!   +48 int32 farScaled             the lab: a vertex past the far plane scaled to it
!                                   exactly (vertices.h verticesFarScaled)
!   +52 int32 zExact                nearer than this << 8, 0x100000 / z instead
!                                   of the table, whose steps are wide up close
!   +56 const uint16* nearTable     0x100000 / z by z, up to zExact >> 8
!   +60 int32 safe                  bit 0: every x and y within the view bound;
!                                   bit 1: every z between zExact (or Z_FAST) and
!                                   zMax as well (vertices.h RoomSafe)
!   +64 int32 left, top, right, bottom   the viewport as this file compares it:
!                                   x0 y0 x1 + 1 y1 + 1, << 16 (vertices.h
!                                   roomProjectReady, with the ripple's base)
!   +80 const uint16* divide        divTable, at .Lodd
!   +88 const int16* farTable       past the far plane 0x1000000 / z by z >> 8;
!                                   none in the lab, which goes by farScaled
!
! Vertex (render.h): int16 x, y; int16 z; uint8 g, clip.
!
! A loop for each kind of room (caustics or not, ripple or not, whether z needs
! its test). A coordinate is (coordinate >> 8) * (d << 4), whose high word is
! the 2^12-scaled projection, plus the frame's near edge in that high word: an
! unsigned compare shows it on the frame, signed compares place it in the
! viewport, and XTRCT packs the two coordinates. The clamp of a product to 2047
! pixels the original makes comes to nothing once that's clamped to what VDP1
! can address, so it's left to .Lodd, where a product can be past 16 bits' worth
! of pixels. Off the frame a side's flags follow from the side (the viewport is
! inside the frame). Everything d << 4 wouldn't hold, or the planes cut, or
! beyond the view bound goes by .Lodd, by call, and comes back with the two
! coordinates as the loop has them.

    .equ    CLIP_FRAME,     1
    .equ    CLIP_LEFT,      2
    .equ    CLIP_RIGHT,     4
    .equ    CLIP_TOP,       8
    .equ    CLIP_BOTTOM,    16
    .equ    R_POINTS,       0
    .equ    R_SCALED,       20
    .equ    R_ZMIN,         24
    .equ    R_ZMAX,         28
    .equ    R_WIBBLE,       32
    .equ    R_PHASE,        36
    .equ    R_CAUSTICS,     40
    .equ    R_COUNT,        44
    .equ    R_FAR_SCALED,   48
    .equ    R_ZEXACT,       52
    .equ    R_NEAR,         56
    .equ    R_SAFE,         60
    .equ    R_LEFT,         64
    .equ    R_TOP,          68
    .equ    R_RIGHT,        72
    .equ    R_BOTTOM,       76
    .equ    R_DIV,          80
    .equ    R_RIPPLE,       84
    .equ    R_FAR_TABLE,    88
    .equ    Z_FAST,         1024 << 8   ! from here d << 4 fits a word (d is at most 1024)
    .equ    FOG_MIN_8,      48          ! FOG_MIN >> 8
    .equ    VERTICES_FAR_LAST, 335      ! vertices.h's

    .text
    .align 2

    ! r2 the frame's x edge; r3 the shade; r4 the rotations; r5 vertices left; r6 the vertices;
    ! r8 the points; r10 the divide table; r11 z; r12 x; r13 y; r14 the clip flags. z in its
    ! test: r7 Z_FAST or so + 1, r9 what's left to the far plane, none if every vertex is
    ! odd. z needing none: r7 and r9 the frame's bounds. The viewport at GBR.
    .macro  ROOMS c, r, z
    .align  2
.Lrooms\c\r\z:
1:  mov.l   @r8+, r3
    mov.l   @r4+, r12
    mov.l   @r4+, r13
    mov.l   @r4+, r11
    extu.b  r3, r3
    .if \c
    ! caustics: shade += caustics[vertex index & 31], within 0..8191
    mov.l   @(R_CAUSTICS, gbr), r0
    mov     r0, r1
    mov.l   @(R_COUNT, gbr), r0
    shll8   r3
    sub     r5, r0
    and     #31, r0
    add     r0, r0
    mov.w   @(r0, r1), r0
    mov.l   .LshadeMax\@, r1
    add     r0, r3
    cmp/hi  r1, r3
    bf      42f
    cmp/pz  r3
    bt/s    42f
    mov     r1, r3
    mov     #0, r3
42:
    .endif
    .if \z == 0
    mov     r11, r0
    sub     r7, r0
    cmp/hs  r9, r0
    bt/s    8f
    mov     #0, r14
    .endif
    shlr8   r11
    shlr8   r12
    mov     r11, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r10), r1
    shlr8   r13
    muls.w  r1, r12
    .if \z == 0
    mov.l   .LfrX\@, r0
    .else
    mov     #0, r14
    .endif
    sts     macl, r12
    muls.w  r1, r13
    add     r2, r12
2:
    .if \z == 0
    cmp/hi  r0, r12
    .else
    cmp/hi  r7, r12
    .endif
    bt      5f
    mov.l   .LedgeY\@, r1
    sts     macl, r13
    .if \z == 0
    mov.l   .LfrY\@, r0
    add     r1, r13
    cmp/hi  r0, r13
    .else
    add     r1, r13
    cmp/hi  r9, r13
    .endif
    bt      6f
    ! on the frame: the window's flags, bottom to left, then 0 for the frame
    mov.l   @(R_BOTTOM, gbr), r0
    cmp/ge  r0, r13
    mov.l   @(R_TOP, gbr), r0
    rotcl   r14
    cmp/gt  r13, r0
    mov.l   @(R_RIGHT, gbr), r0
    rotcl   r14
    cmp/ge  r0, r12
    mov.l   @(R_LEFT, gbr), r0
    rotcl   r14
    cmp/gt  r12, r0
    rotcl   r14
    shll    r14
    .if \r
    ! ripple of what is on the frame: x, y += wibble[(phase + x or y) & 31]; the phase is in
    ! the base, the table being there twice over
    mov.l   @(R_RIPPLE, gbr), r0
    mov     r0, r1
    swap.w  r12, r0
    and     #31, r0
    mov.b   @(r0, r1), r0
    shll16  r0
    add     r0, r12
    swap.w  r13, r0
    and     #31, r0
    mov.b   @(r0, r1), r0
    shll16  r0
    add     r0, r13
    .endif
4:
    .if \c
    ! fog: shade += z - FOG_MIN, at most 8191
    mov     #FOG_MIN_8, r0
    shll8   r0
    cmp/gt  r0, r11
    bf      10f
    mov     r11, r1
    sub     r0, r1
    add     r1, r3
    mov.l   .LshadeMax\@, r1
    cmp/gt  r1, r3
    bf      10f
    mov     r1, r3
10: shlr8   r3
    .else
    mov     #FOG_MIN_8, r0
    shll8   r0
    cmp/gt  r0, r11
    bt      9f
10:
    .endif
    ! store x|y, z|g|clip
    swap.w  r12, r12
    xtrct   r12, r13
    mov.l   r13, @r6
    shll16  r11
    shll8   r3
    or      r3, r11
    or      r14, r11
    mov.l   r11, @(4, r6)
    dt      r5
    bf/s    1b
    add     #8, r6
    bra     .Ldone
    nop

    .if \z == 0
8:  bsr     .Lodd
    nop
    mov.l   .LfrX\@, r0
    bra     2b
    nop
    .endif

    .if \c == 0
    ! fog: shade + (z - FOG_MIN) >> 8, at most 31
9:  mov     r11, r1
    sub     r0, r1
    shlr8   r1
    add     r1, r3
    mov     #31, r1
    cmp/ge  r1, r3
    bf      10b
    bra     10b
    mov     r1, r3
    .endif

    ! x off the frame; y's product in MACL
5:  mov.l   .LedgeY\@, r1
    sts     macl, r13
    add     r1, r13
    shll2   r14                 ! the plane's flag in its place
    shll2   r14
    shll    r14
    cmp/pz  r12
    bt      51f
    add     #CLIP_FRAME + CLIP_LEFT, r14
    mov.l   .Llo\@, r0
    cmp/ge  r0, r12
    bt      52f
    bra     52f
    mov     r0, r12
51: add     #CLIP_FRAME + CLIP_RIGHT, r14
    mov.l   .Lhi\@, r0
    cmp/gt  r0, r12
    bf      52f
    mov     r0, r12
52:
    .if \z == 0
    mov.l   .LfrY\@, r0
    cmp/hi  r0, r13
    .else
    cmp/hi  r9, r13
    .endif
    bt      61f
    mov.l   @(R_BOTTOM, gbr), r0
    cmp/ge  r0, r13
    bf      53f
    add     #CLIP_BOTTOM, r14
53: mov.l   @(R_TOP, gbr), r0
    cmp/gt  r13, r0
    bf      4b
    bra     4b
    add     #CLIP_TOP, r14

    ! y off the frame, x on it
6:  shll2   r14
    shll2   r14
    shll    r14
    add     #CLIP_FRAME, r14
    mov.l   @(R_LEFT, gbr), r0
    cmp/gt  r12, r0
    bf      60f
    add     #CLIP_LEFT, r14
60: mov.l   @(R_RIGHT, gbr), r0
    cmp/ge  r0, r12
    bf      61f
    add     #CLIP_RIGHT, r14
61: cmp/pz  r13
    bt      62f
    add     #CLIP_TOP, r14
    mov.l   .Llo\@, r0
    cmp/ge  r0, r13
    bt      4b
    bra     4b
    mov     r0, r13
62: add     #CLIP_BOTTOM, r14
    mov.l   .Lhi\@, r0
    cmp/gt  r0, r13
    bf      4b
    bra     4b
    mov     r0, r13

    .align 2
    .if \z == 0
.LfrX\@:    .long   (353 << 16) - 1
.LfrY\@:    .long   (225 << 16) - 1
    .endif
.LedgeY\@:  .long   112 << 16
    .if \c
.LshadeMax\@: .long  8191
    .endif
.Llo\@:     .long   -1024 << 16
.Lhi\@:     .long   (1024 << 16) - 1
    .endm

    .global _roomProjectAsm
_roomProjectAsm:
    tst     r5, r5
    bf      1f
    rts
    nop
1:  mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    stc.l   gbr, @-r15
    ldc     r7, gbr
    mov.l   @(R_SCALED, gbr), r0
    mov     r0, r10
    mov.l   @(R_POINTS, gbr), r0
    mov     r0, r8
    mov.l   .LedgeX, r2
    ! the loop: caustics, ripple, z tested or not
    mov     #0, r1
    mov.l   @(R_CAUSTICS, gbr), r0
    tst     r0, r0
    bt      2f
    add     #4, r1
2:  mov.l   @(R_WIBBLE, gbr), r0
    tst     r0, r0
    bt      3f
    bra     4f                  ! with a ripple z is always tested
    add     #8, r1
3:  mov.l   @(R_SAFE, gbr), r0
    cmp/eq  #3, r0
    bf      4f
    add     #16, r1
    mov.l   .LfrXz, r7
    mov.l   .LfrYz, r9
    bra     6f
    nop
4:  mov.l   @(R_ZEXACT, gbr), r0
    mov.l   .LzFast, r9
    cmp/ge  r9, r0
    bt      5f
    mov     r9, r0
5:  add     #1, r0
    mov     r0, r7
    mov.l   @(R_ZMAX, gbr), r0
    sub     r7, r0
    mov     r0, r9
    mov.l   @(R_SAFE, gbr), r0
    tst     #1, r0
    bf      6f
    mov     #0, r9              ! every vertex odd
6:  mova    .Lloops, r0
    mov.l   @(r0, r1), r1
    jmp     @r1
    nop

    .align 2
.LedgeX:    .long   176 << 16
.LzFast:    .long   Z_FAST
.LfrXz:     .long   (353 << 16) - 1
.LfrYz:     .long   (225 << 16) - 1
.Lloops:
    .long   .Lrooms000, .Lrooms100, .Lrooms010, .Lrooms110, .Lrooms001, .Lrooms101

    ROOMS   0, 0, 0
    ROOMS   1, 0, 0
    ROOMS   0, 1, 0
    ROOMS   1, 1, 0
    ROOMS   0, 0, 1
    ROOMS   1, 0, 1

.Ldone:
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

    ! x, y (r12, r13) times zMax (r0) / z (r11), the divider's 64 / 32 at
    ! r1 (DVSR set): projected at the far plane, they land where the vertex
    ! does; r14
    .macro  FAR_SCALE
    dmuls.l r0, r12
    sts     mach, r14
    mov.l   r14, @(16, r1)      ! DVDNTH
    sts     macl, r14
    mov.l   r14, @(20, r1)      ! DVDNTL: divides
    mov.l   @(20, r1), r12
    dmuls.l r0, r13
    sts     mach, r14
    mov.l   r14, @(16, r1)
    sts     macl, r14
    mov.l   r14, @(20, r1)
    mov.l   @(20, r1), r13
    .endm

    ! x, y (r12, r13) within what >> 8 holds in 16 bits; r0, r1
    .macro  XY_BOUND
    mov.l   .LxyMax, r1
    neg     r1, r0
    cmp/gt  r1, r12
    bf      91f
    mov     r1, r12
91: cmp/ge  r0, r12
    bt      92f
    mov     r0, r12
92: cmp/gt  r1, r13
    bf      93f
    mov     r1, r13
93: cmp/ge  r0, r13
    bt      94f
    mov     r0, r13
94:
    .endm

    ! The vertex whichever way it is: x, y, z (r12, r13, r11) -> x at the loop's scale and
    ! edge in r12, y at its scale in MACL, z >> 8 in r11, r14 the plane's flag; r0, r1
.Lodd:
    tst     r9, r9
    bt      .LoddAll
    mov.l   .LzNearFast, r0
    cmp/gt  r0, r11
    bf      .LoddAll
    mov.l   @(R_ZEXACT, gbr), r0
    cmp/gt  r0, r11
    bt      .LoddAll
.LnearFast:
    mov.l   @(R_NEAR, gbr), r0
    mov     r0, r1
    shlr8   r11
    mov     r11, r0
    add     r0, r0
    mov.w   @(r0, r1), r1
    shll2   r1
    shll2   r1
    shlr8   r12
    shlr8   r13
    muls.w  r1, r12
    sts     macl, r12
    add     r2, r12
    rts
    muls.w  r1, r13
.LoddAll:
    mov.l   @(R_ZMAX, gbr), r0
    cmp/ge  r0, r11
    bt      .Lfar
    cmp/ge  r7, r11
    bf      1f
    ! a room with x, y perhaps past what >> 8 holds, z where the loop's table goes
    XY_BOUND
    shlr8   r11
    shlr8   r12
    mov     r11, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r10), r1
    shlr8   r13
    muls.w  r1, r12
    sts     macl, r12
    add     r2, r12
    rts
    muls.w  r1, r13
1:  mov.l   .LzNearFast, r0
    cmp/gt  r0, r11
    bf      2f
    mov.l   @(R_ZEXACT, gbr), r0
    cmp/gt  r0, r11
    bt      3f
    XY_BOUND
    bra     .LnearFast
    nop
2:  mov.l   @(R_ZMIN, gbr), r0
    cmp/gt  r0, r11
    bt      3f
    mov     r0, r11             ! behind the near plane
    bra     3f
    mov     #1, r14

    ! past the far plane: x and y by 0x1000000 / z to the nearest 256 of z, which lands them
    ! within a pixel or so where they lie; z at it. Only fills want them.
.Lfar:
    .ifdef RENDER_LAB
    mov.l   @(R_FAR_TABLE, gbr), r0
    tst     r0, r0
    bt      .LfarExact
    .endif
    tst     r9, r9
    bf      1f
    XY_BOUND
1:  mov.l   @(R_FAR_TABLE, gbr), r0
    mov     r0, r1
    swap.w  r11, r0
    extu.w  r0, r0
    mov.w   .LfarLast, r14
    cmp/hi  r14, r0
    bf      2f
    mov     r14, r0
2:  add     r0, r0
    mov.w   @(r0, r1), r1
    mov.l   @(R_ZMAX, gbr), r0
    mov     r0, r11
    shlr8   r11
    mov     #1, r14
    shlr8   r12
    shlr8   r13
    muls.w  r1, r12
    sts     macl, r12
    add     r2, r12
    rts
    muls.w  r1, r13

    .ifdef RENDER_LAB
    ! ... or exactly, or as the original had them: as the lab's captures did
.LfarExact:
    mov.l   @(R_FAR_SCALED, gbr), r0
    tst     r0, r0
    bt      2f
    mov     #-1, r1
    shll8   r1                  ! the divider at 0xFFFFFF00
    mov.l   r11, @r1            ! DVSR: z
    mov.l   @(R_ZMAX, gbr), r0
    FAR_SCALE
2:  mov.l   @(R_ZMAX, gbr), r0
    mov     r0, r11
    mov     #1, r14
    .endif
    ! x, y to 16-bit view coordinates: within the bound, >> 8
3:  tst     r9, r9
    bf      7f
    XY_BOUND
7:  shlr8   r12
    shlr8   r13
    ! d: by the table, or exact up close
    mov.l   @(R_ZEXACT, gbr), r0
    cmp/gt  r0, r11
    shlr8   r11
    bt      8f
    mov.l   @(R_NEAR, gbr), r0
    mov     r0, r1
    mov     r11, r0
    add     r0, r0
    bra     9f
    mov.w   @(r0, r1), r1
8:  mov.l   @(R_DIV, gbr), r0
    mov     r0, r1
    mov     r11, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r1), r1
9:  muls.w  r1, r12
    sts     macl, r12
    muls.w  r1, r13
    sts     macl, r13
    ! screen x, y = (coordinate * d) >> 12, within what's projected: to the loop's scale
    mov.l   .LpMax, r1
    neg     r1, r0
    cmp/gt  r1, r12
    bf      10f
    mov     r1, r12
10: cmp/ge  r0, r12
    bt      11f
    mov     r0, r12
11: cmp/gt  r1, r13
    bf      12f
    mov     r1, r13
12: cmp/ge  r0, r13
    bt      13f
    mov     r0, r13
13: shll2   r12
    shll2   r12
    add     r2, r12
    shll2   r13
    shll2   r13
    rts
    lds     r13, macl

    .align 2
.LzNearFast: .long  (513 << 8) - 1   ! past this d << 4 fits a word
.LxyMax:    .long   0x007FFF00  ! view x, y whose >> 8 fits in 16 bits
.LpMax:     .long   0x007FF000  ! projected 2047 << 12
.LfarLast:  .word   VERTICES_FAR_LAST
    .align  2

    ! uint32 roomSafeAsm(const FaceRoom* room, const uint8* reach, int32 zMax): what its rotations are sure of,
    ! each the row's translation plus each column's coefficient (m * 4) by that axis's reach, where that lies
    ! below 0 for the least and above for the most
    .equ    ROOM_M,         44      ! FaceRoom's int16 m[9]
    .equ    ROOM_T,         64      ! ... and int32 t[3]
    .equ    VIEW_EXACT_8,   2048 << 8

    .macro  ROOM_ROW row
    mov.l   @(4 * \row, r4), r1
    mov     r1, r2
    .irp    k, 0, 1, 2
    mov.w   @r3+, r0
    .if \k == 0
    muls.w  r0, r7
    .elseif \k == 1
    muls.w  r0, r8
    .else
    muls.w  r0, r9
    .endif
    sts     macl, r0
    shll2   r0
    cmp/pz  r0
    bt      1f
    bra     2f
    add     r0, r1
1:  add     r0, r2
2:
    .endr
    .endm

    .align  2
    .global _roomSafeAsm
_roomSafeAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov     r6, r3              ! (zMax, till it's wanted)
    mov.b   @r5+, r0
    extu.b  r0, r7
    mov.b   @r5+, r0
    extu.b  r0, r8
    mov.b   @r5, r0
    extu.b  r0, r9
    mov     r3, r5              ! zMax
    mov     r4, r3
    add     #ROOM_M, r3
    add     #ROOM_T, r4
    mov.l   .LsafeBound, r6
    ROOM_ROW 0
    cmp/gt  r6, r2
    bt      8f
    neg     r6, r0
    cmp/ge  r0, r1
    bf      8f
    ROOM_ROW 1
    cmp/gt  r6, r2
    bt      8f
    neg     r6, r0
    cmp/ge  r0, r1
    bf      8f
    ROOM_ROW 2
    mov.l   .LsafeNear, r0
    cmp/gt  r0, r1              ! the nearest past where the divides are exact
    bf      7f
    cmp/ge  r5, r2              ! the farthest short of the far plane
    bt      7f
    mov     #3, r0              ! ROOM_SAFE_XY | ROOM_SAFE_Z
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8
7:  mov     #1, r0              ! ROOM_SAFE_XY
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8
8:  mov     #0, r0
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    .align  2
.LsafeBound:    .long   0x007FFF00  ! view x, y whose >> 8 fits in 16 bits
.LsafeNear:     .long   VIEW_EXACT_8


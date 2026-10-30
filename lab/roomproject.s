! Stage 2 the new way (vertices.h): a room's vertices projected from their
! rotation, which the DSP makes: saturn/xform.s from its multiplies on, the
! arithmetic unchanged.
!
! void roomProjectAsm(const RoomRotated* src, int32 count, Vertex* dst, RoomProject* rp)
!
! RoomRotated: int32 x, y, z, each the room's row times the vertex (its bytes
! times 4) plus the translation, as xform.s sums them.
! RoomProject (vertices.h):
!   +0  const RoomVertex* points    the room's, for their shades (byte 3)
!   +4  int32 x0, y0, x1, y1        viewport
!   +20 const uint16* divTable
!   +24 int32 zMin, zMax            near and far planes << 8
!   +32 const int8* wibble          ripple offsets (32), or NULL
!   +36 int32 phase                 ripple phase
!   +40 const int16* caustics       shade offsets (32), by vertex index, or NULL
!   +44 int32 count                 the room's vertex count
!
! Vertex (render.h): int16 x, y; int16 z; uint8 g, clip.
!
! A room out of water and off its surface (no caustics, no ripple) takes a
! second loop, the same arithmetic in fewer instructions: the planes and
! the view bound in registers, the projection at GBR; a coordinate's frame
! test on its product plus the frame's near edge, one unsigned compare that
! also shows the product needs no clamp; the window's flags shifted in under
! the plane's; the fog added past the shade's >> 8 (min(31, shade + fog >>
! 8) is (shade << 8 + fog, at most 8191) >> 8). What is off the frame, cut
! by a plane or beyond the view bound goes out of the loop and back.

    .equ    CLIP_FRAME,     1
    .equ    CLIP_LEFT,      2
    .equ    CLIP_RIGHT,     4
    .equ    CLIP_TOP,       8
    .equ    CLIP_BOTTOM,    16
    .equ    CLIP_PLANE,     32

    .text
    .align 2
    .global _roomProjectAsm
_roomProjectAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15

    tst     r5, r5
    bf      .Lbegin
    bra     .Ldone
    nop
.Lbegin:
    mov.l   @(20, r7), r10      ! divTable
    mov.l   @r7, r8             ! the points
    mov.l   @(32, r7), r0       ! ripple
    mov.l   @(40, r7), r1       ! caustics
    or      r1, r0
    tst     r0, r0
    bf      .Lloop
    bra     .Ldry
    nop

    ! the loop's rare cases, here where its conditional branches reach
.LzNear:
    mov     r0, r11             ! behind the near plane
    bra     .LzChecked
    mov     #CLIP_PLANE, r14
.LzFar:
    mov     r0, r11             ! past the far plane
    bra     .LzChecked
    mov     #CLIP_PLANE, r14

.LclampXY:                      ! r1 = the largest x, y; r2 = -r1
    cmp/gt  r1, r12
    bf      1f
    mov     r1, r12
1:  cmp/ge  r2, r12
    bt      2f
    mov     r2, r12
2:  cmp/gt  r1, r13
    bf      3f
    mov     r1, r13
3:  cmp/ge  r2, r13
    bt      4f
    mov     r2, r13
4:  bra     .LclampedXY
    nop

.LclampP:                       ! r1 = the largest projected x, y; r2 = -r1
    cmp/gt  r1, r12
    bf      1f
    mov     r1, r12
1:  cmp/ge  r2, r12
    bt      2f
    mov     r2, r12
2:  cmp/gt  r1, r13
    bf      3f
    mov     r1, r13
3:  cmp/ge  r2, r13
    bt      4f
    mov     r2, r13
4:  bra     .LclampedP
    nop

.Lloop:
    ! the rotated x, y, z; the shade to r3
    mov.b   @(3, r8), r0
    mov.l   @r4+, r12
    extu.b  r0, r3
    mov.l   @r4+, r13
    mov.l   @r4+, r11
    add     #4, r8
    mov     #0, r14             ! clip flags

    ! near and far planes: clamp z, flag the vertex
    mov.l   @(24, r7), r0
    cmp/gt  r0, r11
    bf      .LzNear
    mov.l   @(28, r7), r0
    cmp/ge  r0, r11
    bt      .LzFar
.LzChecked:
    ! x, y to 16-bit view coordinates: clamp, >> 8. Each within -max .. max
    ! is x + max within 0 .. 2 max, unsigned.
    mov.l   .LxyMax, r1
    mov     r1, r9
    add     r9, r9              ! 2 max
    mov     r12, r0
    add     r1, r0
    cmp/hi  r9, r0
    bt/s    .LclampXY
    neg     r1, r2
    mov     r13, r0
    add     r1, r0
    cmp/hi  r9, r0
    bt      .LclampXY
.LclampedXY:
    shlr8   r12
    exts.w  r12, r12
    shlr8   r13
    exts.w  r13, r13

    ! z >> 8; d = divTable[z >> PROJ_SHIFT] (z is at most VIEW_MAX, inside the table)
    shlr8   r11
    mov     r11, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r10), r0

    ! screen x, y = (coordinate * d) >> 12, clamped, + centre
    muls.w  r0, r12
    sts     macl, r12
    muls.w  r0, r13
    mov.l   .LpMax, r1
    sts     macl, r13
    mov     r1, r9
    add     r9, r9
    mov     r12, r0
    add     r1, r0
    cmp/hi  r9, r0
    bt/s    .LclampP
    neg     r1, r2
    mov     r13, r0
    add     r1, r0
    cmp/hi  r9, r0
    bt      .LclampP
.LclampedP:
    shlr8   r12
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
    bt      11f
    mov.w   .LframeHeight, r0
    cmp/hi  r0, r13
    bf      12f
11: add     #CLIP_FRAME, r14
    mov.w   .LcoordMax, r1
    neg     r1, r2
    add     #-1, r2             ! -1024
    cmp/gt  r1, r12
    bf      13f
    mov     r1, r12
13: cmp/ge  r2, r12
    bt      14f
    mov     r2, r12
14: cmp/gt  r1, r13
    bf      15f
    mov     r1, r13
15: cmp/ge  r2, r13
    bt      12f
    mov     r2, r13
12:
    ! viewport (each delay slot loads the next bound)
    mov.l   @(4, r7), r0
    cmp/gt  r12, r0
    bf/s    16f
    mov.l   @(12, r7), r0
    add     #CLIP_LEFT, r14
16: cmp/gt  r0, r12
    bf/s    17f
    mov.l   @(8, r7), r0
    add     #CLIP_RIGHT, r14
17: cmp/gt  r13, r0
    bf/s    18f
    mov.l   @(16, r7), r0
    add     #CLIP_TOP, r14
18: cmp/gt  r0, r13
    bf      19f
    add     #CLIP_BOTTOM, r14
19:
    shll8   r3                  ! shade << 8 from here
    mov.l   @(40, r7), r1       ! caustics: wet rooms only
    tst     r1, r1
    bt      23f

    ! caustics: shade += caustics[vertex index & 31], within 0..8191
    mov.l   @(44, r7), r0
    sub     r5, r0              ! index = count - vertices left
    and     #31, r0
    add     r0, r0
    mov.w   @(r0, r1), r0
    add     r0, r3
    cmp/pz  r3
    bt      21f
    mov     #0, r3
21: mov.w   .LshadeMax, r1
    cmp/gt  r1, r3
    bf      23f
    mov     r1, r3
23:
    mov.l   @(32, r7), r9       ! ripple table: rooms across the water surface
    tst     r9, r9
    bt      22f

    ! ripple of what is on the frame: x, y += wibble[(phase + x or y) & 31]
    mov     r14, r0
    tst     #CLIP_FRAME, r0
    bf      22f
    mov.l   @(36, r7), r1
    mov     r12, r0
    add     r1, r0
    and     #31, r0
    mov.b   @(r0, r9), r0
    add     r0, r12
    mov     r13, r0
    add     r1, r0
    and     #31, r0
    mov.b   @(r0, r9), r0
    add     r0, r13
22:
    ! fog: shade += z - FOG_MIN, at most 8191
    mov.w   .LfogMin, r1
    mov     r11, r0
    sub     r1, r0
    cmp/pl  r0
    bf      20f
    add     r0, r3
    mov.w   .LshadeMax, r1
    cmp/gt  r1, r3
    bf      20f
    mov     r1, r3
20: shlr8   r3
    ! store x|y, z|g|clip
    shll16  r12
    extu.w  r13, r13
    or      r13, r12
    mov.l   r12, @r6
    shll16  r11
    shll8   r3
    or      r3, r11
    or      r14, r11
    mov.l   r11, @(4, r6)

    dt      r5
    bt      .Ldone
    bra     .Lloop              ! the loop is too long for a conditional branch
    add     #8, r6

.Ldone:
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    .align 2
.LxyMax:        .long   0x007FFF00  ! view x, y whose >> 8 fits in 16 bits
.LpMax:         .long   0x007FF000  ! projected 2047 << 12
.LhalfWidth:    .short  176         ! FRAME_WIDTH / 2
.LhalfHeight:   .short  112         ! FRAME_HEIGHT / 2
.LframeWidth:   .short  352         ! FRAME_WIDTH
.LframeHeight:  .short  224         ! FRAME_HEIGHT
.LcoordMax:     .short  1023        ! VDP1_COORD_MAX
.LfogMin:       .short  12288       ! FOG_MIN
.LshadeMax:     .short  8191        ! shade << 8 at its darkest

    ! --- a dry room --------------------------------------------------------
    ! r2 2 * the view bound, r7 zMin, r9 the view bound, GBR the projection;
    ! r14 the plane's flag (0 or 1) until the window's are shifted in under it

    .align 2
.Ldry:
    stc     gbr, r0
    mov.l   r0, @-r15
    ldc     r7, gbr
    mov.l   @(24, r7), r7       ! zMin
    mov.l   .LdXyMax, r9
    mov     r9, r2
    bra     .Ldloop
    add     r2, r2

    ! the loop's rare cases, here where its conditional branches reach
.LdNear:                        ! behind the near plane
    mov     r7, r11
    bra     .LdZ
    mov     #1, r14
.LdFar:                         ! past the far plane
    mov     r0, r11
    bra     .LdZ
    mov     #1, r14

.LdClampXY:                     ! x, y within the view bound
    neg     r9, r1
    cmp/gt  r9, r12
    bf      1f
    mov     r9, r12
1:  cmp/ge  r1, r12
    bt      2f
    mov     r1, r12
2:  cmp/gt  r9, r13
    bf      3f
    mov     r9, r13
3:  cmp/ge  r1, r13
    bt      .LdXY
    bra     .LdXY
    mov     r1, r13

    ! off the frame, or a product beyond what is projected: the first loop's
    ! way, each product clamped and made a coordinate, both then flagged and
    ! clamped to what VDP1 can address, the window's flags added
.LdOffX:                        ! r12 x's product + the frame's near edge, MACL y's
    mov.l   .LdKx, r0
    sub     r0, r12
    sts     macl, r13
    mov.l   .LdPMax, r1
    neg     r1, r0
    cmp/gt  r1, r12
    bf      1f
    mov     r1, r12
1:  cmp/ge  r0, r12
    bt      2f
    mov     r0, r12
2:  shlr8   r12
    shlr2   r12
    shlr2   r12
    exts.w  r12, r12
    mov.w   .LdHalfWidth, r0
    bra     .LdOffY2
    add     r0, r12
.LdOffY:                        ! x on the frame; r13 y's product + the frame's near edge
    mov.l   .LdKy, r0
    sub     r0, r13
    mov.l   .LdPMax, r1
.LdOffY2:
    neg     r1, r0
    cmp/gt  r1, r13
    bf      1f
    mov     r1, r13
1:  cmp/ge  r0, r13
    bt      2f
    mov     r0, r13
2:  shlr8   r13
    shlr2   r13
    shlr2   r13
    exts.w  r13, r13
    mov.w   .LdHalfHeight, r0
    add     r0, r13
    shll2   r14                 ! the plane's flag in its place
    shll2   r14
    shll    r14
    add     #CLIP_FRAME, r14
    mov.w   .LdCoordMax, r1
    mov     r1, r0
    not     r0, r0              ! -1024
    cmp/gt  r1, r12
    bf      3f
    mov     r1, r12
3:  cmp/ge  r0, r12
    bt      4f
    mov     r0, r12
4:  cmp/gt  r1, r13
    bf      5f
    mov     r1, r13
5:  cmp/ge  r0, r13
    bt      6f
    mov     r0, r13
6:  mov.l   @(4, gbr), r0
    cmp/gt  r12, r0
    bf/s    7f
    mov.l   @(12, gbr), r0
    add     #CLIP_LEFT, r14
7:  cmp/gt  r0, r12
    bf/s    8f
    mov.l   @(8, gbr), r0
    add     #CLIP_RIGHT, r14
8:  cmp/gt  r13, r0
    bf/s    9f
    mov.l   @(16, gbr), r0
    add     #CLIP_TOP, r14
9:  cmp/gt  r0, r13
    bf      .LdShade
    bra     .LdShade
    add     #CLIP_BOTTOM, r14

.Ldloop:
    ! the rotated x, y, z; the shade to r3
    mov.b   @(3, r8), r0
    mov.l   @r4+, r12
    extu.b  r0, r3
    mov.l   @r4+, r13
    mov.l   @r4+, r11
    add     #4, r8
    cmp/gt  r7, r11
    bf/s    .LdNear
    mov     #0, r14
    mov.l   @(28, gbr), r0      ! zMax
    cmp/ge  r0, r11
    bt      .LdFar
.LdZ:
    ! x, y + the bound, or'd, within 2 * the bound: neither needs a clamp
    mov     r12, r0
    add     r9, r0
    mov     r13, r1
    add     r9, r1
    or      r1, r0
    cmp/hi  r2, r0
    bt      .LdClampXY
.LdXY:
    shlr8   r12
    exts.w  r12, r12
    shlr8   r13
    exts.w  r13, r13
    shlr8   r11                 ! z >> 8; d = divTable[z >> PROJ_SHIFT]
    mov     r11, r0
    shlr2   r0
    shlr2   r0
    add     r0, r0
    mov.w   @(r0, r10), r1

    ! each product + the frame's near edge, unsigned, within its width
    ! (4096 a pixel) is a coordinate on the frame from a product in range
    muls.w  r1, r12
    mov.l   .LdKx, r0
    sts     macl, r12
    muls.w  r1, r13
    add     r0, r12
    mov.l   .LdWx, r0
    cmp/hi  r0, r12
    bt      .LdOffX
    shlr8   r12
    shlr2   r12
    shlr2   r12
    sts     macl, r13
    mov.l   .LdKy, r0
    add     r0, r13
    mov.l   .LdWy, r0
    cmp/hi  r0, r13
    bt      .LdOffY
    shlr8   r13
    shlr2   r13
    shlr2   r13

    ! on the frame: the window's flags, bottom to left, then 0 for the frame
    mov.l   @(16, gbr), r0
    cmp/gt  r0, r13
    rotcl   r14
    mov.l   @(8, gbr), r0
    cmp/gt  r13, r0
    rotcl   r14
    mov.l   @(12, gbr), r0
    cmp/gt  r0, r12
    rotcl   r14
    mov.l   @(4, gbr), r0
    cmp/gt  r12, r0
    rotcl   r14
    shll    r14
.LdShade:
    ! fog: shade + (z - FOG_MIN) >> 8, at most 31
    mov.w   .LdFogMin, r1
    mov     r11, r0
    sub     r1, r0
    cmp/pl  r0
    bf      1f
    shlr8   r0
    add     r0, r3
    mov     #31, r1
    cmp/gt  r1, r3
    bf      1f
    mov     r1, r3
1:  ! store x|y, z|g|clip
    shll16  r12
    extu.w  r13, r13
    or      r13, r12
    mov.l   r12, @r6
    shll16  r11
    shll8   r3
    or      r3, r11
    or      r14, r11
    mov.l   r11, @(4, r6)
    dt      r5
    bf/s    .Ldloop
    add     #8, r6

    mov.l   @r15+, r0
    bra     .Ldone
    ldc     r0, gbr

    .align 2
.LdXyMax:       .long   0x007FFF00  ! the view bound
.LdPMax:        .long   0x007FF000  ! projected 2047 << 12
.LdKx:          .long   176 << 12   ! the frame's near edges: FRAME_WIDTH / 2 pixels
.LdWx:          .long   (353 << 12) - 1     ! ... its width, of 0 .. 352
.LdKy:          .long   112 << 12
.LdWy:          .long   (225 << 12) - 1
.LdHalfWidth:   .short  176
.LdHalfHeight:  .short  112
.LdCoordMax:    .short  1023
.LdFogMin:      .short  12288

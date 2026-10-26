! Room vertex transform for the slave CPU (rooms.cpp): projectRoomPoint's
! arithmetic, with the matrix rows on the multiply-accumulate unit and each
! vertex written as two longwords. Water rooms add caustics to the shade and
! ripple on screen.
!
! void transformRoomAsm(const RoomVertex* src, int32 count, Vertex* dst, RoomXform* xf)
!
! transformRoomDspAsm takes the vertices in view space instead, as the SCU DSP
! writes them (xform.dsp): four longwords each, g z x y, the matrix applied;
! a cache line each, purged before they are read.
!
! void transformRoomDspAsm(const int32* src, int32 count, Vertex* dst, RoomXform* xf)
!
! RoomXform (rooms.cpp):
!   +0  int16 v[3]      scratch: the vertex being transformed, times 4
!   +6  int16 rows[9]   rotation rows: m00 m01 m02 m10 m11 m12 m20 m21 m22
!   +24 int32 tx, ty, tz    view position of the room origin << 8
!   +36 int32 x0, y0, x1, y1    viewport
!   +52 const uint16* divTable
!   +56 int32 zMin, zMax    near and far planes << 8
!   +64 const int8* wibble  ripple offsets (32), or NULL for rooms not under water
!   +68 int32 phase         ripple phase
!   +72 const int16* caustics   shade offsets (32), by vertex index
!   +76 int32 count         the index past the last vertex: count - vertices left
!                           is a vertex's index in its room
!
! Vertex (render.h): int16 x, y; int16 z; uint8 g, clip.

    .equ    CLIP_FRAME,     1
    .equ    CLIP_LEFT,      2
    .equ    CLIP_RIGHT,     4
    .equ    CLIP_TOP,       8
    .equ    CLIP_BOTTOM,    16
    .equ    CLIP_PLANE,     32

    .text
    .align 2

! The function body; dsp 1 reads the DSP's records, 0 room vertices.
    .macro  TRANSFORM dsp
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15

    tst     r5, r5
    bf      .Lbegin\dsp
    bra     .Ldone\dsp
    nop
.Lbegin\dsp:
    mov.l   @(52, r7), r10      ! divTable
    .if \dsp
    ! the DSP wrote the records behind the cache: purge their lines
    mov.l   .Lpurge\dsp, r1
    or      r4, r1
    mov     r5, r2
1:  mov.l   r2, @r1
    dt      r2
    bf/s    1b
    add     #16, r1
    .endif
    bra     .Lloop\dsp
    nop

    ! the loop's rare cases, here where its conditional branches reach
.LzNear\dsp:
    mov     r0, r11             ! behind the near plane
    bra     .LzChecked\dsp
    mov     #CLIP_PLANE, r14
.LzFar\dsp:
    mov     r0, r11             ! past the far plane
    bra     .LzChecked\dsp
    mov     #CLIP_PLANE, r14

.LclampXY\dsp:                  ! r1 = the largest x, y; r2 = -r1
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
4:  bra     .LclampedXY\dsp
    nop

.LclampP\dsp:                   ! r1 = the largest projected x, y; r2 = -r1
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
4:  bra     .LclampedP\dsp
    nop

.Lloop\dsp:
    .if \dsp
    mov.l   @r4+, r3            ! g
    mov.l   @r4+, r11           ! z
    mov     #0, r14             ! clip flags
    .else
    ! unpack x, y, z (256 world units) times 4 into the scratch vector; g to r3
    mov.b   @r4+, r0
    extu.b  r0, r0
    shll2   r0
    mov.w   r0, @(0, r7)
    mov.b   @r4+, r0
    extu.b  r0, r0
    shll2   r0
    mov.w   r0, @(2, r7)
    mov.b   @r4+, r0
    extu.b  r0, r0
    shll2   r0
    mov.w   r0, @(4, r7)
    mov.b   @r4+, r3
    extu.b  r3, r3

    ! z = tz + v . row2
    mov.l   @(32, r7), r0
    lds     r0, macl
    mov     r7, r8
    mov     r7, r9
    add     #18, r9
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mov     #0, r14             ! clip flags
    sts     macl, r11
    .endif

    ! near and far planes: clamp z, flag the vertex
    mov.l   @(56, r7), r0
    cmp/gt  r0, r11
    bf      .LzNear\dsp
    mov.l   @(60, r7), r0
    cmp/ge  r0, r11
    bt      .LzFar\dsp
.LzChecked\dsp:
    .if \dsp
    mov.l   @r4+, r12           ! x
    mov.l   @r4+, r13           ! y
    mov.l   .LxyMax\dsp, r1
    .else
    ! x = tx + v . row0, y = ty + v . row1
    mov.l   @(24, r7), r0
    lds     r0, macl
    mov     r7, r8
    mov     r7, r9
    add     #6, r9
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mov.l   @(28, r7), r0
    sts     macl, r12
    lds     r0, macl
    mov     r7, r8
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mac.w   @r8+, @r9+
    mov.l   .LxyMax\dsp, r1
    sts     macl, r13
    .endif

    ! x, y to 16-bit view coordinates: clamp, >> 8. Each within -max .. max
    ! is x + max within 0 .. 2 max, unsigned.
    mov     r1, r8
    add     r8, r8              ! 2 max (r8 is free after the multiplies)
    mov     r12, r0
    add     r1, r0
    cmp/hi  r8, r0
    bt/s    .LclampXY\dsp
    neg     r1, r2
    mov     r13, r0
    add     r1, r0
    cmp/hi  r8, r0
    bt      .LclampXY\dsp
.LclampedXY\dsp:
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
    mov.l   .LpMax\dsp, r1
    sts     macl, r13
    mov     r1, r8
    add     r8, r8
    mov     r12, r0
    add     r1, r0
    cmp/hi  r8, r0
    bt/s    .LclampP\dsp
    neg     r1, r2
    mov     r13, r0
    add     r1, r0
    cmp/hi  r8, r0
    bt      .LclampP\dsp
.LclampedP\dsp:
    shlr8   r12
    shlr2   r12
    shlr2   r12
    exts.w  r12, r12
    mov.w   .LhalfWidth\dsp, r0
    add     r0, r12
    shlr8   r13
    shlr2   r13
    shlr2   r13
    exts.w  r13, r13
    mov.w   .LhalfHeight\dsp, r0
    add     r0, r13

    ! off the frame: flag, and clamp to what VDP1 can address
    mov.w   .LframeWidth\dsp, r0
    cmp/hi  r0, r12
    bt      11f
    mov.w   .LframeHeight\dsp, r0
    cmp/hi  r0, r13
    bf      12f
11: add     #CLIP_FRAME, r14
    mov.w   .LcoordMax\dsp, r1
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
    mov.l   @(36, r7), r0
    cmp/gt  r12, r0
    bf/s    16f
    mov.l   @(44, r7), r0
    add     #CLIP_LEFT, r14
16: cmp/gt  r0, r12
    bf/s    17f
    mov.l   @(40, r7), r0
    add     #CLIP_RIGHT, r14
17: cmp/gt  r13, r0
    bf/s    18f
    mov.l   @(48, r7), r0
    add     #CLIP_TOP, r14
18: cmp/gt  r0, r13
    bf      19f
    add     #CLIP_BOTTOM, r14
19:
    shll8   r3                  ! shade << 8 from here
    mov     #64, r0
    mov.l   @(r0, r7), r9       ! ripple table: under water only
    tst     r9, r9
    bt      22f

    ! caustics: shade += caustics[vertex index & 31], within 0..8191
    mov     #72, r0
    mov.l   @(r0, r7), r1       ! caustics
    mov     #76, r0
    mov.l   @(r0, r7), r0
    sub     r5, r0              ! index = count - vertices left
    and     #31, r0
    add     r0, r0
    mov.w   @(r0, r1), r0
    add     r0, r3
    cmp/pz  r3
    bt      21f
    mov     #0, r3
21: mov.w   .LshadeMax\dsp, r1
    cmp/gt  r1, r3
    bf      23f
    mov     r1, r3
23:
    ! ripple of what is on the frame: x, y += wibble[(phase + x or y) & 31]
    mov     r14, r0
    tst     #CLIP_FRAME, r0
    bf      22f
    mov     #68, r0
    mov.l   @(r0, r7), r1
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
    mov.w   .LfogMin\dsp, r1
    mov     r11, r0
    sub     r1, r0
    cmp/pl  r0
    bf      20f
    add     r0, r3
    mov.w   .LshadeMax\dsp, r1
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
    bt      .Ldone\dsp
    bra     .Lloop\dsp          ! the loop is too long for a conditional branch
    add     #8, r6

.Ldone\dsp:
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    .align 2
.LxyMax\dsp:        .long   0x007FFF00  ! view x, y whose >> 8 fits in 16 bits
.LpMax\dsp:         .long   0x007FF000  ! projected 2047 << 12
.LhalfWidth\dsp:    .short  176         ! FRAME_WIDTH / 2
.LhalfHeight\dsp:   .short  112         ! FRAME_HEIGHT / 2
.LframeWidth\dsp:   .short  352         ! FRAME_WIDTH
.LframeHeight\dsp:  .short  224         ! FRAME_HEIGHT
.LcoordMax\dsp:     .short  1023        ! VDP1_COORD_MAX
.LfogMin\dsp:       .short  12288       ! FOG_MIN
.LshadeMax\dsp:     .short  8191        ! shade << 8 at its darkest
    .align 2
    .if \dsp
.Lpurge\dsp:       .long   0x40000000  ! the cache's associative purge area
    .endif
    .endm

    .global _transformRoomAsm
_transformRoomAsm:
    TRANSFORM 0

    .global _transformRoomDspAsm
_transformRoomDspAsm:
    TRANSFORM 1

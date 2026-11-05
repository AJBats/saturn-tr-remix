! Water's shimmers, a sheet of a room's water at a time (faces.h ShimmerSheet; faces.cpp sets it up).
!
! int32 facesShimmersAsm(ShimmerSheet* sheet, const uint8_t* runs, int32 count)
!     runs: count of (z, x, sectors, shade) bytes; returns 0 if the list ran out.
!
! Each sector in view of the room's clip owes shimmers by its area on the frame. Each is a place with a
! clock of its own: when its 8 frames are up the place lights somewhere else in the sector. Near, where
! a sector owes many and most would fall off the frame, it goes a cell at a time, a quarter of its side.
!
! What's tested of a sector is linear in its x and z: o + e * x + f * z. Over a run of the same z, the
! o + f * z is kept (ROW_ below). A plane's value is how far past it the sector's middle lies; past by
! more than half the sector's reach along it, the whole sector is.
!
! GBR the sheet. r15 the frame below. Through the runs: r14 the runs' bytes, r13 runs left, r12 z, r11 x,
! r10 sectors left, r9 shade, r8 the z the rows were made for.
! Through a sector's shimmers (.Lplace): r8 shimmers left, r9 the piece, r10 the age's bits; r1 sy, r2 now,
! r3 life, r4 the list, r5 the command, r6 dd, r7 sx.

    .ifndef RENDER_LAB

    .equ    S_LIST,         0
    .equ    S_STOP,         4       ! the list's commands stop here
    .equ    S_DIV,          8
    .equ    S_DEPTH,        12
    .equ    S_BAG,          16
    .equ    S_PIECES,       20
    .equ    S_COLOURS,      24
    .equ    S_AGES,         28
    .equ    S_TABLES,       32
    .equ    S_FRAME,        36
    .equ    S_SPRITE_DD,    40      ! from this dd, art; below it, a line
    .equ    S_X0,           44      ! the room's clip, from the frame's middle
    .equ    S_Y0,           48
    .equ    S_X1,           52
    .equ    S_Y1,           56
    .equ    S_HEIGHT,       60
    .equ    S_M0,           64
    .equ    S_M2,           68
    .equ    S_M3,           72
    .equ    S_M5,           76
    .equ    S_M6,           80
    .equ    S_M8,           84
    .equ    S_TURN,         88
    .equ    S_OX,           92
    .equ    S_EX,           96
    .equ    S_FX,           100
    .equ    S_OY,           104
    .equ    S_EY,           108
    .equ    S_FY,           112
    .equ    S_OZ,           116
    .equ    S_EZ,           120
    .equ    S_FZ,           124
    .equ    S_NEAR_Z,       128
    .equ    S_FAR_Z,        132
    .equ    S_REACH,        136
    .equ    S_NEAR_CELL,    140
    .equ    S_R,            144     ! right: o, e, f, past, past for a cell
    .equ    S_L,            164     ! left
    .equ    S_B,            184     ! bottom
    .equ    S_T,            204     ! top
    .equ    S_SEED,         224     ! a sector's, while its cells go
    .equ    S_CELL,         228

    .equ    P_O,            0
    .equ    P_E,            4
    .equ    P_F,            8
    .equ    P_PAST,         12
    .equ    P_PAST_CELL,    16

    ! ShimmerPiece
    .equ    A_HALF_W,       4
    .equ    A_HALF_H,       5
    .equ    A_W,            6

    ! the frame
    .equ    F_CX,           0       ! the stretch's middle, 65536 to a view unit
    .equ    F_CY,           4
    .equ    F_CZ,           8
    .equ    F_SEED,         12      ! the next shimmer's
    .equ    F_PART,         16      ! the stretch's share of a shimmer, for its first
    .equ    F_DEPTH,        20      ! its bucket, doubled
    .equ    F_LEVEL,        24
    .equ    F_SPREAD,       28      ! places go a quarter as far from the middle
    .equ    F_R8,           32
    .equ    F_R9,           36
    .equ    F_R10,          40
    .equ    ROW_Z,          44
    .equ    ROW_R,          48
    .equ    ROW_L,          52
    .equ    ROW_B,          56
    .equ    ROW_T,          60
    .equ    FRAME_BYTES,    64

    .equ    FRAME_WIDTH,    352
    .equ    FRAME_HEIGHT,   224
    .equ    NEAR_PLANE,     80
    .equ    PER_STRETCH,    32
    .equ    CELLS_FROM,     8
    .equ    TONE_LAST,      11

! r0 and r1 go: this plane's row, for the rows' z
    .macro  ROW plane, slot
    mov.l   @(\plane + P_F,gbr), r0
    mul.l   r0, r12
    sts     macl, r1
    mov.l   @(\plane + P_O,gbr), r0
    add     r1, r0
    mov.l   r0, @(\slot,r15)
    .endm

! past the plane: on to the next sector
    .macro  PLANE plane, slot
    mov.l   @(\plane + P_E,gbr), r0
    mul.l   r0, r11
    mov.l   @(\slot,r15), r1
    sts     macl, r2
    add     r1, r2
    mov.l   @(\plane + P_PAST,gbr), r0
    cmp/gt  r0, r2
    bt      .Lnext
    .endm

! r2 x * 8 + a, r3 z * 8 + b into \to: o + (e * x + f * z) / 8
    .macro  CELL_VALUE oo, ee, ff, to
    mov.l   @(\ee,gbr), r0
    mul.l   r0, r2
    sts     macl, \to
    mov.l   @(\ff,gbr), r0
    mul.l   r0, r3
    sts     macl, r0
    add     r0, \to
    shar    \to
    shar    \to
    shar    \to
    mov.l   @(\oo,gbr), r0
    add     r0, \to
    .endm

! past the plane, the cell is out
    .macro  CELL_PLANE plane
    CELL_VALUE \plane+P_O, \plane+P_E, \plane+P_F, r1
    mov.l   @(\plane + P_PAST_CELL,gbr), r0
    cmp/gt  r0, r1
    bf      1f
    bra     .LcellNext
    nop
1:
    .endm

! du r4 and dv r5 from the middle \cc: into \to, view units
    .macro  PLACE_AXIS mu, mv, cc, to
    mov.l   @(\mu,gbr), r0
    muls.w  r0, r4
    sts     macl, \to
    mov.l   @(\mv,gbr), r0
    muls.w  r0, r5
    sts     macl, r0
    add     r0, \to
    shll2   \to
    shll2   \to
    mov.l   @(\cc,r15), r0
    add     r0, \to
    swap.w  \to, \to
    exts.w  \to, \to
    .endm

    .text
    .align 2
    .global _facesShimmersAsm
_facesShimmersAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    stc.l   gbr, @-r15
    add     #-FRAME_BYTES, r15
    ldc     r4, gbr
    mov     r5, r14
    mov     r6, r13
    mov     #-1, r8
.Lrun:
    tst     r13, r13
    bf      1f
    bra     .Lreturn
    mov     #1, r0
1:  add     #-1, r13
    mov.b   @r14+, r12
    extu.b  r12, r12
    mov.b   @r14+, r11
    extu.b  r11, r11
    mov.b   @r14+, r10
    extu.b  r10, r10
    mov.b   @r14+, r9
    extu.b  r9, r9
    cmp/eq  r8, r12
    bt      .Lsector
    mov     r12, r8
    ROW     S_OZ, ROW_Z
    ROW     S_R, ROW_R
    ROW     S_L, ROW_L
    ROW     S_B, ROW_B
    ROW     S_T, ROW_T
    bra     .Lsector
    nop

.Lnext:
    add     #1, r11
    dt      r10
    bf      .Lsector
    bra     .Lrun
    nop

.Lsector:
    mov.l   @(S_EZ,gbr), r0
    mul.l   r0, r11
    mov.l   @(ROW_Z,r15), r1
    sts     macl, r7
    add     r1, r7                  ! z
    mov.l   @(S_FAR_Z,gbr), r0
    cmp/gt  r0, r7
    bt      .Lnext
    mov.l   @(S_NEAR_Z,gbr), r0
    cmp/gt  r7, r0
    bt      .Lnext
    PLANE   S_R, ROW_R
    PLANE   S_L, ROW_L
    PLANE   S_B, ROW_B
    PLANE   S_T, ROW_T

    ! what it owes, by its depth; r3
    swap.w  r7, r0
    exts.w  r0, r0
    cmp/pz  r0
    bt      1f
    mov     #0, r0
1:  mov     #80, r1
    cmp/gt  r1, r0
    bf      2f
    mov     r1, r0
2:  shll2   r0
    mov     r0, r1
    mov.l   @(S_DEPTH,gbr), r0
    mov.l   @(r0,r1), r1
    mov.l   @(S_HEIGHT,gbr), r0
    dmuls.l r0, r1
    sts     mach, r2
    sts     macl, r3
    xtrct   r2, r3
    tst     r3, r3
    bf      3f
    bra     .Lnext
    nop
3:
    ! its bucket, and how dim
    mov.l   @(S_REACH,gbr), r0
    mov     r7, r2
    add     r0, r2
    shlr8   r2                      ! its far end, view units
    mov     r2, r0
    shlr2   r0
    shlr2   r0
    mov.l   .LfarBucket, r1
    cmp/gt  r1, r0
    bf      4f
    mov     r1, r0
4:  add     r0, r0
    mov.l   r0, @(F_DEPTH,r15)
    mov.l   .Lfog, r0
    sub     r0, r2
    cmp/pz  r2
    bt      5f
    mov     #0, r2
5:  shlr8   r2
    add     r9, r2
    shlr2   r2
    shlr    r2
    mov.l   r2, @(F_LEVEL,r15)

    ! its middle, its seed
    mov.l   @(S_EX,gbr), r0
    mul.l   r0, r11
    sts     macl, r5
    mov.l   @(S_FX,gbr), r0
    mul.l   r0, r12
    sts     macl, r0
    add     r0, r5
    mov.l   @(S_OX,gbr), r0
    add     r0, r5
    mov.l   @(S_EY,gbr), r0
    mul.l   r0, r11
    sts     macl, r6
    mov.l   @(S_FY,gbr), r0
    mul.l   r0, r12
    sts     macl, r0
    add     r0, r6
    mov.l   @(S_OY,gbr), r0
    add     r0, r6
    mov.l   .LturnX, r0
    mul.l   r0, r11
    sts     macl, r1
    mov.l   .LturnZ, r0
    mul.l   r0, r12
    sts     macl, r0
    add     r0, r1
    mov.l   @(S_TURN,gbr), r0
    add     r0, r1
    swap.w  r1, r2
    extu.w  r2, r2
    xor     r2, r1
    mov.l   .LmixA, r0
    mul.l   r0, r1
    sts     macl, r1
    swap.w  r1, r2
    extu.w  r2, r2
    xor     r2, r1

    mov     #CELLS_FROM, r0
    shll2   r0
    shll2   r0
    cmp/ge  r0, r3
    bt      .Lcells

    shll8   r5
    mov.l   r5, @(F_CX,r15)
    shll8   r6
    mov.l   r6, @(F_CY,r15)
    shll8   r7
    mov.l   r7, @(F_CZ,r15)
    mov     #0, r0
    bsr     .Lplace
    mov.l   r0, @(F_SPREAD,r15)
    tst     r0, r0
    bt      .LreturnFar
    bra     .Lnext
    nop
.LreturnFar:
    bra     .Lreturn
    mov     #0, r0

! near: a cell at a time
.Lcells:
    mov     r1, r0
    mov.l   r0, @(S_SEED,gbr)
    mov     #0, r0
    mov.l   r0, @(S_CELL,gbr)
.Lcell:
    ! a cell's middle: x * 8 + a, z * 8 + b, of -3, -1, 1, 3
    mov.l   @(S_CELL,gbr), r0
    mov     r0, r3
    and     #3, r0
    add     r0, r0
    add     #-3, r0
    mov     r11, r2
    shll2   r2
    add     r2, r2
    add     r0, r2
    shlr2   r3
    add     r3, r3
    add     #-3, r3
    mov     r12, r0
    shll2   r0
    add     r0, r0
    add     r0, r3
    CELL_VALUE S_OZ, S_EZ, S_FZ, r7
    mov.l   @(S_NEAR_CELL,gbr), r0
    cmp/gt  r7, r0
    bf      1f
    bra     .LcellNext
    nop
1:
    CELL_PLANE S_R
    CELL_PLANE S_L
    CELL_PLANE S_B
    CELL_PLANE S_T
    CELL_VALUE S_OX, S_EX, S_FX, r5
    CELL_VALUE S_OY, S_EY, S_FY, r6
    ! what it owes: a sixteenth of a sector at its depth
    swap.w  r7, r0
    exts.w  r0, r0
    cmp/pz  r0
    bt      2f
    mov     #0, r0
2:  mov     #80, r1
    cmp/gt  r1, r0
    bf      3f
    mov     r1, r0
3:  shll2   r0
    mov     r0, r1
    mov.l   @(S_DEPTH,gbr), r0
    mov.l   @(r0,r1), r1
    mov.l   @(S_HEIGHT,gbr), r0
    dmuls.l r0, r1
    sts     mach, r2
    sts     macl, r3
    xtrct   r2, r3
    shar    r3
    shar    r3
    shar    r3
    shar    r3
    shll8   r5
    mov.l   r5, @(F_CX,r15)
    shll8   r6
    mov.l   r6, @(F_CY,r15)
    shll8   r7
    mov.l   r7, @(F_CZ,r15)
    mov     #1, r0
    mov.l   r0, @(F_SPREAD,r15)
    mov.l   @(S_SEED,gbr), r0
    bsr     .Lplace
    mov     r0, r1
    tst     r0, r0
    bf      .LcellNext
    bra     .Lreturn
    mov     #0, r0
.LcellNext:
    mov.l   .LmixCell, r1
    mov.l   @(S_SEED,gbr), r0
    add     r1, r0
    mov.l   r0, @(S_SEED,gbr)
    mov.l   @(S_CELL,gbr), r0
    add     #1, r0
    mov.l   r0, @(S_CELL,gbr)
    cmp/eq  #16, r0
    bt      4f
    bra     .Lcell
    nop
4:  bra     .Lnext
    nop

.Lreturn:
    add     #FRAME_BYTES, r15
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
.LfarBucket:    .long 1280
.Lfog:          .long 0x1000
.LturnX:        .long 40503
.LturnZ:        .long 2654435761
.LmixA:         .long 0x2C1B3C6D
.LmixCell:      .long 0x51ED270B

! a stretch's shimmers: r1 its seed, r3 what it owes, 28.4; the frame's middle, depth, level, spread set.
! r0 back: 0 if the list ran out. r8 r9 r10 kept; r0 .. r7 not.
    .align 2
.Lplace:
    mov.l   r8, @(F_R8,r15)
    mov.l   r9, @(F_R9,r15)
    mov.l   r10, @(F_R10,r15)
    mov     r3, r0
    and     #15, r0
    mov     r0, r2                  ! its share of one more
    shlr2   r3
    shlr2   r3                      ! whole ones
    mov     #PER_STRETCH, r0
    cmp/ge  r0, r3
    bf      1f
    mov     r0, r3
    mov     #0, r2
1:  mov.l   r2, @(F_PART,r15)
    tst     r2, r2
    bf      2f
    add     #-1, r3
2:  cmp/pz  r3
    bt      3f
    bra     .Lplaced
    mov     #1, r0
3:  mov     r3, r8
    mov.l   .LmixK, r0
    mul.l   r0, r3
    sts     macl, r0
    add     r1, r0
    mov.l   r0, @(F_SEED,r15)

.Lshimmer:
    ! its clock
    mov.l   @(F_SEED,r15), r1
    mov.l   .LmixK, r0
    mov     r1, r2
    sub     r0, r2
    mov.l   r2, @(F_SEED,r15)
    swap.w  r1, r2
    extu.w  r2, r2
    xor     r2, r1
    mov.l   .LmixB, r0
    mul.l   r0, r1
    sts     macl, r1
    swap.w  r1, r2
    extu.w  r2, r2
    shlr8   r2
    shlr2   r2
    shlr2   r2                      ! clock >> 28
    mov.l   @(S_FRAME,gbr), r0
    add     r0, r2                  ! now
    ! this life of it
    mov     r2, r3
    shlr2   r3
    shlr    r3
    mov.l   .LmixC, r0
    mul.l   r0, r3
    sts     macl, r3
    add     r1, r3
    swap.w  r3, r1
    extu.w  r1, r1
    xor     r1, r3
    mov.l   .LmixD, r0
    mul.l   r0, r3
    sts     macl, r3
    swap.w  r3, r1
    extu.w  r1, r1
    xor     r1, r3
    ! the stretch's share of one more: this life, is it lit?
    mov.l   @(F_PART,r15), r0
    tst     r0, r0
    bt      4f
    mov     r0, r4
    mov     r3, r0
    and     #15, r0
    cmp/ge  r4, r0
    mov     #0, r1
    bf/s    4f
    mov.l   r1, @(F_PART,r15)
    bra     .LshimmerNext
    nop
4:
    ! where: r4 r5 from the stretch's middle
    swap.w  r3, r4
    extu.b  r4, r5
    shlr8   r4
    extu.b  r4, r4
    add     #-128, r4
    add     #-128, r5
    mov.l   @(F_SPREAD,r15), r0
    tst     r0, r0
    bt      5f
    shar    r4
    shar    r4
    shar    r5
    shar    r5
5:  PLACE_AXIS S_M6, S_M8, F_CZ, r6
    mov     #NEAR_PLANE, r0
    cmp/ge  r0, r6
    bt      6f
    bra     .LshimmerNext
    nop
6:  shlr2   r6
    shlr2   r6
    mov.w   .LdivLast, r0
    cmp/gt  r0, r6
    bf      7f
    mov     r0, r6
7:  add     r6, r6
    mov.l   @(S_DIV,gbr), r0
    mov.w   @(r0,r6), r6            ! dd
    PLACE_AXIS S_M0, S_M2, F_CX, r7
    muls.w  r6, r7
    sts     macl, r7
    shll2   r7
    shll2   r7
    swap.w  r7, r7
    exts.w  r7, r7                  ! sx
    mov.l   @(S_X0,gbr), r0
    cmp/ge  r0, r7
    bf      .LoffShimmer
    mov.l   @(S_X1,gbr), r0
    cmp/gt  r0, r7
    bt      .LoffShimmer
    PLACE_AXIS S_M3, S_M5, F_CY, r1
    muls.w  r6, r1
    sts     macl, r1
    shll2   r1
    shll2   r1
    swap.w  r1, r1
    exts.w  r1, r1                  ! sy
    mov.l   @(S_Y0,gbr), r0
    cmp/ge  r0, r1
    bf      .LoffShimmer
    mov.l   @(S_Y1,gbr), r0
    cmp/gt  r0, r1
    bf      8f
.LoffShimmer:
    bra     .LshimmerNext
    nop
8:
    ! the command
    mov.l   @(S_LIST,gbr), r0
    mov     r0, r4
    mov.l   @r4, r5
    mov.l   @(S_STOP,gbr), r0
    cmp/hs  r0, r5
    bf      9f
    bra     .Lplaced
    mov     #0, r0
9:
    ! by its age: mesh in the top bit, tones dimmer below
    mov     r2, r0
    and     #7, r0
    mov     r0, r10
    mov.l   @(S_AGES,gbr), r0
    mov.b   @(r0,r10), r10
    extu.b  r10, r10
    mov     r10, r0
    and     #0x7F, r0
    mov     r0, r2                  ! dimmer by
    mov     r10, r0
    and     #0x80, r0
    add     r0, r0
    mov     r0, r10                 ! PMOD_MESH or 0
    mov.l   @(F_LEVEL,r15), r0
    add     r0, r2
    mov     #TONE_LAST, r0
    cmp/gt  r0, r2
    bf      1f
    mov     r0, r2
1:
    ! the piece
    mov     r3, r0
    shlr8   r0
    and     #63, r0
    mov     r0, r9
    mov.l   @(S_BAG,gbr), r0
    mov.b   @(r0,r9), r9
    extu.b  r9, r9
    mov.l   @(S_PIECES,gbr), r0
    add     r0, r9
    add     #FRAME_WIDTH >> 2, r7
    add     #FRAME_WIDTH >> 2, r7
    add     #FRAME_HEIGHT >> 1, r1
    mov.l   @(S_SPRITE_DD,gbr), r0
    cmp/ge  r0, r6
    bf      .Lline

    ! art
    shll2   r2
    mov.l   @(S_TABLES,gbr), r0
    add     r0, r2
    mov.w   .LpmodArt, r0
    or      r0, r10
    shll16  r10
    or      r2, r10
    mov.l   r10, @(4,r5)
    mov.l   @r9, r0
    mov.l   r0, @(8,r5)
    mov.b   @(A_HALF_W,r9), r0
    sub     r0, r7
    mov.b   @(A_HALF_H,r9), r0
    sub     r0, r1
    shll16  r7
    extu.w  r1, r1
    or      r1, r7
    mov.l   r7, @(12,r5)
    mov.l   .Lw0Art, r2
    bra     .Llink
    nop

.Lline:
    mov     r6, r3
    shlr2   r3
    shlr2   r3
    shlr2   r3                      ! the texture's pixel, in quarters of the frame's
    mov.b   @(A_W,r9), r0
    mulu.w  r0, r3
    sts     macl, r3
    shlr2   r3
    shlr    r3                      ! half of it
    tst     r10, r10
    bt      1f
    shlr    r3
1:  add     r2, r2
    mov.l   @(S_COLOURS,gbr), r0
    mov.w   @(r0,r2), r2
    extu.w  r2, r2
    mov.l   .LpmodLine, r0
    or      r0, r2
    mov.l   r2, @(4,r5)
    extu.w  r1, r1
    mov     r7, r0
    sub     r3, r0
    shll16  r0
    or      r1, r0
    mov.l   r0, @(12,r5)
    mov     r7, r0
    add     r3, r0
    shll16  r0
    or      r1, r0
    mov.l   r0, @(16,r5)
    mov.l   .Lw0Line, r2

.Llink:
    ! faces.cpp linked: r2 its first word
    mov.l   @(F_DEPTH,r15), r9
    mov.l   @(20,r4), r0            ! heads
    mov.w   @(r0,r9), r3
    extu.w  r3, r3
    or      r3, r2
    mov.l   r2, @r5
    mov.l   @(12,r4), r1            ! link
    mov.w   r1, @(r0,r9)
    tst     r3, r3
    bf      2f
    mov.l   @(24,r4), r0            ! tails
    mov.w   r1, @(r0,r9)
2:  add     #4, r1                  ! sizeof(Command) >> 3
    mov.l   r1, @(12,r4)
    mov.l   @(16,r4), r0            ! grdLink
    mov.l   r0, @(28,r5)
    mov.l   .LgrdStep, r1
    add     r1, r0
    mov.l   r0, @(16,r4)
    add     #32, r5
    mov.l   r5, @r4
    mov.l   @(8,r4), r0             ! grd
    add     #8, r0
    mov.l   r0, @(8,r4)

.LshimmerNext:
    add     #-1, r8
    cmp/pz  r8
    bf      3f
    bra     .Lshimmer
    nop
3:  mov     #1, r0
.Lplaced:
    mov.l   @(F_R8,r15), r8
    mov.l   @(F_R9,r15), r9
    rts
    mov.l   @(F_R10,r15), r10

    .align 1
.LdivLast:      .word 1280
.LpmodArt:      .word 0x0088    ! PMOD_ECD_OFF | PMOD_LUT16
    .align 2
.LmixB:         .long 0x2C1B3C6D
.LmixC:         .long 0x85EBCA6B
.LmixD:         .long 0x27D4EB2F
.LmixK:         .long 0x9E3779B1
.LpmodLine:     .long 0x00C00000    ! PMOD_NO_ECD_SPD
.Lw0Art:        .long 0x10000000    ! CMD_NORMAL_SPRITE | CMD_JUMP
.Lw0Line:       .long 0x10060000    ! CMD_LINE | CMD_JUMP
.LgrdStep:      .long 0x00010000

    .endif

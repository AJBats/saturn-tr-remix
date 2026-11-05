! Water's shimmers (faces.h ShimmerSheet; faces.cpp sets it up).
!
! A shimmer stays where it is on the water for its 8 frames, so it's worked out once, as it's born, and kept;
! after that only its place on the frame is new.
!
! int32 facesShimmersBornAsm(ShimmerSheet* sheet, const uint8_t* sectors, int32 count, int32 first)
!     Those of a sheet's sectors that start on one frame in 8, of which this frame's slot is first, give their
!     new shimmers. sectors: count of (z, x, shade) bytes. Returns 0 if there's no more room for them.
!
!     What's tested of a sector is linear in its x and z: o + e * x + f * z. A side's value is how far past it
!     the sector's middle lies; past by more than half the sector's reach along it, the whole sector is.
!
! int32 facesShimmersDrawAsm(ShimmerSheet* sheet, const void* lives, const uint8_t* runs, const uint32_t* w1)
!     A generation, onto the frame and into the list. lives: 6 bytes each, x, z, level, piece. runs: S_RUNS of
!     (shimmers, room, view's offset as a halfword), the rooms they lie in, in order. w1: the first word pair by
!     level, art's then a line's. Returns 0 if the list ran out.
!
! A view (ShimmerView) is the view's row of world x and z twice over, and the sum for that sheet's height, for
! Z, X and Y. The row by a shimmer's x, z (8 to a unit) comes to 4096 to a view unit, so z / 16 is its high
! halfword, and a coordinate on the frame the high longword of the view's by the divide table's value << 8.
!
! GBR the sheet. A value loaded and used in the next instruction stalls a cycle, so loads go where the next
! instruction can't use them, and what's the same for a whole run stays in a register.

    .ifndef RENDER_LAB

    .equ    S_LIST,         0
    .equ    S_STOP,         4       ! the list's commands stop here
    .equ    S_DIV,          8
    .equ    S_DEPTH,        12
    .equ    S_BAG,          16
    .equ    S_PIECES,       20
    .equ    S_VIEWS,        24
    .equ    S_SEEN,         28      ! by room: the frame's low byte if it's in this frame's walk
    .equ    S_FRAME,        32
    .equ    S_SPRITE_DD,    36      ! from this dd, art; below it, a line
    .equ    S_LIMIT,        40      ! z / 16 less 5, and a frame's coordinate: past this as unsigned, it's out
    .equ    S_RUNS,         44
    .equ    S_LIVES,        48      ! the next shimmer born goes here
    .equ    S_LIVES_END,    52
    .equ    S_TURN,         56
    .equ    S_VIEW,         60      ! the sheet's
    .equ    S_OWED,         64
    .equ    S_ONE_Z,        68
    .equ    S_WX,           72      ! its first sector's middle in the world, 8 to a unit
    .equ    S_WZ,           76
    .equ    S_X0,           80      ! the room's clip, on the frame
    .equ    S_X1,           84
    .equ    S_Y0,           88
    .equ    S_Y1,           92
    .equ    S_OZ,           96
    .equ    S_EZ,           100
    .equ    S_FZ,           104
    .equ    S_NEAR_Z,       108
    .equ    S_REACH_Z,      112
    .equ    S_LINE_SHIFT,   124     ! a line is half as long, this generation
    .equ    S_R,            128     ! right: o, e, f, past
    .equ    S_L,            144     ! left
    .equ    S_B,            160     ! bottom
    .equ    S_T,            176     ! top

    .equ    P_O,            0
    .equ    P_E,            4
    .equ    P_F,            8
    .equ    P_PAST,         12

    ! ShimmerPiece
    .equ    A_HALF_W,       4
    .equ    A_HALF_H,       5
    .equ    A_W,            6

    ! a shimmer
    .equ    L_LEVEL,        4
    .equ    L_PIECE,        5
    .equ    LIFE_BYTES,     6

    ! the frame of facesShimmersBornAsm
    .equ    F_R9,           0
    .equ    F_R10,          4
    .equ    F_WHOLE,        8       ! whole shimmers the sector owes
    .equ    F_PART,         12      ! ... and its share of one more
    .equ    F_LEVEL,        16
    .equ    F_LIFE,         20
    .equ    F_HASH,         24
    .equ    F_FIRST,        28      ! its slot whose frame this is, of the first 8
    .equ    FRAME_BYTES,    32

    .equ    BUCKET_ON,      45      ! from a shimmer to its bucket, z / 16: about the sector's reach
    .equ    MOST_OWED,      64
    .equ    LEVEL_LAST,     11
    .equ    LINE_W1,        48      ! in the w1 table, a line's after art's

! A shimmer's place. \rec its x, z; r3 its view; r7 S_LIMIT. Out: r4 and r1 on the frame, r5 dd, r8 z / 16.
! r0, r2, r6 go. Behind or too near or far, or left of or above the frame or far past it: to \out.
    .macro  PROJECT rec, out
    mov     r3, r2
    mov     \rec, r1
    clrmac
    mac.w   @r1+, @r2+
    mac.w   @r1+, @r2+
    mov.l   @r2+, r4
    sts     macl, r0
    add     r0, r4
    swap.w  r4, r8
    extu.w  r8, r8
    mov     r8, r0
    add     #-5, r0
    cmp/hi  r7, r0
    bt      \out
    mov.l   @(S_DIV,gbr), r0
    mov     r8, r1
    add     r1, r1
    mov.w   @(r0,r1), r5
    mov     \rec, r1
    clrmac
    mac.w   @r1+, @r2+
    mac.w   @r1+, @r2+
    mov     r5, r6
    shll8   r6
    mov.l   @r2+, r4
    sts     macl, r0
    add     r0, r4
    dmuls.l r6, r4
    mov     \rec, r1
    sts     mach, r4
    add     #88, r4
    add     #88, r4
    cmp/hi  r7, r4
    bt      \out
    clrmac
    mac.w   @r1+, @r2+
    mac.w   @r1+, @r2+
    mov.l   @r2+, r0
    sts     macl, r1
    add     r1, r0
    dmuls.l r6, r0
    sts     mach, r1
    add     #112, r1
    cmp/hi  r7, r1
    bt      \out
    .endm

! past the side: on to the next sector
    .macro  SIDE side
    mov.l   @(\side + P_E,gbr), r0
    mul.l   r0, r11
    mov.l   @(\side + P_F,gbr), r0
    sts     macl, r2
    mul.l   r0, r12
    mov.l   @(\side + P_O,gbr), r0
    add     r0, r2
    sts     macl, r0
    add     r0, r2
    mov.l   @(\side + P_PAST,gbr), r0
    cmp/gt  r0, r2
    bt      .Lnext
    .endm

    .text
    .align 4
    .global _facesShimmersBornAsm
_facesShimmersBornAsm:
    ! Through the sectors: r14 their bytes, r13 how many left, r12 z, r11 x, r9 shade
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
    mov.l   r7, @(F_FIRST,r15)
    bra     .Lsector
    nop

.Lnext:
    dt      r13
    bf      .Lsector
    bra     .LbornOut
    mov     #1, r0

    .align 2
.Lsector:
    mov.b   @r14+, r12
    mov.b   @r14+, r11
    mov.b   @r14+, r9
    extu.b  r12, r12
    extu.b  r11, r11
    extu.b  r9, r9
    ! z
    mov.l   @(S_EZ,gbr), r0
    mul.l   r0, r11
    mov.l   @(S_FZ,gbr), r0
    sts     macl, r7
    mul.l   r0, r12
    mov.l   @(S_OZ,gbr), r0
    add     r0, r7
    sts     macl, r0
    add     r0, r7
    mov.l   @(S_ONE_Z,gbr), r0  ! past this it doesn't owe as many as this slot
    cmp/gt  r0, r7
    bt      .Lnext
    mov.l   @(S_NEAR_Z,gbr), r0
    cmp/gt  r7, r0
    bt      .Lnext
    SIDE    S_R
    SIDE    S_L
    SIDE    S_B
    SIDE    S_T
    mov.l   @(F_FIRST,r15), r6

    ! what it owes, by its depth: r3
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
    mov.l   @(S_OWED,gbr), r0
    dmuls.l r0, r1
    sts     mach, r2
    sts     macl, r3
    xtrct   r2, r3
    mov     r3, r0
    and     #15, r0
    mov     r0, r2                  ! its share of one more
    shlr2   r3
    shlr2   r3                      ! whole ones
    mov     #MOST_OWED, r0
    cmp/ge  r0, r3
    bf      4f
    mov     r0, r3
    mov     #0, r2
4:  ! is this slot, or one 8 on, among them?
    cmp/gt  r3, r6
    bf      5f
    bra     .Lnext
    nop
5:  cmp/eq  r3, r6
    bf      6f
    tst     r2, r2
    bf      6f
    bra     .Lnext
    nop
6:  mov.l   r3, @(F_WHOLE,r15)
    mov.l   r2, @(F_PART,r15)

    ! how dim it is, with the room's shade and the fog
    mov.l   @(S_REACH_Z,gbr), r0
    mov     r7, r2
    add     r0, r2
    shlr8   r2
    mov.l   .Lfog, r0
    sub     r0, r2
    cmp/pz  r2
    bt      7f
    mov     #0, r2
7:  shlr8   r2
    add     r9, r2
    shlr2   r2
    shlr    r2
    mov     #LEVEL_LAST, r0
    cmp/gt  r0, r2
    bf      8f
    mov     r0, r2
8:  mov.l   r2, @(F_LEVEL,r15)

    ! its first shimmer's seed
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
    xor     r2, r1                  ! the sector's
    mov.l   @(S_FRAME,gbr), r0
    shlr2   r0
    shlr    r0
    mov.l   .LmixC, r2
    mul.l   r2, r0                  ! this time round
    sts     macl, r0
    add     r0, r1
    mov.l   .LmixK, r0
    mul.l   r0, r6
    sts     macl, r0
    add     r1, r0
    mov.l   r0, @(F_HASH,r15)

    mov.l   r9, @(F_R9,r15)
    mov.l   r10, @(F_R10,r15)
    mov     r6, r10                 ! the slot
    mov.l   @(S_LIVES,gbr), r0
    mov     r0, r9                  ! where it goes
    mov.l   @(S_VIEW,gbr), r0
    mov     r0, r3
    mov.l   @(S_LIMIT,gbr), r0
    mov     r0, r7
    mov.l   @(S_LIVES_END,gbr), r0
    cmp/hs  r0, r9
    bf      .Lbirth
    bra     .LbornFull
    nop

! on to the same frame's slot 8 further on, if the sector owes that many
.LbirthNext:
    add     #8, r10
    mov.l   @(F_HASH,r15), r1
    mov.l   .LmixK8, r0
    add     r0, r1
    mov.l   r1, @(F_HASH,r15)
    mov.l   @(F_WHOLE,r15), r0
    cmp/gt  r0, r10
    bt      .LbirthsDone
    cmp/eq  r0, r10
    bf      .Lbirth
    mov.l   @(F_PART,r15), r0
    tst     r0, r0
    bf      .Lbirth
.LbirthsDone:
    mov     r9, r0
    mov.l   r0, @(S_LIVES,gbr)
    mov.l   @(F_R9,r15), r9
    mov.l   @(F_R10,r15), r10
    bra     .Lnext
    nop

.Lbirth:
    mov.l   @(F_HASH,r15), r2
    swap.w  r2, r0
    extu.w  r0, r0
    xor     r0, r2
    mov.l   .LmixB, r0
    mul.l   r0, r2
    sts     macl, r2
    swap.w  r2, r0
    extu.w  r0, r0
    xor     r0, r2                  ! this life of it
    mov.l   r2, @(F_LIFE,r15)
    ! the sector's share of one more: this life, is it lit?
    mov.l   @(F_WHOLE,r15), r0
    cmp/eq  r0, r10
    bf      1f
    mov.l   @(F_PART,r15), r1
    mov     r2, r0
    and     #15, r0
    cmp/ge  r1, r0
    bt      .LbirthNext
1:  ! where: from the sector's middle
    swap.w  r2, r4
    extu.b  r4, r5
    shlr8   r4
    extu.b  r4, r4
    add     #-128, r4
    add     #-128, r5
    shar    r4
    shar    r5
    mov     r11, r1
    shll8   r1
    shlr    r1                      ! a sector is 128
    add     r4, r1
    mov.l   @(S_WX,gbr), r0
    add     r1, r0
    mov.w   r0, @r9
    mov     r12, r1
    shll8   r1
    shlr    r1
    add     r5, r1
    mov.l   @(S_WZ,gbr), r0
    add     r1, r0
    mov.w   r0, @(2,r9)
    PROJECT r9, .LbirthNear
    ! inside the room's clip, it's kept
    mov.l   @(S_X0,gbr), r0
    cmp/ge  r0, r4
    bf      .LbirthNear
    mov.l   @(S_X1,gbr), r0
    cmp/gt  r0, r4
    bt      .LbirthNear
    mov.l   @(S_Y0,gbr), r0
    cmp/ge  r0, r1
    bf      .LbirthNear
    mov.l   @(S_Y1,gbr), r0
    cmp/gt  r0, r1
    bt      .LbirthNear
    mov.l   @(F_LEVEL,r15), r0
    mov.b   r0, @(L_LEVEL,r9)
    mov.l   @(F_LIFE,r15), r0
    shlr8   r0
    and     #63, r0
    mov     r0, r1
    mov.l   @(S_BAG,gbr), r0
    mov.b   @(r0,r1), r0
    mov.b   r0, @(L_PIECE,r9)
    add     #LIFE_BYTES, r9
    mov.l   @(S_LIVES_END,gbr), r0
    cmp/hs  r0, r9
    bf      .LbirthNear
.LbornFull:
    mov     r9, r0
    mov.l   r0, @(S_LIVES,gbr)
    mov     #0, r0
.LbornOut:
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

.LbirthNear:
    bra     .LbirthNext
    nop

    .align 2
.Lfog:          .long 0x1000
.LturnX:        .long 40503
.LturnZ:        .long 2654435761
.LmixA:         .long 0x2C1B3C6D
.LmixB:         .long 0x2C1B3C6D
.LmixC:         .long 0x85EBCA6B
.LmixK:         .long 0x9E3779B1
.LmixK8:        .long 0xF1BBCD88    ! 0x9E3779B1 * 8

! ---

    .equ    D_RUN,          0       ! the frame of facesShimmersDrawAsm: the next run
    .equ    D_RUNS,         4       ! how many are left
    .equ    D_FIRST,        8       ! the first command
    .equ    D_RESULT,       12
    .equ    D_BYTES,        16

    .align 4
    .global _facesShimmersDrawAsm
_facesShimmersDrawAsm:
    ! r14 the shimmer, r13 how many left in the run, r12 the command, r11 w1, r10 heads, r9 the link,
    ! r7 S_LIMIT, r3 the run's view
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    stc.l   gbr, @-r15
    add     #-D_BYTES, r15
    ldc     r4, gbr
    mov     r5, r14
    mov.l   r6, @(D_RUN,r15)
    mov     r7, r11
    mov.l   @(S_LIMIT,gbr), r0
    mov     r0, r7
    mov.l   @(S_RUNS,gbr), r0
    mov.l   r0, @(D_RUNS,r15)
    mov.l   @(S_LIST,gbr), r0
    mov.l   @r0, r12
    mov.l   @(20,r0), r10
    mov.l   @(12,r0), r9
    mov.l   r12, @(D_FIRST,r15)
    mov     #1, r0
    mov.l   r0, @(D_RESULT,r15)
    bra     .LdrawRun
    nop

    ! a run of shimmers of a room not in this frame's walk: passed over
.LrunUnseen:
    mov     r13, r0
    shll    r0
    add     r13, r0
    add     r0, r0                  ! LIFE_BYTES
    add     r0, r14
.LdrawRun:
    mov.l   @(D_RUNS,r15), r0
    tst     r0, r0
    bf      1f
    bra     .LdrawDone
    nop
1:  add     #-1, r0
    mov.l   r0, @(D_RUNS,r15)
    mov.l   @(D_RUN,r15), r1
    mov.b   @r1+, r13
    extu.b  r13, r13
    mov.b   @r1+, r2
    mov.w   @r1+, r3
    mov.l   r1, @(D_RUN,r15)
    extu.w  r3, r3
    mov.l   @(S_VIEWS,gbr), r0
    add     r0, r3
    extu.b  r2, r2
    mov.l   @(S_SEEN,gbr), r0
    mov.b   @(r0,r2), r1
    mov.l   @(S_FRAME,gbr), r0
    extu.b  r1, r1
    extu.b  r0, r0
    cmp/eq  r0, r1
    bf      .LrunUnseen
    ! as many as the list has room for
    mov.l   @(S_STOP,gbr), r0
    sub     r12, r0
    cmp/pz  r0
    bt      2f
    mov     #0, r0                  ! past it already
2:  shlr2   r0
    shlr2   r0
    shlr    r0
    cmp/hs  r13, r0
    bt      .LdrawGo
    mov     r0, r13
    mov     #0, r0
    mov.l   r0, @(D_RESULT,r15)
    mov     r0, r1
    mov.l   r1, @(D_RUNS,r15)
.LdrawGo:
    tst     r13, r13
    bf      .Ldraw
    bra     .LdrawRun
    nop

    .align 2
.Lskip:
    dt      r13
    bf/s    .Ldraw
    add     #LIFE_BYTES, r14
    bra     .LdrawRun
    nop

    .align 4
.Ldraw:
    PROJECT r14, .Lskip
    mov.b   @(L_LEVEL,r14), r0
    shll2   r0
    mov     r0, r6
    mov.b   @(L_PIECE,r14), r0
    extu.b  r0, r2
    mov.l   @(S_PIECES,gbr), r0
    add     r0, r2                  ! the piece
    mov.l   @(S_SPRITE_DD,gbr), r0
    cmp/ge  r0, r5
    bf      .Lline

    ! art
    mov     r6, r0
    mov.l   @(r0,r11), r6
    mov.l   @r2, r0
    mov.l   r6, @(4,r12)
    mov.l   r0, @(8,r12)
    mov.b   @(A_HALF_W,r2), r0
    sub     r0, r4
    mov.b   @(A_HALF_H,r2), r0
    sub     r0, r1
    shll16  r4
    extu.w  r1, r1
    or      r1, r4
    mov.l   r4, @(12,r12)
    mov.l   .Lw0Art, r4
.Llink:
    ! faces.cpp linked: r4 its first word, r8 z / 16. S_LIMIT keeps z / 16 and BUCKET_ON within the buckets.
    add     #BUCKET_ON, r8
    add     r8, r8
    mov     r8, r0
    mov.w   @(r0,r10), r1
    mov.w   r9, @(r0,r10)
    extu.w  r1, r1
    or      r1, r4
    mov.l   r4, @r12
    tst     r1, r1
    bf      4f
    mov.l   @(S_LIST,gbr), r0
    mov.l   @(24,r0), r1            ! tails
    mov     r8, r0
    mov.w   r9, @(r0,r1)
4:  add     #4, r9                  ! sizeof(Command) >> 3
    dt      r13
    add     #32, r12
    bf/s    .Ldraw
    add     #LIFE_BYTES, r14
    bra     .LdrawRun
    nop

    .align 2
.Lline:
    mov     #LINE_W1, r0
    add     r6, r0
    mov.l   @(r0,r11), r6
    mov.b   @(A_W,r2), r0
    mov.l   r6, @(4,r12)
    shlr2   r5
    shlr2   r5
    shlr2   r5                      ! the texture's pixel, in quarters of the frame's
    mulu.w  r0, r5
    mov.l   @(S_LINE_SHIFT,gbr), r0
    sts     macl, r5
    shlr2   r5
    shlr    r5                      ! half of it
    tst     r0, r0
    bt      2f
    shlr    r5
2:  extu.w  r1, r1
    mov     r4, r0
    sub     r5, r0
    shll16  r0
    or      r1, r0
    mov.l   r0, @(12,r12)
    add     r5, r4
    shll16  r4
    or      r1, r4
    mov.l   r4, @(16,r12)
    mov.l   .Lw0Line, r4
    bra     .Llink
    nop

.LdrawDone:
    ! the list as this leaves it
    mov.l   @(D_FIRST,r15), r1
    mov.l   @(S_LIST,gbr), r0
    mov     r0, r4
    mov.l   r12, @r4
    mov.l   r9, @(12,r4)
    mov     r12, r2
    sub     r1, r2
    shlr2   r2                      ! 8 bytes of grd for each 32 of commands
    mov.l   @(8,r4), r0
    add     r2, r0
    mov.l   r0, @(8,r4)
    shll2   r2
    shll    r2                      ! ... and 1 << 16 of grdLink
    shll8   r2
    shll2   r2
    shll    r2
    mov.l   @(16,r4), r0
    add     r2, r0
    mov.l   r0, @(16,r4)
    mov.l   @(D_RESULT,r15), r0
    add     #D_BYTES, r15
    ldc.l   @r15+, gbr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    .align 2
.Lw0Art:        .long 0x10000000    ! CMD_NORMAL_SPRITE | CMD_JUMP
.Lw0Line:       .long 0x10060000    ! CMD_LINE | CMD_JUMP

    .endif

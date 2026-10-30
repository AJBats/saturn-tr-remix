! Stage 1 the new way (rooms.h): the rooms seen, each exactly as the game
! sees them: openlara room.h's portal walk (getVisibleRooms, addVisibleRoom,
! checkPortal), then draw.h queueRoom's matrix for each room listed.
!
! void roomsSeenAsm(RoomsContext* ctx)
!
! A room's walk matrix is the view's rotation, its translation the view's
! rows times the room's origin from the camera (matrixTranslateAbs): no
! stack, the translation carried down the walk in r12 r13 r14. A portal's
! corner is the rotation times it plus that translation (DP43), its >> 14
! and the projection's >> 12 the high longword of a product by 2^18 and
! 2^20 (DMULS.L), exact as C's for any longword; the divide table's step
! never needs its clamp (a corner is held to the far plane first).
!
! RoomsContext (rooms.h): +0 level (RoomLevel, 16 bytes each), +4 portals
! (32 each: room, normal, 4 corners), +8 divTable, +12 camera, +24 view
! (Matrix), +28 clip (per room x0 y0 x1 y1), +32 visible (a byte a room),
! +36 list (uint16), +40 listed, +44 seen (RoomSeen, 68 each), +48 viewport,
! +64 the camera's room, +68 voids, +72 the list's end (the walk's own),
! +76 2^18, +80 2^20, +84 VIEW_MAX_F (constants out of a pool's reach); for
! the jobs +88 each room's RoomGeometry, +92 RGEO, +96 gVertices, +100 the
! jobs (FaceRoom), +104 their count, +108 RoomCaustics, +112 the caustics'
! table (32 int16).

    .equ    R_LEVEL,        0
    .equ    R_PORTALS,      4
    .equ    R_DIV,          8
    .equ    R_CAMERA,       12
    .equ    R_VIEW,         24
    .equ    R_CLIP,         28
    .equ    R_VISIBLE,      32
    .equ    R_LIST,         36
    .equ    R_LISTED,       40
    .equ    R_SEEN,         44
    .equ    R_VIEWPORT,     48
    .equ    R_CAMERA_ROOM,  64
    .equ    R_VOIDS,        68
    .equ    R_LIST_END,     72
    .equ    R_K18,          76
    .equ    R_K20,          80
    .equ    R_VIEW_MAX,     84
    .equ    R_GEOMETRY,     88
    .equ    R_RGEO,         92
    .equ    R_VERTICES,     96
    .equ    R_JOBS,         100
    .equ    R_JOB_COUNT,    104
    .equ    R_CAUSTICS,     108
    .equ    R_TABLE,        112

    .equ    L_X,            0       ! RoomLevel
    .equ    L_Z,            2
    .equ    L_TOP,          4
    .equ    L_PORTALS,      7
    .equ    L_FIRST,        12
    .equ    PORTAL,         32
    .equ    P_NORMAL,       2
    .equ    P_CORNERS,      8
    .equ    SEEN,           68

    .text
    .align  2

! The checks' frame: the corners' view places (x y z each) from +0, the
! window x0 y0 x1 y1 from +48.
    .equ    F_PV,           0
    .equ    F_X0,           48
    .equ    F_Y0,           52
    .equ    F_X1,           56
    .equ    F_Y1,           60
    .equ    FRAME,          64

! A corner: its view place, into the frame; projected, the window grown to
! it (r10 the corners behind the camera, r11 those past the far plane;
! r6 the view's rotation); r0 .. r5, r7
    .macro  CORNER i
    mov.w   @(P_CORNERS+6*\i,r9), r0
    mov     r0, r1
    mov.w   @(P_CORNERS+6*\i+2,r9), r0
    mov     r0, r2
    mov.w   @(P_CORNERS+6*\i+4,r9), r0
    mov     r0, r3
    .irp    row, 0, 1, 2
    mov.l   @(\row*16,r6), r0
    mul.l   r0, r1
    mov.l   @(\row*16+4,r6), r0
    sts     macl, r5
    mul.l   r0, r2
    mov.l   @(\row*16+8,r6), r0
    sts     macl, r4
    mul.l   r0, r3
    add     r4, r5
    sts     macl, r4
    add     r4, r5
    .if \row == 0
    add     r12, r5
    .elseif \row == 1
    add     r13, r5
    .else
    add     r14, r5
    .endif
    mov.l   r5, @(F_PV+12*\i+4*\row,r15)
    .endr
    ! r5 the corner's z: behind the camera, or held to the far plane
    cmp/pl  r5
    bt      1f
    bra     9f
    add     #1, r10
1:  mov.l   @(R_VIEW_MAX,gbr), r0
    cmp/gt  r0, r5
    bf      2f
    mov     r0, r5
    add     #1, r11
2:  mov.l   @(R_K18,gbr), r0
    mov     r0, r4
    dmuls.l r5, r4
    sts     mach, r3                ! z >> 14
    mov.l   @(F_PV+12*\i,r15), r1
    dmuls.l r1, r4
    sts     mach, r1                ! x >> 14
    mov.l   @(F_PV+12*\i+4,r15), r2
    dmuls.l r2, r4
    sts     mach, r2                ! y >> 14
    ! PERSPECTIVE: d = divTable[z >> 4], x = x d >> 12, y = y d >> 12
    shlr2   r3
    shlr2   r3
    shll    r3
    mov.l   @(R_DIV,gbr), r0
    add     r0, r3
    mov.w   @r3, r3
    extu.w  r3, r3
    mov.l   @(R_K20,gbr), r0
    mov     r0, r4
    mul.l   r3, r1
    sts     macl, r1
    dmuls.l r1, r4
    sts     mach, r1
    mul.l   r3, r2
    sts     macl, r2
    dmuls.l r2, r4
    sts     mach, r2
    mov.l   @(F_X0,r15), r0
    cmp/gt  r1, r0
    bf      3f
    mov.l   r1, @(F_X0,r15)
3:  mov.l   @(F_X1,r15), r0
    cmp/gt  r0, r1
    bf      4f
    mov.l   r1, @(F_X1,r15)
4:  mov.l   @(F_Y0,r15), r0
    cmp/gt  r2, r0
    bf      5f
    mov.l   r2, @(F_Y0,r15)
5:  mov.l   @(F_Y1,r15), r0
    cmp/gt  r0, r2
    bf      9f
    mov.l   r2, @(F_Y1,r15)
9:
    .endm

! An edge from corner a to corner b crossing the near plane: the window
! opened to the frame's edges its ends lie past (x and y alike); r0 .. r3
    .macro  EDGE a, b
    mov.l   @(F_PV+12*\a+8,r15), r0
    mov.l   @(F_PV+12*\b+8,r15), r1
    xor     r1, r0
    cmp/pz  r0
    bt      9f
    EDGEAXIS \a, \b, 0, F_X0, F_X1, 352
    EDGEAXIS \a, \b, 4, F_Y0, F_Y1, 224
9:
    .endm

! r0 the frame's edge: its width (352) or height (224)
    .macro  EDGEAT edge
    .if \edge == 352
    mov     #88, r0
    shll2   r0
    .else
    mov     #112, r0
    shll    r0
    .endif
    .endm

! both ends' coordinate below 0: the window from 0; both above: to the edge;
! else both
    .macro  EDGEAXIS a, b, k, lo, hi, edge
    mov.l   @(F_PV+12*\a+\k,r15), r2
    mov.l   @(F_PV+12*\b+\k,r15), r3
    cmp/pz  r2
    bt      1f
    cmp/pz  r3
    bt      3f
    mov     #0, r0
    bra     4f
    mov.l   r0, @(\lo,r15)
1:  cmp/pl  r2
    bf      3f
    cmp/pl  r3
    bf      3f
    EDGEAT  \edge
    bra     4f
    mov.l   r0, @(\hi,r15)
3:  mov     #0, r0
    mov.l   r0, @(\lo,r15)
    EDGEAT  \edge
    mov.l   r0, @(\hi,r15)
4:
    .endm

! checkPortal: r8 the room, r9 the portal, r12 r13 r14 the room's walk
! translation -> r0 1 when the next room's window grew to it (its fill
! counted past the draw distance), else 0; r1 .. r7
.Lcheck:
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    add     #-FRAME, r15
    ! facing the camera: its normal times its first corner from the camera
    mov     r8, r1
    shll2   r1
    shll2   r1
    mov.l   @(R_LEVEL,gbr), r0
    add     r0, r1
    mov.w   @(L_X,r1), r0
    shll8   r0
    mov     r0, r2
    mov.w   @(L_Z,r1), r0
    shll8   r0
    mov     r0, r3
    mov.w   @(P_CORNERS,r9), r0
    add     r0, r2
    mov.l   @(R_CAMERA,gbr), r0
    sub     r0, r2                  ! d.x
    mov.w   @(P_CORNERS+2,r9), r0
    mov     r0, r4
    mov.l   @(R_CAMERA+4,gbr), r0
    sub     r0, r4                  ! d.y
    mov.w   @(P_CORNERS+4,r9), r0
    add     r0, r3
    mov.l   @(R_CAMERA+8,gbr), r0
    sub     r0, r3                  ! d.z
    mov.w   @(P_NORMAL,r9), r0
    mul.l   r0, r2
    mov.w   @(P_NORMAL+2,r9), r0
    sts     macl, r5
    mul.l   r0, r4
    mov.w   @(P_NORMAL+4,r9), r0
    sts     macl, r4
    mul.l   r0, r3
    add     r4, r5
    sts     macl, r4
    add     r4, r5
    cmp/pz  r5
    bf      1f
    bra     .Lnot
    nop
    ! the window starts empty about the frame's centre: x0 from the room's
    ! right edge, x1 from its left
1:  mov.l   @(R_CLIP,gbr), r0
    mov     r8, r7
    shll2   r7
    shll2   r7
    add     r0, r7                  ! the room's clip
    mov     #88, r1
    shll    r1                      ! FRAME_WIDTH / 2
    mov     #112, r2                ! FRAME_HEIGHT / 2
    mov.l   @(8,r7), r0
    sub     r1, r0
    mov.l   r0, @(F_X0,r15)
    mov.l   @(12,r7), r0
    sub     r2, r0
    mov.l   r0, @(F_Y0,r15)
    mov.l   @(0,r7), r0
    sub     r1, r0
    mov.l   r0, @(F_X1,r15)
    mov.l   @(4,r7), r0
    sub     r2, r0
    mov.l   r0, @(F_Y1,r15)
    mov     #0, r10
    mov     #0, r11
    mov.l   @(R_VIEW,gbr), r0
    mov     r0, r6
    CORNER  0
    CORNER  1
    CORNER  2
    CORNER  3
    mov     r10, r0
    cmp/eq  #4, r0
    bf      1f
    bra     .Lnot
    nop
1:  mov.l   @(R_CLIP,gbr), r0
    mov     r8, r7
    shll2   r7
    shll2   r7
    add     r0, r7
    mov     #88, r1
    shll    r1                      ! FRAME_WIDTH / 2
    mov     #112, r2                ! FRAME_HEIGHT / 2
    mov     r11, r0
    cmp/eq  #4, r0
    bf      .Lnear
    ! past the draw distance: the fill, within the room's window
    mov.l   @(F_X0,r15), r3
    add     r1, r3
    mov.l   @(0,r7), r0
    cmp/gt  r3, r0
    bf      1f
    mov     r0, r3
1:  mov.l   @(F_X1,r15), r4
    add     r1, r4
    mov.l   @(8,r7), r0
    cmp/gt  r0, r4
    bf      2f
    mov     r0, r4
2:  cmp/gt  r3, r4
    bf      .Lnofill
    mov.l   @(F_Y0,r15), r3
    add     r2, r3
    mov.l   @(4,r7), r0
    cmp/gt  r3, r0
    bf      3f
    mov     r0, r3
3:  mov.l   @(F_Y1,r15), r4
    add     r2, r4
    mov.l   @(12,r7), r0
    cmp/gt  r0, r4
    bf      4f
    mov     r0, r4
4:  cmp/gt  r3, r4
    bf      .Lnofill
    mov.l   @(R_VOIDS,gbr), r0
    add     #1, r0
    mov.l   r0, @(R_VOIDS,gbr)
.Lnofill:
    bra     .Lnot
    nop

.Lnear:
    ! the window in the frame's place
    .irp    f, F_X0, F_X1
    mov.l   @(\f,r15), r0
    add     r1, r0
    mov.l   r0, @(\f,r15)
    .endr
    .irp    f, F_Y0, F_Y1
    mov.l   @(\f,r15), r0
    add     r2, r0
    mov.l   r0, @(\f,r15)
    .endr
    tst     r10, r10
    bf      .Ledges
    ! cut to the room's window; empty, or the next room's grown to it
.Lnoedges:
    mov.l   @(R_CLIP,gbr), r0
    mov     r8, r7
    shll2   r7
    shll2   r7
    add     r0, r7
    mov.l   @(F_X0,r15), r1
    mov.l   @(0,r7), r0
    cmp/gt  r1, r0
    bf      1f
    mov     r0, r1
1:  mov.l   @(F_X1,r15), r3
    mov.l   @(8,r7), r0
    cmp/gt  r0, r3
    bf      2f
    mov     r0, r3
2:  mov.l   @(F_Y0,r15), r2
    mov.l   @(4,r7), r0
    cmp/gt  r2, r0
    bf      3f
    mov     r0, r2
3:  mov.l   @(F_Y1,r15), r4
    mov.l   @(12,r7), r0
    cmp/gt  r0, r4
    bf      4f
    mov     r0, r4
4:  cmp/gt  r1, r3
    bf      .Lnot
    cmp/gt  r2, r4
    bf      .Lnot
    mov.w   @r9, r0
    extu.w  r0, r7
    shll2   r7
    shll2   r7
    mov.l   @(R_CLIP,gbr), r0
    add     r0, r7                  ! the next room's clip
    mov.l   @(0,r7), r0
    cmp/gt  r1, r0
    bf      5f
    mov.l   r1, @(0,r7)
5:  mov.l   @(8,r7), r0
    cmp/gt  r0, r3
    bf      6f
    mov.l   r3, @(8,r7)
6:  mov.l   @(4,r7), r0
    cmp/gt  r2, r0
    bf      7f
    mov.l   r2, @(4,r7)
7:  mov.l   @(12,r7), r0
    cmp/gt  r0, r4
    bf      8f
    mov.l   r4, @(12,r7)
8:  mov     #1, r0
    add     #FRAME, r15
    mov.l   @r15+, r11
    rts
    mov.l   @r15+, r10
.Lnot:
    mov     #0, r0
    add     #FRAME, r15
    mov.l   @r15+, r11
    rts
    mov.l   @r15+, r10

    ! a corner behind the camera: the window opened to the frame's edges
    ! where the portal's edges cross the near plane
.Ledges:
    EDGE    0, 3
    EDGE    1, 0
    EDGE    2, 1
    EDGE    3, 2
    bra     .Lnoedges
    nop


! r1 r2 r3 the view's rows times (r1, r2, r3) -> r12 r13 r14; r0 r4 r5 r6
    .macro  TRANSLATE
    mov.l   @(R_VIEW,gbr), r0
    mov     r0, r6
    .irp    row, 0, 1, 2
    mov.l   @(\row*16,r6), r0
    mul.l   r0, r1
    mov.l   @(\row*16+4,r6), r0
    sts     macl, r5
    mul.l   r0, r2
    mov.l   @(\row*16+8,r6), r0
    sts     macl, r4
    mul.l   r0, r3
    add     r4, r5
    sts     macl, r4
    add     r4, r5
    .if \row == 0
    mov     r5, r12
    .elseif \row == 1
    mov     r5, r13
    .else
    mov     r5, r14
    .endif
    .endr
    .endm

! addVisibleRoom: r4 the room; the rooms behind its portals listed after
! those behind theirs. Keeps r8 .. r14.
.Lvisit:
    sts.l   pr, @-r15
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    mov     r4, r8
    mov     r8, r7
    shll2   r7
    shll2   r7
    mov.l   @(R_LEVEL,gbr), r0
    add     r0, r7                  ! its RoomLevel
    mov.w   @(L_X,r7), r0
    shll8   r0
    mov     r0, r1
    mov.l   @(R_CAMERA,gbr), r0
    sub     r0, r1
    mov.l   @(R_CAMERA+4,gbr), r0
    neg     r0, r2
    mov.w   @(L_Z,r7), r0
    shll8   r0
    mov     r0, r3
    mov.l   @(R_CAMERA+8,gbr), r0
    sub     r0, r3
    mov.b   @(L_PORTALS,r7), r0
    extu.b  r0, r10
    mov.l   @(L_FIRST,r7), r9
    shll2   r9
    shll2   r9
    shll    r9
    mov.l   @(R_PORTALS,gbr), r0
    add     r0, r9
    TRANSLATE
.Lportal:
    tst     r10, r10
    bt      .Lvisited
    bsr     .Lcheck
    nop
    tst     r0, r0
    bt      .Lnextportal
    mov.w   @r9, r0
    extu.w  r0, r4
    mov.l   r4, @-r15
    bsr     .Lvisit
    nop
    mov.l   @r15+, r4
    ! listed, unless it already is
    mov.l   @(R_VISIBLE,gbr), r0
    mov.b   @(r0,r4), r1
    tst     r1, r1
    bf      .Lnextportal
    mov     #1, r1
    mov.b   r1, @(r0,r4)
    mov.l   @(R_LIST_END,gbr), r0
    mov.w   r4, @r0
    add     #2, r0
    mov.l   r0, @(R_LIST_END,gbr)
.Lnextportal:
    add     #PORTAL, r9
    bra     .Lportal
    add     #-1, r10
.Lvisited:
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    mov.l   @r15+, r8
    lds.l   @r15+, pr
    rts
    nop

    .global _roomsSeenAsm
_roomsSeenAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    stc.l   gbr, @-r15
    ldc     r4, gbr
    mov     #0, r0
    mov.l   r0, @(R_VOIDS,gbr)
    ! the camera's room's window: the viewport
    mov.l   @(R_CAMERA_ROOM,gbr), r0
    mov     r0, r8
    shll2   r0
    shll2   r0
    mov     r0, r7
    mov.l   @(R_CLIP,gbr), r0
    add     r0, r7
    .irp    k, 0, 4, 8, 12
    mov.l   @(R_VIEWPORT+\k,gbr), r0
    mov.l   r0, @(\k,r7)
    .endr
    mov.l   @(R_LIST,gbr), r0
    mov.l   r0, @(R_LIST_END,gbr)
    bsr     .Lvisit
    mov     r8, r4
    mov.l   @(R_LIST_END,gbr), r0
    mov.w   r8, @r0
    add     #2, r0
    mov     r0, r11                 ! the list's end
    mov.l   @(R_LIST,gbr), r0
    mov     r0, r9                  ! the next listed
    sub     r9, r11
    shlr    r11
    mov     r11, r0
    mov.l   r0, @(R_LISTED,gbr)
    mov.l   @(R_SEEN,gbr), r0
    mov     r0, r10                 ! the next RoomSeen
    ! queueRoom: each listed room's window and matrix (the view placed at its
    ! origin and top)
.Lqueue:
    mov.w   @r9+, r0
    extu.w  r0, r8
    mov.w   r8, @r10
    mov     r8, r7
    shll2   r7
    shll2   r7
    mov.l   @(R_CLIP,gbr), r0
    add     r0, r7
    mov.l   @(0,r7), r0
    mov.l   r0, @(4,r10)
    mov.l   @(4,r7), r0
    mov.l   r0, @(8,r10)
    mov.l   @(8,r7), r0
    mov.l   r0, @(12,r10)
    mov.l   @(12,r7), r0
    mov.l   r0, @(16,r10)
    mov     r8, r7
    shll2   r7
    shll2   r7
    mov.l   @(R_LEVEL,gbr), r0
    add     r0, r7
    mov.w   @(L_X,r7), r0
    shll8   r0
    mov     r0, r1
    mov.l   @(R_CAMERA,gbr), r0
    sub     r0, r1
    mov.w   @(L_TOP,r7), r0
    mov     r0, r2
    mov.l   @(R_CAMERA+4,gbr), r0
    sub     r0, r2
    mov.w   @(L_Z,r7), r0
    shll8   r0
    mov     r0, r3
    mov.l   @(R_CAMERA+8,gbr), r0
    sub     r0, r3
    TRANSLATE
    ! the matrix: the view's rotation, that translation
    mov     r10, r7
    add     #20, r7
    .irp    row, 0, 1, 2
    mov.l   @(\row*16,r6), r0
    mov.l   r0, @(\row*16,r7)
    mov.l   @(\row*16+4,r6), r0
    mov.l   r0, @(\row*16+4,r7)
    mov.l   @(\row*16+8,r6), r0
    mov.l   r0, @(\row*16+8,r7)
    .endr
    mov.l   r12, @(12,r7)
    mov.l   r13, @(28,r7)
    mov.l   r14, @(44,r7)
    add     #SEEN, r10
    dt      r11
    bf      .Lqueue

    ! the listed rooms' windows and listings reset for the next frame, once
    ! all are queued (a room listed twice is queued with its window twice)
    mov.l   @(R_LIST,gbr), r0
    mov     r0, r9
    mov.l   @(R_LISTED,gbr), r0
    mov     r0, r11
    mov.l   @(R_CLIP,gbr), r0
    mov     r0, r5
    mov.l   @(R_VISIBLE,gbr), r0
    mov     r0, r6
    mov     #88, r1
    shll2   r1                      ! FRAME_WIDTH
    mov     #112, r2
    shll    r2                      ! FRAME_HEIGHT
    mov     #0, r3
.Lreset:
    mov.w   @r9+, r0
    extu.w  r0, r0
    mov.b   r3, @(r0,r6)
    shll2   r0
    shll2   r0
    add     r5, r0
    mov.l   r1, @r0
    mov.l   r2, @(4,r0)
    mov.l   r3, @(8,r0)
    dt      r11
    bf/s    .Lreset
    mov.l   r3, @(12,r0)

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

! rooms.cpp roomsQueue, placeRooms and roomBegin's caustics: each room listed
! with vertices (at most ROOM_JOBS), its vertices placed from MASTER_VERTICES
! up (a room that has no place below MAX_VERTICES has no job), as a FaceRoom
! (faces.h) for stages 2 and 3; the caustics' table once, if a room is wet.
!
! void roomsJobsAsm(RoomsContext* ctx)

    .equ    L_FLAGS,        6
    .equ    L_VERTICES,     8
    .equ    JOB,            112     ! FaceRoom
    .equ    J_COUNTS,       16      ! quadsCount, trianglesCount, facing[6]
    .equ    J_CAMERA,       32
    .equ    J_M,            44
    .equ    J_T,            64      ! from here, at r1: +0 t, +12 clip, +28 flags,
                                    ! +32 ripple, +36 phase, +40 count | room, +44 caustics
    .equ    G_QUADS,        0       ! RoomGeometry: +12 quadsCount .. facing[6]
    .equ    ROOM_JOBS,      64

    .global _roomsJobsAsm
_roomsJobsAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    stc.l   gbr, @-r15
    ldc     r4, gbr
    ! the camera's room under water
    mov.l   @(R_CAMERA_ROOM,gbr), r0
    shll2   r0
    shll2   r0
    mov     r0, r1
    mov.l   @(R_LEVEL,gbr), r0
    add     r0, r1
    mov.b   @(L_FLAGS,r1), r0
    and     #1, r0
    mov     r0, r13
    mov.l   @(R_SEEN,gbr), r0
    mov     r0, r8                  ! the next room listed
    mov.l   @(R_LISTED,gbr), r0
    mov     r0, r9
    mov.l   @(R_JOBS,gbr), r0
    mov     r0, r10                 ! the next job
    mov     #0, r0
    mov.l   r0, @(R_JOB_COUNT,gbr)
    mov     #1, r11
    shll8   r11
    shll2   r11
    shll    r11                     ! the vertices' bottom: MASTER_VERTICES
    mov     #0, r12                 ! the rooms queued
    bra     .Ljob
    mov     #0, r14                 ! any wet
.Lnojob:
    dt      r9
    bf/s    .Ljob
    add     #SEEN, r8
    bra     .Ljobsend
    nop
.Ljob:
    mov.w   @r8, r0
    extu.w  r0, r6                  ! its room
    mov     r6, r7
    shll2   r7
    shll2   r7
    mov.l   @(R_LEVEL,gbr), r0
    add     r0, r7                  ! its RoomLevel
    mov.w   @(L_VERTICES,r7), r0
    extu.w  r0, r5
    tst     r5, r5
    bt      .Lnojob
    mov     r12, r0
    cmp/eq  #ROOM_JOBS, r0
    bt      .Lnojob
    add     #1, r12
    mov     r5, r4
    add     #15, r4
    mov     #-16, r0
    and     r0, r4                  ! its vertices' size
    mov     r11, r0
    add     r4, r0
    mov     #5, r1
    shll8   r1
    shll2   r1                      ! MAX_VERTICES
    cmp/gt  r1, r0
    bt      .Lnojob
    ! its geometry, its vertices' place
    mov     r6, r3
    shll2   r3
    shll2   r3
    shll    r3
    mov.l   @(R_GEOMETRY,gbr), r0
    add     r0, r3                  ! its RoomGeometry
    mov.l   @(R_RGEO,gbr), r0
    mov     r0, r2
    mov.l   @(G_QUADS,r3), r0
    add     r2, r0
    mov.l   r0, @r10
    mov.l   @(G_QUADS+4,r3), r0
    add     r2, r0
    mov.l   r0, @(4,r10)
    mov.l   @(G_QUADS+8,r3), r0
    add     r2, r0
    mov.l   r0, @(8,r10)
    mov     r11, r1
    shll2   r1
    shll    r1
    mov.l   @(R_VERTICES,gbr), r0
    add     r1, r0
    mov.l   r0, @(12,r10)
    add     r4, r11
    .irp    k, 0, 4, 8, 12
    mov.l   @(12+\k,r3), r0
    mov.l   r0, @(J_COUNTS+\k,r10)
    .endr
    ! the camera from its origin
    mov.w   @(L_X,r7), r0
    shll8   r0
    mov     r0, r1
    mov.l   @(R_CAMERA,gbr), r0
    sub     r1, r0
    mov.l   r0, @(J_CAMERA,r10)
    mov.w   @(L_TOP,r7), r0
    mov     r0, r1
    mov.l   @(R_CAMERA+4,gbr), r0
    sub     r1, r0
    mov.l   r0, @(J_CAMERA+4,r10)
    mov.w   @(L_Z,r7), r0
    shll8   r0
    mov     r0, r1
    mov.l   @(R_CAMERA+8,gbr), r0
    sub     r1, r0
    mov.l   r0, @(J_CAMERA+8,r10)
    ! the matrix's rotation, its translation >> 6
    mov     r10, r1
    add     #J_M, r1
    .irp    k, 0, 1, 2, 4, 5, 6, 8, 9, 10
    mov.l   @(20+4*\k,r8), r0
    .if \k < 4
    mov.w   r0, @(2*\k,r1)
    .elseif \k < 8
    mov.w   r0, @(2*\k-2,r1)
    .else
    mov.w   r0, @(2*\k-4,r1)
    .endif
    .endr
    mov     r10, r1
    add     #J_T, r1
    mov     r8, r2
    add     #32, r2                 ! its matrix's translation column
    mov     #1, r3
    shll16  r3
    shll8   r3
    shll2   r3                      ! 2^26: x >> 6 the high longword of x 2^26
    .irp    k, 0, 1, 2
    mov.l   @(16*\k,r2), r0
    dmuls.l r0, r3
    sts     mach, r0
    mov.l   r0, @(4*\k,r1)
    .endr
    .irp    k, 0, 4, 8, 12
    mov.l   @(4+\k,r8), r0
    mov.l   r0, @(12+\k,r1)
    .endr
    ! wet (the room or the camera under water), rippling (one of them)
    mov.b   @(L_FLAGS,r7), r0
    and     #1, r0
    mov     r0, r2
    xor     r13, r2
    mov.l   r2, @(32,r1)
    or      r13, r0
    or      r0, r14
    mov     r0, r2
    rotr    r0
    rotr    r0
    mov.l   r0, @(28,r1)            ! FACE_WATER
    neg     r2, r2
    mov.l   @(R_TABLE,gbr), r0
    and     r2, r0
    mov.l   r0, @(44,r1)
    mov.l   @(R_CAUSTICS,gbr), r0
    mov.l   @r0, r0
    mov.l   r0, @(36,r1)
    mov     r5, r0
    shll16  r0
    or      r6, r0
    mov.l   r0, @(40,r1)
    add     #JOB, r10
    mov.l   @(R_JOB_COUNT,gbr), r0
    add     #1, r0
    bra     .Lnojob
    mov.l   r0, @(R_JOB_COUNT,gbr)

.Ljobsend:
    ! the caustics: each vertex's shade offset, the wave at its start plus
    ! the frame's phase
    tst     r14, r14
    bt      .Ljobsdone
    mov.l   @(R_CAUSTICS,gbr), r0
    mov     r0, r3
    mov.l   @r3+, r2                ! the phase
    mov     r3, r4
    add     #64, r4                 ! the starts
    mov.l   @(R_TABLE,gbr), r0
    mov     r0, r5
    mov     #32, r6
.Lcaustic:
    mov.b   @r4+, r0
    add     r2, r0
    and     #31, r0
    add     r0, r0
    mov.w   @(r0,r3), r7
    mov.w   r7, @r5
    dt      r6
    bf/s    .Lcaustic
    add     #2, r5
.Ljobsdone:
    ldc.l   @r15+, gbr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

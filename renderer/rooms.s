! Stage 1 the new way (rooms.h): the rooms seen, each exactly as the game
! sees them: openlara room.h's portal walk (getVisibleRooms, addVisibleRoom,
! checkPortal), then draw.h queueRoom's matrix for each room listed.
!
! The walk enters a room again only when its window grew, where the game's
! enters it again by every way the portals lead to it. Windows only grow, so
! a room entered with the window it was last walked with finds nothing new
! behind its portals: the windows come out the game's, and the list and its
! order too unless the walk comes back round to a room it is still in (of
! 80000 random views over every level none did, but for a few that loop for
! ever, which the game's walk can't survive). Diagonal views across the
! Cistern's rooms took the game's walk ~300 portal checks for 33 rooms.
!
! void roomsSeenAsm(RoomsContext* ctx)
!
! A room's walk matrix is the view's rotation, its translation the view's
! rows times the room's origin from the camera (matrixTranslateAbs): no
! stack, the translation carried down the walk in r12 r13 r14. A portal's
! corner is the rotation times it plus that translation (DP43): MAC.W on the
! corner's 16-bit x y z and the rotation's rows copied to 16 bits (2^14
! fixed, they fit), whose low longword is the products' sum as C's. Its
! >> 14 and the projection's >> 12 are the high longword of a product by
! 2^18 and 2^20 (DMULS.L), exact as C's for any longword; the divide
! table's step never needs its clamp (a corner is held to the far plane
! first).
!
! A portal faces the camera when its normal times its first corner from the
! camera is below 0. Every level's normals lie along one axis, +-1 (all 21
! levels' 2600 portals), so the corner's offset on that axis and the
! normal's sign decide it, without the products.
!
! RoomsContext (rooms.h): +0 level (RoomLevel, 16 bytes each), +4 portals
! (32 each: room, normal, 4 corners), +8 divTable, +12 camera, +24 view
! (Matrix), +28 clip (per room x0 y0 x1 y1), +32 visible (a byte a room),
! +36 list (uint16), +40 listed, +44 seen (RoomSeen, 68 each), +48 viewport,
! +64 the camera's room, +68 voids, +72 the list's end (the walk's own),
! +76 2^18, +80 2^20, +84 VIEW_MAX_F (constants out of a pool's reach); for
! the jobs +88 each room's RoomGeometry, +92 RGEO, +96 gVertices, +100 the
! jobs (FaceRoom), +104 their count, +108 RoomCaustics, +112 the caustics'
! table (32 int16); +116 the fills' rectangles (x0 y0 x1 y1, the first
! ROOM_FILLS; +68 counts them all); +120 a corner past the far plane scaled
! to it (rooms.h roomsFarScaled); +124 the view's rotation, its rows as
! 16-bit x y z (the walk's own); +144 the entries (RoomEntry: the room seen,
! the room it was seen from, the portal it was seen by), +148 their count
! (all of them: ROOM_ENTRIES kept); +152 where the vertices stop; +156 by room,
! hidden; +160 by room, seen nearer than where it is all at the fog's floor;
! +164 windows under so many pixels aren't entered (rooms.h roomsTiny); +168 the
! solid boxes on the frame (BoxSeen), +172 their count.
!
! A portal whose window lies in a box's rectangle and whose corners are all past
! the box's sides that face the camera isn't entered: whatever is seen through it
! is behind the box.
!
! A point seen through a portal is no nearer than the portal's nearest corner,
! so a room seen by no portal with a corner nearer than RoomLevel.black shows
! nothing but the fog's floor: it is dropped. No job, no objects, its window
! filled; the walk goes on through it to what may be bright beyond.

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
    .equ    R_FILLS,        116
    .equ    R_FAR_SCALED,   120
    .equ    R_ROTATION,     124
    .equ    R_ENTRIES,      144
    .equ    R_ENTRY_COUNT,  148
    .equ    R_VERTICES_TOP, 152
    .equ    R_HIDDEN,       156
    .equ    R_SEEN_NEAR,    160
    .equ    R_TINY,         164
    .equ    R_BOXES,        168
    .equ    R_BOX_COUNT,    172
    .equ    R_SOLID,        176
    .equ    R_SOLID_COUNT,  180
    .equ    BOX,            40      ! BoxSeen
    .equ    BOX_NEAR,       8
    .equ    BOX_LO,         12
    .equ    BOX_HI,         24
    .equ    BOX_AREA,       36
    .equ    TINY_UNFILLED,  16      ! a window under these pixels isn't even filled
    .equ    ROOM_ENTRIES,   128
    .equ    ROOM_FILLS,     64

    .equ    L_X,            0       ! RoomLevel
    .equ    L_Z,            2
    .equ    L_TOP,          4
    .equ    L_PORTALS,      7
    .equ    L_BLACK,        10
    .equ    L_FIRST,        12
    .equ    PORTAL,         32
    .equ    P_NORMAL,       2
    .equ    P_CORNERS,      8
    .equ    SEEN,           68

    .text
    .align  2

! r12 r13 r14 (roomsBoxesAsm) on along axis k, side to side (op add), or back (sub); r0
    .macro  BOX_STEP op, k
    mov.l   @(B_DELTA+0*12+\k*4,r15), r0
    \op     r0, r12
    mov.l   @(B_DELTA+1*12+\k*4,r15), r0
    \op     r0, r13
    mov.l   @(B_DELTA+2*12+\k*4,r15), r0
    \op     r0, r14
    .endm

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
! r6 the rotation's 16-bit rows); r0 .. r5, r7
    .macro  CORNER i
    mov     r9, r3
    add     #P_CORNERS+6*\i, r3     ! its x y z
    mov     r6, r2
    .irp    row, 0, 1, 2
    clrmac
    mac.w   @r2+, @r3+
    mac.w   @r2+, @r3+
    mac.w   @r2+, @r3+
    add     #-6, r3
    sts     macl, r5
    .if \row == 0
    add     r12, r5
    .elseif \row == 1
    add     r13, r5
    .else
    add     r14, r5
    .endif
    mov.l   r5, @(F_PV+12*\i+4*\row,r15)
    .endr
    ! r5 the corner's z: behind the camera, or held to the far plane, its x
    ! and y scaled to it unless the original's way (64 / 32 divides)
    cmp/pl  r5
    bt      1f
    bra     9f
    add     #1, r10
1:  mov.l   @(R_VIEW_MAX,gbr), r0
    cmp/gt  r0, r5
    bf      2f
    mov.l   @(R_FAR_SCALED,gbr), r0
    tst     r0, r0
    bt      7f
    mov     #-1, r7
    shll8   r7                      ! the divider at 0xFFFFFF00
    mov.l   r5, @r7                 ! DVSR: z
    mov.l   @(R_VIEW_MAX,gbr), r0
    .irp    k, 0, 4
    mov.l   @(F_PV+12*\i+\k,r15), r1
    dmuls.l r0, r1
    sts     mach, r1
    mov.l   r1, @(16,r7)            ! DVDNTH
    sts     macl, r1
    mov.l   r1, @(20,r7)            ! DVDNTL: divides
    mov.l   @(20,r7), r1
    mov.l   r1, @(F_PV+12*\i+\k,r15)
    .endr
7:  mov.l   @(R_VIEW_MAX,gbr), r0
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
    ! PERSPECTIVE: d = divTable[z >> 4], x = x d >> 12, y = y d >> 12; under
    ! 16 units ahead there is no scale: the frame's edges x and y point to
    shlr2   r3
    shlr2   r3
    tst     r3, r3
    bf      6f
    mov     #88, r0
    shll    r0
    cmp/pz  r1
    bt/s    8f
    mov     r0, r1
    neg     r0, r1
8:  mov     #112, r0
    cmp/pz  r2
    bt/s    10f
    mov     r0, r2
    neg     r0, r2
10: bra     11f
    nop
6:  shll    r3
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
11: mov.l   @(F_X0,r15), r0
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
! translation -> r0 1 when the next room's window grew (its fill counted
! past the draw distance), else 0; r1 .. r7
.Lcheck:
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    add     #-FRAME, r15
    ! facing the camera: r4 the normal's one component, r2 the first
    ! corner's offset from the camera on its axis
    mov.w   @(P_NORMAL,r9), r0
    tst     r0, r0
    bt      2f
    mov     r0, r4
    mov     r8, r1
    shll2   r1
    shll2   r1
    mov.l   @(R_LEVEL,gbr), r0
    add     r0, r1
    mov.w   @(L_X,r1), r0
    shll8   r0
    mov     r0, r2
    mov.w   @(P_CORNERS,r9), r0
    add     r0, r2
    mov.l   @(R_CAMERA,gbr), r0
    bra     4f
    sub     r0, r2                  ! d.x
2:  mov.w   @(P_NORMAL+2,r9), r0
    tst     r0, r0
    bt      3f
    mov     r0, r4
    mov.w   @(P_CORNERS+2,r9), r0
    mov     r0, r2
    mov.l   @(R_CAMERA+4,gbr), r0
    bra     4f
    sub     r0, r2                  ! d.y
3:  mov.w   @(P_NORMAL+4,r9), r0
    mov     r0, r4
    mov     r8, r1
    shll2   r1
    shll2   r1
    mov.l   @(R_LEVEL,gbr), r0
    add     r0, r1
    mov.w   @(L_Z,r1), r0
    shll8   r0
    mov     r0, r2
    mov.w   @(P_CORNERS+4,r9), r0
    add     r0, r2
    mov.l   @(R_CAMERA+8,gbr), r0
    sub     r0, r2                  ! d.z
    ! their product 0 or above: away
4:  tst     r2, r2
    bt      5f
    xor     r4, r2
    cmp/pz  r2
    bf      1f
5:  bra     .Lnot
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
    stc     gbr, r6
    add     #R_ROTATION, r6
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
    ! past the draw distance: the fill, within the room's window (r3 r5 to
    ! r4 r6), kept
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
    mov.l   @(F_Y0,r15), r5
    add     r2, r5
    mov.l   @(4,r7), r0
    cmp/gt  r5, r0
    bf      3f
    mov     r0, r5
3:  mov.l   @(F_Y1,r15), r6
    add     r2, r6
    mov.l   @(12,r7), r0
    cmp/gt  r0, r6
    bf      4f
    mov     r0, r6
4:  cmp/gt  r5, r6
    bf      .Lnofill
    mov.l   @(R_VOIDS,gbr), r0
    mov     #ROOM_FILLS, r1
    cmp/hs  r1, r0
    bt      5f
    shll2   r0
    shll2   r0
    mov     r0, r1
    mov.l   @(R_FILLS,gbr), r0
    add     r0, r1
    mov.l   r3, @r1
    mov.l   r5, @(4,r1)
    mov.l   r4, @(8,r1)
    mov.l   r6, @(12,r1)
5:  mov.l   @(R_VOIDS,gbr), r0
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
    bt      .Lnoedges
    bra     .Ledges
    nop
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
    bt      19f
20: bra     .Lnot
    nop
19: cmp/gt  r2, r4
    bf      20b
    mov.l   @(R_BOX_COUNT,gbr), r0
    tst     r0, r0
    bt      24f
    tst     r10, r10
    bf      24f
    bra     .Lboxes
    nop
    ! too small to matter: nothing behind it, its window filled if it's worth it
24: mov.l   @(R_TINY,gbr), r0
    tst     r0, r0
    bt      21f
    tst     r10, r10
    bf      21f
    mov     r3, r5
    sub     r1, r5
    mov     r4, r6
    sub     r2, r6
    mulu.w  r5, r6
    sts     macl, r5
    cmp/hs  r0, r5
    bt      21f
    mov     #TINY_UNFILLED, r0
    cmp/hs  r0, r5
    bf      20b
    mov.l   @(R_VOIDS,gbr), r0
    mov     #ROOM_FILLS, r5
    cmp/hs  r5, r0
    bt      22f
    shll2   r0
    shll2   r0
    mov     r0, r5
    mov.l   @(R_FILLS,gbr), r0
    add     r0, r5
    mov.l   r1, @r5
    mov.l   r2, @(4,r5)
    mov.l   r3, @(8,r5)
    mov.l   r4, @(12,r5)
22: mov.l   @(R_VOIDS,gbr), r0
    add     #1, r0
    bra     20b
    mov.l   r0, @(R_VOIDS,gbr)
21: mov.w   @r9, r0
    extu.w  r0, r7
    shll2   r7
    shll2   r7
    mov.l   @(R_CLIP,gbr), r0
    add     r0, r7                  ! the next room's clip
    mov     #0, r5                  ! grown
    mov.l   @(0,r7), r0
    cmp/gt  r1, r0
    bf      5f
    mov.l   r1, @(0,r7)
    mov     #1, r5
5:  mov.l   @(8,r7), r0
    cmp/gt  r0, r3
    bf      6f
    mov.l   r3, @(8,r7)
    mov     #1, r5
6:  mov.l   @(4,r7), r0
    cmp/gt  r2, r0
    bf      7f
    mov.l   r2, @(4,r7)
    mov     #1, r5
7:  mov.l   @(12,r7), r0
    cmp/gt  r0, r4
    bf      8f
    mov.l   r4, @(12,r7)
    mov     #1, r5
    ! a corner nearer than where the next room is all at the fog's floor
8:  mov.w   @r9, r0
    extu.w  r0, r3
    mov     r3, r7
    shll2   r7
    shll2   r7
    mov.l   @(R_LEVEL,gbr), r0
    add     r0, r7
    mov.w   @(L_BLACK,r7), r0
    tst     r10, r10
    bf      12f
    tst     r0, r0
    bt      12f
    shll8   r0
    shll2   r0
    shll2   r0
    shll2   r0
    .irp    i, 0, 1, 2
    mov.l   @(F_PV+12*\i+8,r15), r1
    cmp/gt  r1, r0
    bt      12f
    .endr
    mov.l   @(F_PV+12*3+8,r15), r1
    cmp/gt  r1, r0
    bf      13f
12: mov.l   @(R_SEEN_NEAR,gbr), r0
    mov     #1, r1
    mov.b   r1, @(r0,r3)
    ! the entry, for the items' test (rooms.h roomsItemSeen)
13: mov.l   @(R_ENTRY_COUNT,gbr), r0
    mov     r0, r1
    add     #1, r0
    mov.l   r0, @(R_ENTRY_COUNT,gbr)
    mov     #ROOM_ENTRIES / 2, r2
    shll    r2
    cmp/hs  r2, r1
    bt      9f
    shll2   r1
    shll    r1
    mov.l   @(R_ENTRIES,gbr), r0
    add     r0, r1
    mov.w   @r9, r0
    mov.b   r0, @r1                 ! the room seen
    mov     r8, r0
    mov.b   r0, @(1,r1)             ! from
    mov.l   r9, @(4,r1)             ! by
9:  mov     r5, r0
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

    ! the window r1 r2 r3 r4 of the portal at r9, no corner behind the camera:
    ! behind a box it isn't entered, else on as it was; r0, r5, r6, r7
.Lboxes:
    mov.l   @(R_BOXES,gbr), r0
    mov     r0, r5
    mov.l   @(R_BOX_COUNT,gbr), r0
    mov     r0, r6
1:  mov.w   @r5, r0                 ! in its rectangle
    cmp/gt  r1, r0
    bt      4f
    mov.w   @(4,r5), r0
    cmp/gt  r0, r3
    bt      4f
    mov.w   @(2,r5), r0
    cmp/gt  r2, r0
    bt      4f
    mov.w   @(6,r5), r0
    cmp/gt  r0, r4
    bf      5f
4:  dt      r6
    bf/s    1b
    add     #BOX, r5
    bra     24b
    nop
    ! the room's origin from the camera in r1 r2 r3
5:
    mov.l   r1, @-r15
    mov.l   r2, @-r15
    mov.l   r3, @-r15
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
    mov.l   @(R_CAMERA+4,gbr), r0
    neg     r0, r2
    mov.w   @(L_Z,r7), r0
    shll8   r0
    mov     r0, r3
    mov.l   @(R_CAMERA+8,gbr), r0
    sub     r0, r3
    ! each corner past the box's sides that face the camera
    .irp    c, 0, 1, 2, 3
    .irp    k, 0, 1, 2
    mov.w   @(P_CORNERS+6*\c+2*\k,r9), r0
    .if \k == 0
    add     r1, r0
    .elseif \k == 1
    add     r2, r0
    .else
    add     r3, r0
    .endif
    mov     r0, r7
    mov.l   @(BOX_LO+4*\k,r5), r0
    cmp/gt  r0, r7
    bf      3f
    mov.l   @(BOX_HI+4*\k,r5), r0
    cmp/gt  r7, r0
    bf      3f
    .endr
    .endr
    add     #12, r15
    bra     .Lnot
    nop
3:  mov.l   @r15+, r3
    mov.l   @r15+, r2
    mov.l   @r15+, r1
    bra     4b
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
    mov.l   r0, @(R_ENTRY_COUNT,gbr)
    ! the view's rotation as 16-bit rows, for the corners' MAC.W
    mov.l   @(R_VIEW,gbr), r0
    mov     r0, r6
    mov     r4, r7
    add     #R_ROTATION, r7
    .irp    row, 0, 1, 2
    .irp    k, 0, 1, 2
    mov.l   @(\row*16+4*\k,r6), r0
    mov.w   r0, @(2*(\row*3+\k),r7)
    .endr
    .endr
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
    ! dropped: hidden, or not the camera's and seen nowhere nearer than its
    ! floor; its window a fill
    mov.l   @(R_HIDDEN,gbr), r0
    mov     r0, r1
    mov     r8, r0
    mov.b   @(r0,r1), r1
    tst     r1, r1
    bf      14f
    mov.l   @(R_CAMERA_ROOM,gbr), r0
    cmp/eq  r0, r8
    bt      15f
    mov.l   @(R_SEEN_NEAR,gbr), r0
    mov     r0, r1
    mov     r8, r0
    mov.b   @(r0,r1), r1
    tst     r1, r1
    bf      15f
14: mov     #1, r0
    mov.w   r0, @(2,r10)
    mov.l   @(R_VOIDS,gbr), r0
    mov     #ROOM_FILLS, r1
    cmp/hs  r1, r0
    bt      16f
    shll2   r0
    shll2   r0
    mov     r0, r1
    mov.l   @(R_FILLS,gbr), r0
    add     r0, r1
    .irp    k, 0, 4, 8, 12
    mov.l   @(4+\k,r10), r0
    mov.l   r0, @(\k,r1)
    .endr
16: mov.l   @(R_VOIDS,gbr), r0
    add     #1, r0
    bra     17f
    mov.l   r0, @(R_VOIDS,gbr)
15: mov     #0, r0
    mov.w   r0, @(2,r10)
17:
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
    bt      18f
    bra     .Lqueue
    nop
18:
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
    mov.l   @(R_SEEN_NEAR,gbr), r0
    mov     r0, r4
    mov     #88, r1
    shll2   r1                      ! FRAME_WIDTH
    mov     #112, r2
    shll    r2                      ! FRAME_HEIGHT
    mov     #0, r3
.Lreset:
    mov.w   @r9+, r0
    extu.w  r0, r0
    mov.b   r3, @(r0,r6)
    mov.b   r3, @(r0,r4)
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

    .ifndef RENDER_LAB              ! the lab has none, and no room
! void roomsBoxesAsm(RoomsContext* ctx)
!
! The level's solid boxes (SolidBox) on the frame (BoxSeen, the biggest BOXES_SEEN): for each
! in front of the camera and short of the far plane all over, a rectangle wholly inside its
! outline. Two opposite sides, one wholly above the other on the frame: over the x both cover,
! every point between them lies on a line from one to the other, so inside the outline; or
! the same across. Top and bottom first, as most boxes stand; failing that the biggest of the
! rest, which is how a flat one is seen.

    .equ    SOLID,          28
    .equ    SOLID_SIDE,     24
    .equ    BOXES_SEEN,     6
    .equ    BOX_REACH,      1       ! << 16: further along an axis isn't looked at, the products fit
    .equ    BOX_NEAR_Z,     128     ! a corner nearer: its projection isn't the box's outline
    .equ    BOX_LEAST,      24      ! a rectangle narrower or lower on the frame isn't worth its test
    .equ    BOX_AREA_LEAST, 2000    ! ... or with fewer pixels on it: too little lies wholly inside
    .equ    BOX_COORD,      1022    ! a rectangle's reach, inside what a vertex is clamped to

    ! the frame: at r15 each box's sides from the camera (k * 8 + side * 4), what a row of the
    ! view adds along each axis, side to side (row * 12 + k * 4); at r11 the corners on the
    ! frame, x by corner * 4, y 32 on; at r10 the best rectangle, the nearest depth, two
    ! sides' ranges
    .equ    B_D,            0
    .equ    B_DELTA,        24
    .equ    B_X0,           0
    .equ    B_Y0,           4
    .equ    B_X1,           8
    .equ    B_Y1,           12
    .equ    B_AREA,         16
    .equ    B_NEAR,         20
    .equ    B_A,            24      ! x0 x1 y0 y1
    .equ    B_B,            40

    .align  2
    ! the corner by r12 r13 r14, its view place << 14, to the frame's corner r6 (by 4); r0 .. r5
.LboxCorner:
    mov     r14, r3
    shlr16  r3
    shlr2   r3
    shll    r3
    mov.l   @(R_DIV,gbr), r0
    add     r0, r3
    mov.w   @r3, r3
    extu.w  r3, r3                  ! the divide table's
    mov.l   @(R_K18,gbr), r0
    mov     r0, r4
    mov.l   @(R_K20,gbr), r0
    mov     r0, r5
    dmuls.l r12, r4
    sts     mach, r1
    mul.l   r3, r1
    sts     macl, r1
    dmuls.l r1, r5
    sts     mach, r1
    mov     #88, r0
    shll    r0
    add     r0, r1
    mov     r6, r0
    mov.l   r1, @(r0,r11)
    dmuls.l r13, r4
    sts     mach, r1
    mul.l   r3, r1
    sts     macl, r1
    dmuls.l r1, r5
    sts     mach, r1
    mov     #112, r0
    add     r0, r1
    mov     r6, r0
    add     #32, r0
    rts
    mov.l   r1, @(r0,r11)

    ! the range of the 4 corners listed at r4 into the 4 words at r5; r0 .. r3, r6, r7, r12, r13, r14
.LboxSide:
    mov.b   @r4+, r0
    mov.l   @(r0,r11), r1           ! x: least
    mov     r1, r2                  ! ... most
    add     #32, r0
    mov.l   @(r0,r11), r3           ! y: least
    mov     r3, r7                  ! ... most
    mov     #3, r14
1:  mov.b   @r4+, r0
    mov.l   @(r0,r11), r12
    add     #32, r0
    mov.l   @(r0,r11), r13
    cmp/gt  r12, r1
    bf      2f
    mov     r12, r1
2:  cmp/gt  r2, r12
    bf      3f
    mov     r12, r2
3:  cmp/gt  r13, r3
    bf      4f
    mov     r13, r3
4:  cmp/gt  r7, r13
    bf      5f
    mov     r13, r7
5:  dt      r14
    bf      1b
    mov.l   r1, @r5
    mov.l   r2, @(4,r5)
    mov.l   r3, @(8,r5)
    rts
    mov.l   r7, @(12,r5)

    ! rectangle r1 r2 to r3 r4 kept if it shows more of the frame than the best; r0, r5, r6, r7
.LboxTry:
    mov     r1, r5
    cmp/pz  r5
    bt      1f
    mov     #0, r5
1:  mov     #88, r0
    shll2   r0                      ! FRAME_WIDTH
    mov     r3, r6
    cmp/gt  r0, r6
    bf      2f
    mov     r0, r6
2:  sub     r5, r6                  ! its width on the frame
    mov     #BOX_LEAST, r0
    cmp/ge  r0, r6
    bf      9f
    mov     r2, r5
    cmp/pz  r5
    bt      3f
    mov     #0, r5
3:  mov     #112, r0
    shll    r0                      ! FRAME_HEIGHT
    mov     r4, r7
    cmp/gt  r0, r7
    bf      4f
    mov     r0, r7
4:  sub     r5, r7                  ! its height
    mov     #BOX_LEAST, r0
    cmp/ge  r0, r7
    bf      9f
    mulu.w  r6, r7
    mov.l   @(B_AREA,r10), r0
    sts     macl, r5
    cmp/gt  r0, r5
    bf      9f
    mov.l   r5, @(B_AREA,r10)
    mov.l   r1, @(B_X0,r10)
    mov.l   r2, @(B_Y0,r10)
    mov.l   r3, @(B_X1,r10)
    mov.l   r4, @(B_Y1,r10)
9:  rts
    nop

    ! r low of a and b's at their offsets; r0
    .macro  BOX_MOST r, a, b
    mov.l   @(\a,r10), \r
    mov.l   @(\b,r10), r0
    cmp/gt  \r, r0
    bf      1f
    mov     r0, \r
1:
    .endm

    .macro  BOX_LEAST_OF r, a, b
    mov.l   @(\a,r10), \r
    mov.l   @(\b,r10), r0
    cmp/gt  r0, \r
    bf      1f
    mov     r0, \r
1:
    .endm

    ! side at offset a (x0 x1 y0 y1) wholly above that at b, or wholly left of it: a rectangle
    .macro  BOX_APART a, b
    mov.l   @(\a+12,r10), r2
    mov.l   @(\b+8,r10), r4
    cmp/ge  r4, r2
    bt      7f
    BOX_MOST r1, \a, \b
    BOX_LEAST_OF r3, \a+4, \b+4
    bsr     .LboxTry
    nop
7:  mov.l   @(\a+4,r10), r1
    mov.l   @(\b,r10), r3
    cmp/ge  r3, r1
    bt      8f
    BOX_MOST r2, \a+8, \b+8
    BOX_LEAST_OF r4, \a+12, \b+12
    bsr     .LboxTry
    nop
8:
    .endm

    ! the two sides listed at r4, 8 corners by 4 each; r0 .. r7, r12, r13, r14
.LboxPair:
    sts.l   pr, @-r15
    mov     r10, r5
    bsr     .LboxSide
    add     #B_A, r5
    mov     r10, r5
    bsr     .LboxSide
    add     #B_B, r5
    BOX_APART B_A, B_B
    BOX_APART B_B, B_A
    lds.l   @r15+, pr
    rts
    nop

    .align  2
    .global _roomsBoxesAsm
_roomsBoxesAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    stc.l   gbr, @-r15
    add     #-120, r15
    add     #-64, r15
    ldc     r4, gbr
    mov     r15, r11
    add     #64, r11
    mov     r15, r10
    add     #127, r10
    add     #1, r10
    mov     #0, r0
    mov.l   r0, @(R_BOX_COUNT,gbr)
    mov.l   @(R_SOLID,gbr), r0
    mov     r0, r8
    mov.l   @(R_SOLID_COUNT,gbr), r0
    tst     r0, r0
    bf/s    .LboxOne
    mov     r0, r9
    bra     .LboxesDone
    nop

.LboxNext:
    dt      r9
    bf/s    .LboxOne
    add     #SOLID, r8
    bra     .LboxesDone
    nop

    ! from the camera; it, or too far along an axis: not looked at
.LboxOne:
    mov     #0, r4                  ! axes the camera lies within
    mov     #BOX_REACH, r5
    shll16  r5                      ! the reach
    neg     r5, r6
    .irp    k, 0, 1, 2
    mov.l   @(\k*4,r8), r1
    mov.l   @(R_CAMERA+\k*4,gbr), r0
    sub     r0, r1
    mov.l   r1, @(B_D+\k*8,r15)
    mov.l   @(12+\k*4,r8), r2
    sub     r0, r2
    mov.l   r2, @(B_D+\k*8+4,r15)
    cmp/gt  r6, r1
    bf      .LboxNext
    cmp/gt  r2, r5
    bf      .LboxNext
    cmp/pl  r1
    bt      1f
    cmp/pz  r2
    bf      1f
    add     #1, r4
1:
    .endr
    mov     r4, r0
    cmp/eq  #3, r0
    bt      .LboxNext
    ! a flat one from its side
    mov.l   @(SOLID_SIDE,r8), r0
    tst     r0, r0
    bt      .LboxSolid
    mov     r0, r1
    cmp/pz  r1
    bt      2f
    neg     r0, r0
2:  add     #-1, r0
    shll2   r0
    shll    r0
    mov.l   @(r0,r15), r2           ! its plane from the camera
    cmp/pz  r1
    bf      3f
    neg     r2, r2
3:  cmp/pl  r2
    bf      .LboxNext
.LboxSolid:
    ! the depth at its low corner, the most and least its axes can take that to: all in view
    mov.l   @(R_VIEW,gbr), r0
    mov     r0, r7                  ! the view, row 2 at +32
    mov     #0, r14                 ! at the low corner
    mov     #0, r12                 ! less, along the axes
    mov     #0, r13                 ! more
    .irp    k, 0, 1, 2
    mov     #32+\k*4, r0
    mov.l   @(r0,r7), r1
    mov.l   @(B_D+\k*8,r15), r2
    mul.l   r1, r2
    mov.l   @(B_D+\k*8+4,r15), r3
    sts     macl, r4
    add     r4, r14
    sub     r2, r3
    mul.l   r1, r3
    sts     macl, r3
    mov.l   r3, @(B_DELTA+2*12+\k*4,r15)
    cmp/pl  r3
    bt      1f
    bra     2f
    add     r3, r12
1:  add     r3, r13
2:
    .endr
    add     r14, r12
    mov     #BOX_NEAR_Z, r0
    extu.b  r0, r0
    shll8   r0
    shll2   r0
    shll2   r0
    shll2   r0                      ! << 14
    cmp/ge  r0, r12
    bt      1f
    bra     .LboxNext
    nop
1:  add     r14, r13
    mov.l   @(R_VIEW_MAX,gbr), r0
    cmp/gt  r13, r0
    bt      2f
    bra     .LboxNext
    nop
2:
    mov.l   @(R_K18,gbr), r0
    dmuls.l r12, r0
    sts     mach, r0
    mov.l   r0, @(B_NEAR,r10)
    ! the other rows at the low corner and along the axes
    mov     #0, r12
    mov     #0, r13
    .irp    k, 0, 1, 2
    mov.l   @(\k*4,r7), r1
    mov.l   @(B_D+\k*8,r15), r2
    mul.l   r1, r2
    mov.l   @(B_D+\k*8+4,r15), r3
    sts     macl, r4
    add     r4, r12
    sub     r2, r3
    mul.l   r1, r3
    sts     macl, r4
    mov.l   r4, @(B_DELTA+0*12+\k*4,r15)
    mov     r7, r0
    add     #16, r0
    mov.l   @(\k*4,r0), r1
    mul.l   r1, r2
    sts     macl, r4
    add     r4, r13
    mul.l   r1, r3
    sts     macl, r4
    mov.l   r4, @(B_DELTA+1*12+\k*4,r15)
    .endr
    ! the corners, one axis on at a time
    mov     #0, r6
    bsr     .LboxCorner
    nop
    .irp    step, 0x01, 0x03, 0x02, 0x06, 0x07, 0x05, 0x04
    .if \step == 0x01
    BOX_STEP add, 0
    .elseif \step == 0x03
    BOX_STEP add, 1
    .elseif \step == 0x02
    BOX_STEP sub, 0
    .elseif \step == 0x06
    BOX_STEP add, 2
    .elseif \step == 0x07
    BOX_STEP add, 0
    .elseif \step == 0x05
    BOX_STEP sub, 1
    .else
    BOX_STEP sub, 0
    .endif
    mov     #\step*4, r6
    bsr     .LboxCorner
    nop
    .endr
    ! its rectangle
    mov.w   .LboxAreaLeast, r0
    mov.l   r0, @(B_AREA,r10)
    mova    .LboxSidesY, r0
    bsr     .LboxPair
    mov     r0, r4
    mov.l   @(B_AREA,r10), r0
    mov.w   .LboxAreaLeast, r1
    cmp/eq  r1, r0
    bf      .LboxFound
    mova    .LboxSidesZ, r0
    bsr     .LboxPair
    mov     r0, r4
    mova    .LboxSidesX, r0
    bsr     .LboxPair
    mov     r0, r4
    mov.l   @(B_AREA,r10), r0
    mov.w   .LboxAreaLeast, r1
    cmp/eq  r1, r0
    bf      .LboxFound
.LboxNextFar:
    bra     .LboxNext
    nop

    ! its place in the list, the smallest's when that's full, unless it's smaller
.LboxFound:
    mov     r0, r5                  ! its area
    mov.l   @(R_BOXES,gbr), r0
    mov     r0, r3                  ! the list
    mov.l   @(R_BOX_COUNT,gbr), r0
    mov     r0, r2
    cmp/eq  #BOXES_SEEN, r0
    bt      1f
    add     #1, r0
    mov.l   r0, @(R_BOX_COUNT,gbr)
    mov     #BOX, r0
    mulu.w  r0, r2
    sts     macl, r0
    bra     .LboxPlace
    add     r0, r3
1:  mov     r3, r6                  ! the smallest so far
    mov.l   @(BOX_AREA,r6), r7
    mov     #BOXES_SEEN-1, r2
2:  add     #BOX, r3
    mov.l   @(BOX_AREA,r3), r1
    cmp/gt  r1, r7
    bf      3f
    mov     r1, r7
    mov     r3, r6
3:  dt      r2
    bf      2b
    mov     r6, r3
    cmp/gt  r7, r5
    bf      .LboxNextFar
.LboxPlace:                         ! r3 its place, r5 its area; a pixel in from each edge
    mov.l   r5, @(BOX_AREA,r3)
    mov.w   .LboxCoord, r4          ! the most
    neg     r4, r5                  ! the least
    .irp    k, 0, 2
    mov.l   @(B_X0+\k*2,r10), r1
    add     #1, r1
    cmp/gt  r5, r1
    bt      1f
    mov     r5, r1
1:  mov     r1, r0
    mov.w   r0, @(\k,r3)
    mov.l   @(B_X1+\k*2,r10), r1
    add     #-1, r1
    cmp/gt  r4, r1
    bf      2f
    mov     r4, r1
2:  mov     r1, r0
    mov.w   r0, @(4+\k,r3)
    .endr
    mov.l   @(B_NEAR,r10), r0
    mov.l   r0, @(BOX_NEAR,r3)
    ! past its sides that face the camera: a flat one's plane alone
    mov.l   @(SOLID_SIDE,r8), r0
    cmp/pz  r0
    bt      4f
    neg     r0, r0
4:  mov     r0, r6                  ! the plane's axis + 1, or 0
    mov     #1, r1
    shll16  r1
    shll8   r1
    shll2   r1
    shll2   r1                      ! no limit: 1 << 28
    neg     r1, r2
    .irp    k, 0, 1, 2
    mov     r2, r4                  ! lo
    mov     r1, r5                  ! hi
    tst     r6, r6
    bt      5f
    mov     r6, r0
    cmp/eq  #\k+1, r0
    bf      7f
5:  mov.l   @(B_D+\k*8,r15), r0
    cmp/pl  r0
    bf      6f
    mov     r0, r4
6:  mov.l   @(B_D+\k*8+4,r15), r0
    cmp/pz  r0
    bt      7f
    mov     r0, r5
7:  mov.l   r4, @(BOX_LO+\k*4,r3)
    mov.l   r5, @(BOX_HI+\k*4,r3)
    .endr
    bra     .LboxNext
    nop

    .align  2
.LboxCoord: .short  BOX_COORD
.LboxAreaLeast: .short  BOX_AREA_LEAST
    .align  2
    ! corners by 4 (bit 0 x high, bit 1 y high, bit 2 z high): low then high side, by axis
.LboxSidesY:    .byte   0, 4, 16, 20, 8, 12, 24, 28
.LboxSidesZ:    .byte   0, 4, 8, 12, 16, 20, 24, 28
.LboxSidesX:    .byte   0, 8, 16, 24, 4, 12, 20, 28

    .align  2

.LboxesDone:
    add     #120, r15
    add     #64, r15
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
    .endif

! rooms.cpp roomsQueue, placeRooms and roomBegin's caustics: each room listed
! with vertices (at most ROOM_JOBS), its vertices placed from MASTER_VERTICES
! up, as a FaceRoom (faces.h) for stages 2 and 3; the caustics' table once, if
! a room is wet. Rooms dropped (RoomSeen.dropped) have none; rooms wanting more
! vertices than there are, those showing least are dropped first (rooms.cpp
! roomsMakeWay).
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
    ! on the stack: the vertices wanted past the top
    mov     r8, r2
    mov     r9, r3
    mov     r11, r4
    mov.l   @(R_LEVEL,gbr), r0
    mov     r0, r5
    mov     #-16, r6
.Lwanted:
    mov.w   @(2,r2), r0
    tst     r0, r0
    bf      .LwantedNext
    mov.w   @r2, r0
    extu.w  r0, r1
    shll2   r1
    shll2   r1
    add     r5, r1
    mov.w   @(L_VERTICES,r1), r0
    extu.w  r0, r1
    add     #15, r1
    and     r6, r1
    add     r1, r4
.LwantedNext:
    dt      r3
    bf/s    .Lwanted
    add     #SEEN, r2
    mov.l   @(R_VERTICES_TOP,gbr), r0
    sub     r0, r4
    cmp/pl  r4
    bf      .LwantedFit
    mov     r4, r5
    stc     gbr, r4
    sts.l   pr, @-r15
    mov.l   .LmakeWay, r0
    jsr     @r0
    nop
    lds.l   @r15+, pr
.LwantedFit:
    bra     .Ljob
    mov     #0, r14                 ! any wet
    .align  2
.LmakeWay:  .long   _roomsMakeWay
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
    mov.w   @(2,r8), r0
    tst     r0, r0
    bf      .Lnojob
    mov     r12, r0
    cmp/eq  #ROOM_JOBS, r0
    bt      .Lnojob
    add     #1, r12
    mov     r5, r4
    add     #15, r4
    mov     #-16, r0
    and     r0, r4                  ! its vertices' size
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

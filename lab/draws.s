! Stage 1 the new way (draws.h): the draw list, exactly the game's way's
! (gamedraws.cpp): the renderer's side of each of the frame's calls after the
! walk, as openlara draw.h and saturn/render.cpp take them. A model's frames
! (ItemObj::getFrames), its sphere in view (sphereIsVisible_c), the pose's
! item, extras and light, and a draw a mesh; a shadow's draw; a static's
! sphere, place and draw; a sprite's and a rectangle's face, made. Each draw
! takes its vertices after the one before.
!
! void drawsAsm(DrawsContext* ctx)
!
! An arithmetic shift right by n is the high longword of the value times
! 2^(32 - n) (DMULS.L), exact for any longword as C's; a frame's divide on
! the SH-2's divider (libgcc's __sdivsi3 for a rate of 0, as the game).
!
! Registers through the calls: GBR the context, r14 the call, r13 calls
! left, r12 the next draw (DrawScene), r11 the next draw's first vertex.
! The handlers take r0..r10.
!
! DrawsContext (draws.h) at GBR; CallRecord, ItemRecord, GameItem, GameAnim,
! GameModel, GameStaticMesh, GameLara, DrawScene, PoseItem, PoseLight,
! PoseStatic, PoseShadow: scene.h; Sprite, Face, Vertex: game.h.

    .equ    D_CALLS,        0
    .equ    D_CALL_COUNT,   4
    .equ    D_ITEMS,        8
    .equ    D_ANIMS,        12
    .equ    D_FRAMES,       16      ! bytes: AFRM - 2 AFRM_BASE
    .equ    D_MODELS,       20
    .equ    D_MESH_INDEX,   24
    .equ    D_MODEL_NODES,  28
    .equ    D_NODES,        32
    .equ    D_STATICS,      36
    .equ    D_ROOM_LIGHTS,  40
    .equ    D_LEVEL,        44
    .equ    D_MESHES,       48
    .equ    D_MESH_GEOMETRY, 52
    .equ    D_SPRITES,      56
    .equ    D_CAMERA,       60
    .equ    D_VIEW,         72      ! int32 e00 .. e23
    .equ    D_DIV,          76
    .equ    D_VIEWPORT,     80      ! x0 y0 x1 y1
    .equ    D_BASE,         96
    .equ    D_DRAWS,        100
    .equ    D_DRAWS_END,    104
    .equ    D_POSES,        108
    .equ    D_LIGHTS,       112
    .equ    D_ITEM_COUNT,   116
    .equ    D_PLACED,       120
    .equ    D_STATIC_COUNT, 124
    .equ    D_FLOORS,       128
    .equ    D_SHADOW_COUNT, 132
    .equ    D_FACES,        136
    .equ    D_VERTICES,     140
    .equ    D_FACE_COUNT,   144
    .equ    D_K20,          148     ! 2^20: >> 12
    .equ    D_K18,          152     ! 2^18: >> 14
    .equ    D_FRAME_A,      156     ! a model's frames, as getFrames gives them
    .equ    D_FRAME_B,      160
    .equ    D_DELTA,        164
    .equ    D_RATE,         168
    .equ    D_FRAME_SIZE,   172     ! bytes
    .equ    D_VIEW_MIN,     176     ! VIEW_MIN_F
    .equ    D_VIEW_MAX,     180     ! VIEW_MAX_F
    .equ    D_DRAW_COUNT,   184
    .equ    D_HINT_ITEMS,   188     ! the frame's hints' things' bits (index & 31): items'
    .equ    D_HINT_STATICS, 192     ! ... statics'
    .equ    D_HINTED,       196     ! the next HintedCall
    .equ    D_HINTED_COUNT, 200
    .equ    D_HINT_OPEN,    204     ! the call's, at D_HINTED, is open

    .equ    CALL_BYTES,     32      ! CallRecord
    .equ    C_INDEX,        2
    .equ    C_FLAGS,        4
    .equ    C_ARGS,         8
    .equ    CALL_VIEWPORT,  1
    .equ    CALL_MODEL,     2
    .equ    CALL_STATIC,    3
    .equ    CALL_SHADOW,    4
    .equ    CALL_SPRITE,    5
    .equ    CALL_RECT,      6
    .equ    IT_POS,         8       ! ItemRecord: the item (GameItem), its room, its extra
    .equ    IT_ANGLE,       20
    .equ    IT_ANIM,        32
    .equ    IT_TYPE,        52
    .equ    IT_INTENSITY,   53
    .equ    IT_VISIBLE,     60
    .equ    IT_EXTRA,       64
    .equ    IT_ROOM,        76
    .equ    IT_INDEX,       78
    .equ    HINTED_BYTES,   8       ! HintedCall: kind, owner, index, first, count
    .equ    A_RATE,         4       ! GameAnim
    .equ    A_BEGIN,        16
    .equ    A_END,          18
    .equ    X_WEAPON,       10      ! GameLara
    .equ    X_HEAD,         12
    .equ    X_TORSO,        18
    .equ    X_ARMS,         24
    .equ    ARM_BYTES,      32
    .equ    ARM_ANIM,       12
    .equ    ARM_FRAME,      14
    .equ    ARM_BASIS,      29
    .equ    DRAW_BYTES,     24      ! DrawScene: type, inside, mesh, flags, base, first, count, bucket
    .equ    POSE_BYTES,     104     ! PoseItem: lara, count, firstDraw, nodes, frameA, frameB, delta,
                                    ! rate, visibleMask, pos; from +40: angle, extraCount, extra,
                                    ! torso, head, weapon, arms (PoseArm: frame, rate, angle, useBasis)
    .equ    LIGHT_BYTES,    12      ! PoseLight: roomX, roomZ, intensity, ambient, lights, first
    .equ    STATIC_BYTES,   20      ! PoseStatic: draw, quadrant, intensity, x y z
    .equ    DRAW_MESH,      1
    .equ    DRAW_SHADOW,    2
    .equ    DRAW_COPY,      3

    .text
    .align  2

! v >> n, arithmetic: k = 2^(32 - n)
    .macro  SAR v, k
    dmuls.l \v, \k
    sts     mach, \v
    .endm

! a row of the view (at r3, from its entry at off) times r4 r5 r6 into r;
! r0 and t
    .macro  ROW off, r, t
    mov.l   @(\off,r3), r0
    mul.l   r0, r4
    mov.l   @(\off+4,r3), r0
    sts     macl, \r
    mul.l   r0, r5
    mov.l   @(\off+8,r3), r0
    sts     macl, \t
    mul.l   r0, r6
    add     \t, \r
    sts     macl, \t
    add     \t, \r
    .endm

! dividend / divisor (C's int division, toward 0) into the dividend's
! register; r0 r6, and where libgcc divides the rest kept
    .macro  DIVIDE dividend, divisor
    mov     \divisor, r0
    tst     r0, r0
    bt      1f
    cmp/eq  #-1, r0
    bt      1f
    mov     #-1, r6
    shll8   r6                  ! DVSR at 0xFFFFFF00, DVDNT at +4
    mov.l   \divisor, @r6
    mov.l   \dividend, @(4,r6)
    bra     2f
    mov.l   @(4,r6), \dividend
1:  sts.l   pr, @-r15
    mov.l   r1, @-r15
    mov.l   r2, @-r15
    mov.l   r3, @-r15
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   r7, @-r15
    mov     \dividend, r4
    mov     r0, r5
    mov.l   .Lsdiv_\@, r0
    jsr     @r0
    nop
    mov     r0, r6
    mov.l   @r15+, r7
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    mov.l   @r15+, r3
    mov.l   @r15+, r2
    mov.l   @r15+, r1
    lds.l   @r15+, pr
    bra     2f
    mov     r6, \dividend
    .align  2
.Lsdiv_\@:  .long   ___sdivsi3
2:
    .endm

! r clamped to the VDP1's coordinates, -1024 .. 1023; r0
    .macro  VDP1COORD r
    mov     #-4, r0
    shll8   r0
    cmp/gt  \r, r0
    bf      1f
    mov     r0, \r
1:  not     r0, r0
    cmp/gt  r0, \r
    bf      2f
    mov     r0, \r
2:
    .endm

! a draw at r12: type r4, mesh r5, faces' flags r6, its vertices r7 (the
! next draw's first after); r0
.Ldraw:
    mov     r4, r0
    mov.b   r0, @r12
    mov     #0, r0
    mov.b   r0, @(1,r12)
    mov     r5, r0
    mov.w   r0, @(2,r12)
    mov.l   r6, @(4,r12)
    mov.l   r11, @(8,r12)
    mov     #0, r0
    mov.l   r0, @(12,r12)
    mov.l   r0, @(16,r12)
    mov.l   r0, @(20,r12)
    add     r7, r11
    add     #DRAW_BYTES, r12
    mov.l   @(D_DRAW_COUNT,gbr), r0
    add     #1, r0
    rts
    mov.l   r0, @(D_DRAW_COUNT,gbr)

! mesh r5 (MESH's), the faces' flags r6: its draw, as many vertices as the
! mesh has; r0 r4 r7
.Lmesh:
    mov.l   @(D_MESHES,gbr), r0
    mov     r5, r7
    shll2   r7
    mov.l   @(r0,r7), r7
    mov.l   @(D_MESH_GEOMETRY,gbr), r0
    add     r0, r7
    mov.b   @(10,r7), r0
    extu.b  r0, r7
    bra     .Ldraw
    mov     #DRAW_MESH, r4

! a copy's face (flags r5, bucket r6) and its draw: its two vertices at r7
! (filled after); r0 .. r6
.Lcopy:
    sts.l   pr, @-r15
    mov.l   @(D_FACE_COUNT,gbr), r0
    mov     r0, r1
    add     #1, r0
    mov.l   r0, @(D_FACE_COUNT,gbr)
    mov     r1, r2
    shll2   r2
    shll2   r2                      ! 16 bytes a face, and its 2 vertices of 8
    mov.l   @(D_FACES,gbr), r0
    add     r2, r0
    mov.l   r5, @r0
    mov     #0, r3
    mov.l   r3, @(4,r0)
    mov.l   r3, @(8,r0)
    mov.l   r3, @(12,r0)
    mov.l   @(D_VERTICES,gbr), r0
    add     r2, r0
    mov.l   r0, @-r15               ! its vertices
    mov.l   r6, @-r15
    mov.l   r1, @-r15
    mov     #DRAW_COPY, r4
    mov     #0, r5
    mov     #0, r6
    bsr     .Ldraw
    mov     #2, r7
    mov.l   @r15+, r1
    mov.l   @r15+, r6
    mov     r12, r0
    add     #-DRAW_BYTES, r0
    mov.l   r1, @(12,r0)            ! first
    mov     #1, r1
    mov.l   r1, @(16,r0)            ! count
    mov.l   r6, @(20,r0)            ! bucket
    mov.l   @r15+, r7
    lds.l   @r15+, pr
    rts
    nop

! sphereIsVisible_c: centre r4 r5 r6, radius r7 from the camera, under the
! viewport -> T; r0 .. r7
.Lsphere:
    ! within it: |x|, |y|, |z| < r
    mov     r4, r0
    cmp/pz  r0
    bt      1f
    neg     r0, r0
1:  cmp/gt  r0, r7
    bf      2f
    mov     r5, r0
    cmp/pz  r0
    bt      1f
    neg     r0, r0
1:  cmp/gt  r0, r7
    bf      2f
    mov     r6, r0
    cmp/pz  r0
    bt      1f
    neg     r0, r0
1:  cmp/gt  r0, r7
    bf      2f
    rts
    sett
2:  mov.l   @(D_VIEW,gbr), r0
    mov     r0, r3
    ROW     32, r2, r1              ! z
    cmp/pz  r2
    bt      3f
    rts
    clrt
3:  mov.l   r8, @-r15
    ROW     0, r1, r8               ! x
    ROW     16, r4, r8              ! y (the centre's x used first)
    mov.l   @r15+, r8
    mov.l   @(D_K18,gbr), r0
    SAR     r1, r0
    SAR     r4, r0
    SAR     r2, r0
    ! the divide table's step: z >> 4, at most its last
    shar    r2
    shar    r2
    shar    r2
    shar    r2
    mov.w   .Ldiv_last, r0
    cmp/gt  r0, r2
    bf      4f
    mov     r0, r2
4:  shll    r2
    mov.l   @(D_DIV,gbr), r0
    mov.w   @(r0,r2), r2
    extu.w  r2, r2                  ! d
    mov.l   @(D_K20,gbr), r0
    mul.l   r2, r1
    sts     macl, r1
    SAR     r1, r0
    mul.l   r2, r4
    sts     macl, r4
    SAR     r4, r0
    mul.l   r2, r7
    sts     macl, r7
    SAR     r7, r0
    ! the box about it on the frame against the viewport
    mov     #88, r0
    shll    r0
    add     r0, r1                  ! x + FRAME_WIDTH / 2
    mov     #112, r0
    add     r0, r4                  ! y + FRAME_HEIGHT / 2
    mov     r1, r2
    sub     r7, r2                  ! rMinX
    mov.l   @(D_VIEWPORT+8,gbr), r0
    cmp/gt  r0, r2
    bt      5f
    mov     r1, r2
    add     r7, r2                  ! rMaxX
    mov.l   @(D_VIEWPORT,gbr), r0
    cmp/gt  r2, r0
    bt      5f
    mov     r4, r2
    sub     r7, r2                  ! rMinY
    mov.l   @(D_VIEWPORT+12,gbr), r0
    cmp/gt  r0, r2
    bt      5f
    mov     r4, r2
    add     r7, r2                  ! rMaxY
    mov.l   @(D_VIEWPORT+4,gbr), r0
    cmp/gt  r2, r0
    bt      5f
    rts
    sett
5:  rts
    clrt

    .align  1
.Ldiv_last: .word   1280
    .align  2

! ItemObj::getFrames for the item at r10, its model at r9: D_FRAME_A,
! D_FRAME_B, D_DELTA, D_RATE, D_FRAME_SIZE; r0 .. r7
.Lframes:
    sts.l   pr, @-r15
    mov.b   @(1,r9), r0             ! the model's nodes
    shll2   r0
    add     #20, r0
    mov.l   r0, @(D_FRAME_SIZE,gbr) ! (sizeof(AnimFrame) >> 1) + (count << 1), in bytes
    mov     r10, r1
    add     #IT_ANIM, r1
    mov.w   @r1+, r2
    extu.w  r2, r2
    shll2   r2
    shll2   r2
    shll    r2
    mov.l   @(D_ANIMS,gbr), r0
    add     r0, r2                  ! its anim
    mov.w   @r1, r3
    extu.w  r3, r3                  ! its frame
    mov.l   @r2, r4
    mov     #-2, r0
    and     r0, r4
    mov.l   @(D_FRAMES,gbr), r0
    add     r0, r4                  ! the anim's first frame
    mov.w   @(A_BEGIN,r2), r0
    extu.w  r0, r5                  ! frameBegin
    mov.w   @(A_END,r2), r0
    extu.w  r0, r6                  ! frameEnd
    cmp/eq  r5, r6
    bf      1f
    ! a still: its first frame, rate 1
    mov     r4, r0
    mov.l   r0, @(D_FRAME_A,gbr)
    mov.l   r0, @(D_FRAME_B,gbr)
    mov     #0, r0
    mov.l   r0, @(D_DELTA,gbr)
    mov     #1, r0
    lds.l   @r15+, pr
    rts
    mov.l   r0, @(D_RATE,gbr)
1:  mov.b   @(A_RATE,r2), r0
    extu.b  r0, r7                  ! the rate
    sub     r5, r3                  ! frame - frameBegin
    mov     r7, r0
    cmp/eq  #1, r0
    bf      2f
    mov     r3, r1                  ! indexA
    bra     5f
    mov     #0, r5                  ! delta
2:  cmp/eq  #2, r0
    bf      3f
    mov     r3, r1
    shar    r1
    mov     r1, r5
    add     r5, r5
    mov     r3, r0
    sub     r5, r0
    bra     5f
    mov     r0, r5
3:  cmp/eq  #4, r0
    bf      4f
    mov     r3, r1
    shar    r1
    shar    r1
    mov     r1, r5
    shll2   r5
    mov     r3, r0
    sub     r5, r0
    bra     5f
    mov     r0, r5
4:  mov     r3, r1
    DIVIDE  r1, r7
    mul.l   r7, r1
    sts     macl, r5
    mov     r3, r0
    sub     r5, r0
    mov     r0, r5
    mov.w   @(A_END,r2), r0         ! frameEnd again (r6 the divide's)
    extu.w  r0, r6
5:  ! r1 indexA, r5 delta, r6 frameEnd, r7 rate, r4 the anim's first frame
    mov     r1, r2
    add     #1, r2                  ! indexB
    mul.l   r7, r2
    sts     macl, r0
    cmp/ge  r6, r0
    bf      6f
    mov     r1, r2                  ! indexB = indexA
6:  mov.l   @(D_FRAME_SIZE,gbr), r0
    mul.l   r0, r1
    sts     macl, r3
    add     r4, r3                  ! frameA
    mul.l   r0, r2
    sts     macl, r0
    add     r4, r0                  ! frameB
    mov.l   r0, @(D_FRAME_B,gbr)
    cmp/eq  r0, r3
    mov     r3, r0
    bt/s    7f                      ! frameA == frameB: no lerp
    mov.l   r0, @(D_FRAME_A,gbr)
    tst     r5, r5
    bt      7f
    mul.l   r7, r2
    sts     macl, r2                ! indexB * rate
    cmp/gt  r6, r2
    bf      8f
    sub     r6, r2
    bra     8f
    sub     r2, r7
7:  mov     #0, r5
8:  mov     r5, r0
    mov.l   r0, @(D_DELTA,gbr)
    mov     r7, r0
    lds.l   @r15+, pr
    rts
    mov.l   r0, @(D_RATE,gbr)

! drawModel: the call at r14
.Lmodel:
    sts.l   pr, @-r15
    mov.w   @(C_INDEX,r14), r0
    extu.w  r0, r10
    mov     #77, r0
    shll2   r0                      ! an ItemRecord's 308 bytes
    mulu.w  r0, r10
    mov.l   @(D_ITEMS,gbr), r0
    sts     macl, r10
    add     r0, r10                 ! the item's record
    mov     r10, r0
    add     #IT_TYPE, r0
    mov.b   @r0, r0
    extu.b  r0, r8                  ! its type
    mov     r8, r9
    shll2   r9
    shll    r9
    mov.l   @(D_MODELS,gbr), r0
    bsr     .Lframes
    add     r0, r9                  ! its model

    ! the sphere about frame A's box, turned by the item's quarter
    mov.l   @(D_FRAME_A,gbr), r0
    mov     r0, r3
    mov.w   @r3, r1                 ! minX
    mov.w   @(2,r3), r0             ! maxX
    mov     r0, r4
    add     r1, r4
    shar    r4                      ! sx
    mov     r0, r7
    sub     r1, r7                  ! sr
    mov.w   @(4,r3), r0
    mov     r0, r1
    mov.w   @(6,r3), r0
    mov     r0, r5
    add     r1, r5
    shar    r5                      ! sy
    sub     r1, r0
    cmp/gt  r7, r0
    bf      1f
    mov     r0, r7
1:  mov.w   @(8,r3), r0
    mov     r0, r1
    mov.w   @(10,r3), r0
    mov     r0, r6
    add     r1, r6
    shar    r6                      ! sz
    sub     r1, r0
    cmp/gt  r7, r0
    bf      2f
    mov     r0, r7
2:  mov     r7, r0
    shar    r0
    shar    r7
    shar    r7
    add     r0, r7                  ! (sr >> 1) + (sr >> 2)
    mov.w   @(IT_ANGLE+2,r10), r0
    mov     #32, r1
    shll8   r1                      ! ANGLE_45
    add     r1, r0
    extu.w  r0, r0
    shlr8   r0
    shlr2   r0
    shlr2   r0
    shlr2   r0                      ! the quadrant
    cmp/eq  #1, r0
    bf      3f
    mov     r4, r1
    mov     r6, r4
    bra     6f
    neg     r1, r6
3:  cmp/eq  #2, r0
    bf      4f
    neg     r4, r4
    bra     6f
    neg     r6, r6
4:  cmp/eq  #3, r0
    bf      6f
    mov     r4, r1
    neg     r6, r4
    mov     r1, r6
6:  mov.l   @(IT_POS,r10), r0
    add     r0, r4
    mov.l   @(D_CAMERA,gbr), r0
    sub     r0, r4
    mov.l   @(IT_POS+4,r10), r0
    add     r0, r5
    mov.l   @(D_CAMERA+4,gbr), r0
    sub     r0, r5
    mov.l   @(IT_POS+8,r10), r0
    add     r0, r6
    mov.l   @(D_CAMERA+8,gbr), r0
    bsr     .Lsphere
    sub     r0, r6
    bt      .Lshown
    lds.l   @r15+, pr
    rts
    nop

.Lshown:
    ! the pose's item at r3
    mov.l   @(D_ITEM_COUNT,gbr), r0
    mov     r0, r1
    add     #1, r0
    mov.l   r0, @(D_ITEM_COUNT,gbr)
    mov     #POSE_BYTES, r3
    mulu.w  r1, r3
    mov.l   @(D_POSES,gbr), r0
    sts     macl, r3
    add     r0, r3
    ! lara, count, first draw, nodes, frames, visible mask, place
    mov     r8, r0
    tst     r0, r0
    movt    r0
    mov.b   r0, @r3
    mov.b   @(1,r9), r0
    mov.b   r0, @(1,r3)
    mov.l   @(D_DRAW_COUNT,gbr), r0
    mov.w   r0, @(2,r3)
    mov     r8, r1
    shll2   r1
    mov.l   @(D_MODEL_NODES,gbr), r0
    mov.l   @(r0,r1), r1
    mov.l   r1, @(4,r3)
    mov.l   @(D_FRAME_A,gbr), r0
    mov.l   r0, @(8,r3)
    mov.l   @(D_FRAME_B,gbr), r0
    mov.l   r0, @(12,r3)
    mov.l   @(D_DELTA,gbr), r0
    mov.l   r0, @(16,r3)
    mov.l   @(D_RATE,gbr), r0
    mov.l   r0, @(20,r3)
    mov.l   @(IT_VISIBLE,r10), r0
    mov.l   r0, @(24,r3)
    mov.l   @(IT_POS,r10), r0
    mov.l   r0, @(28,r3)
    mov.l   @(IT_POS+4,r10), r0
    mov.l   r0, @(32,r3)
    mov.l   @(IT_POS+8,r10), r0
    mov.l   r0, @(36,r3)
    ! from +40 (r1): the angle, then nothing but what the item has
    mov     r3, r1
    add     #40, r1
    mov.w   @(IT_ANGLE,r10), r0
    mov.w   r0, @r1
    mov.w   @(IT_ANGLE+2,r10), r0
    mov.w   r0, @(2,r1)
    mov.w   @(IT_ANGLE+4,r10), r0
    mov.w   r0, @(4,r1)
    mov     #0, r0
    mov.w   r0, @(6,r1)             ! extraCount
    mov     r1, r4
    add     #8, r4
    mov     #14, r5
1:  mov.l   r0, @r4                 ! extra .. arms: 0
    dt      r5
    bf/s    1b
    add     #4, r4
    mov     #-1, r0
    mov.l   r0, @(32,r1)            ! the arms' frames: none
    mov.l   r0, @(48,r1)
    mov     r10, r4
    add     #IT_EXTRA, r4
    mov.l   @r4, r4                 ! the extra (the record's)
    mov     r8, r0
    tst     r0, r0
    bt      .Llara
    tst     r4, r4
    bt      .Llight
    ! an enemy's angles, as many as its nodes' ROT flags take
    mov.l   @(4,r3), r5
    mov.l   @(D_NODES,gbr), r0
    add     r0, r5                  ! its nodes
    mov.b   @(1,r9), r0
    mov     r0, r6
    add     #-1, r6
    mov     #0, r7
    cmp/pl  r6
    bf      3f
2:  mov.w   @(6,r5), r0
    shlr2   r0
    and     #7, r0
    mov     r0, r2
    mova    .Lbits, r0
    mov.b   @(r0,r2), r0
    add     r0, r7
    dt      r6
    bf/s    2b
    add     #8, r5
3:  mov     r7, r0
    mov.w   r0, @(6,r1)             ! extraCount
    tst     r7, r7
    bt      .Llight
    mov     r1, r5
    add     #8, r5
4:  mov.w   @r4+, r0
    mov.w   r0, @r5
    dt      r7
    bf/s    4b
    add     #2, r5
    bra     .Llight
    nop

    .align  2
.Lbits: .byte   0, 1, 1, 2, 1, 2, 2, 3

.Llara:
    ! her torso, head, weapon; each arm's frame (the arm's anim at the
    ! arm's frame), rate, angle, basis
    mov.w   @(X_TORSO,r4), r0
    mov.w   r0, @(16,r1)
    mov.w   @(X_TORSO+2,r4), r0
    mov.w   r0, @(18,r1)
    mov.w   @(X_TORSO+4,r4), r0
    mov.w   r0, @(20,r1)
    mov.w   @(X_HEAD,r4), r0
    mov.w   r0, @(22,r1)
    mov.w   @(X_HEAD+2,r4), r0
    mov.w   r0, @(24,r1)
    mov.w   @(X_HEAD+4,r4), r0
    mov.w   r0, @(26,r1)
    mov.b   @(X_WEAPON,r4), r0
    mov     r1, r5
    add     #28, r5
    mov.b   r0, @r5
    mov     r4, r5
    add     #X_ARMS, r5             ! the arm
    mov     r1, r6
    add     #32, r6                 ! its PoseArm
    mov     #2, r7
5:  mov.w   @(ARM_ANIM,r5), r0
    extu.w  r0, r0
    tst     r0, r0
    bt      6f
    shll2   r0
    shll2   r0
    shll    r0
    mov     r0, r2
    mov.l   @(D_ANIMS,gbr), r0
    add     r0, r2                  ! its anim
    mov.b   @(A_RATE,r2), r0
    extu.b  r0, r0
    mov.l   r0, @(4,r6)             ! its rate
    mov.l   @r2, r2
    mov     #-2, r0
    and     r0, r2
    mov.l   @(D_FRAMES,gbr), r0
    add     r0, r2                  ! the anim's first frame
    mov.w   @(ARM_FRAME,r5), r0
    extu.w  r0, r3
    mov.l   @(D_FRAME_SIZE,gbr), r0
    mul.l   r0, r3
    sts     macl, r0
    add     r0, r2
    mov.l   r2, @r6                 ! its frame
6:  mov.w   @r5, r0
    mov.w   r0, @(8,r6)
    mov.w   @(2,r5), r0
    mov.w   r0, @(10,r6)
    mov.w   @(4,r5), r0
    mov.w   r0, @(12,r6)
    mov     r5, r0
    add     #ARM_BASIS, r0
    mov.b   @r0, r0
    mov.b   r0, @(14,r6)
    add     #ARM_BYTES, r5
    dt      r7
    bf/s    5b
    add     #16, r6

.Llight:
    ! the light: its room's place, its intensity, the room's ambient and
    ! lights
    mov.l   @(D_ITEM_COUNT,gbr), r0
    add     #-1, r0
    mov     #LIGHT_BYTES, r2
    mulu.w  r0, r2
    mov.l   @(D_LIGHTS,gbr), r0
    sts     macl, r2
    add     r0, r2                  ! its PoseLight
    mov     r10, r0
    add     #IT_ROOM, r0
    mov.w   @r0, r0
    extu.w  r0, r4                  ! its room
    mov     r4, r5
    shll2   r5
    shll2   r5
    mov.l   @(D_LEVEL,gbr), r0
    add     r0, r5                  ! its RoomLevel
    mov.w   @r5, r0
    mov.w   r0, @r2
    mov.w   @(2,r5), r0
    mov.w   r0, @(2,r2)
    mov     r10, r0
    add     #IT_INTENSITY, r0
    mov.b   @r0, r0
    mov.b   r0, @(4,r2)
    shll2   r4
    shll    r4
    mov.l   @(D_ROOM_LIGHTS,gbr), r0
    add     r0, r4                  ! its RoomLights
    mov.b   @r4, r0
    mov.b   r0, @(5,r2)
    mov.b   @(1,r4), r0
    mov.b   r0, @(6,r2)
    mov     #0, r0
    mov.b   r0, @(7,r2)
    mov.l   @(4,r4), r0
    mov.l   r0, @(8,r2)

    ! a draw a mesh: Lara's from her joints' table, every one; another's,
    ! each its visible mask shows
    mov.l   @(C_FLAGS,r14), r6
    mov.b   @(1,r9), r0
    mov     r0, r9                  ! nodes left
    mov     r8, r0
    tst     r0, r0
    bf      .Lnodes
    mov     r10, r0
    add     #IT_EXTRA, r0
    mov.l   @r0, r10
    mov     #98, r0
    shll    r0
    add     r0, r10                 ! her joints' meshes (GameLara +196)
    cmp/pl  r9
    bf      .Lmodeled
7:  mov.w   @r10+, r0
    extu.w  r0, r5
    add     r5, r5
    mov.l   @(D_MESH_INDEX,gbr), r0
    mov.w   @(r0,r5), r5
    bsr     .Lmesh
    extu.w  r5, r5
    dt      r9
    bf      7b
    bra     .Lmodeled
    nop
.Lnodes:
    mov.l   @(IT_VISIBLE,r10), r8   ! the visible mask
    mov     r10, r0
    add     #IT_TYPE, r0
    mov.b   @r0, r0
    extu.b  r0, r0
    shll2   r0
    shll    r0
    mov     r0, r10
    mov.l   @(D_MODELS,gbr), r0
    add     r0, r10                 ! its model
    mov.w   @(2,r10), r0            ! its first mesh
    extu.w  r0, r10
    add     r10, r10
    mov.l   @(D_MESH_INDEX,gbr), r0
    add     r0, r10                 ! its meshes' indices in MESH
    cmp/pl  r9
    bf      .Lmodeled
8:  shlr    r8                      ! T: this node shown
    bf/s    9f
    mov.w   @r10+, r5
    bsr     .Lmesh
    extu.w  r5, r5
9:  dt      r9
    bf      8b
.Lmodeled:
    lds.l   @r15+, pr
    rts
    nop

! drawShadow, the floor given: the call at r14
.Lshadow:
    mov.l   @(C_ARGS,r14), r1       ! the floor
    mov     #-127, r0
    shll8   r0                      ! WALL
    cmp/eq  r0, r1
    bf      1f
    rts
    nop
1:  sts.l   pr, @-r15
    mov.l   @(D_SHADOW_COUNT,gbr), r0
    mov     r0, r2
    add     #1, r0
    mov.l   r0, @(D_SHADOW_COUNT,gbr)
    shll2   r2
    shll    r2
    mov.l   @(D_FLOORS,gbr), r0
    add     r0, r2                  ! its PoseShadow
    mov.l   @(D_DRAW_COUNT,gbr), r0
    mov.w   r0, @r2
    mov.l   @(D_ITEM_COUNT,gbr), r0
    add     #-1, r0
    mov.w   r0, @(2,r2)
    mov.l   r1, @(4,r2)
    mov     #DRAW_SHADOW, r4
    mov     #0, r5
    mov.l   @(C_FLAGS,r14), r6
    bsr     .Ldraw
    mov     #8, r7
    lds.l   @r15+, pr
    rts
    nop

! drawRoomStatic: the call at r14 (its RoomMesh, place from the camera)
.Lstatic:
    sts.l   pr, @-r15
    mov.l   @(C_ARGS+4,r14), r1     ! zf
    mov     r1, r0
    and     #0x3F, r0
    mov     #40, r2
    mulu.w  r0, r2
    mov.l   @(D_STATICS,gbr), r0
    sts     macl, r2
    add     r0, r2                  ! its StaticMesh
    mov.l   @(4,r2), r0
    tst     #2, r0                  ! STATIC_MESH_FLAG_VISIBLE
    bf      1f
    lds.l   @r15+, pr
    rts
    nop
1:  mov.l   @(C_ARGS,r14), r0       ! xy
    swap.w  r0, r8
    exts.w  r8, r8
    exts.w  r0, r9
    swap.w  r1, r10
    exts.w  r10, r10
    mov.l   @(C_ARGS+8,r14), r0
    add     r0, r8                  ! px
    mov.l   @(C_ARGS+12,r14), r0
    add     r0, r9                  ! py
    mov.l   @(C_ARGS+16,r14), r0
    add     r0, r10                 ! pz
    mov.l   @(8,r2), r0             ! its visible sphere
    swap.w  r0, r4
    exts.w  r4, r4
    exts.w  r0, r5
    mov.l   @(12,r2), r0
    swap.w  r0, r6
    exts.w  r6, r6
    exts.w  r0, r7
    mov     r1, r0
    shlr2   r0
    shlr2   r0
    shlr2   r0
    and     #3, r0                  ! its quarter
    cmp/eq  #0, r0
    bf      2f
    neg     r4, r4
    bra     4f
    neg     r6, r6
2:  cmp/eq  #1, r0
    bf      3f
    mov     r4, r1
    neg     r6, r4
    bra     4f
    mov     r1, r6
3:  cmp/eq  #3, r0
    bf      4f
    mov     r6, r1
    neg     r4, r6
    mov     r1, r4
4:  add     r8, r4
    add     r9, r5
    bsr     .Lsphere
    add     r10, r6
    bt      5f
    lds.l   @r15+, pr
    rts
    nop
5:  mov.l   @(D_STATIC_COUNT,gbr), r0
    mov     r0, r2
    add     #1, r0
    mov.l   r0, @(D_STATIC_COUNT,gbr)
    mov     #STATIC_BYTES, r3
    mulu.w  r2, r3
    mov.l   @(D_PLACED,gbr), r0
    sts     macl, r3
    add     r0, r3                  ! its PoseStatic
    mov.l   @(D_DRAW_COUNT,gbr), r0
    mov.w   r0, @r3
    mov.l   @(C_ARGS+4,r14), r1     ! zf
    mov     r1, r0
    shlr2   r0
    shlr2   r0
    shlr2   r0
    and     #3, r0
    mov.b   r0, @(2,r3)
    mov     #0, r0
    mov.b   r0, @(3,r3)
    mov     r1, r0
    shlr8   r0
    extu.b  r0, r0
    shll2   r0
    shll2   r0
    shll    r0
    mov.l   r0, @(4,r3)             ! STATIC_MESH_INTENSITY
    mov.l   @(D_CAMERA,gbr), r0
    add     r0, r8
    mov.l   r8, @(8,r3)
    mov.l   @(D_CAMERA+4,gbr), r0
    add     r0, r9
    mov.l   r9, @(12,r3)
    mov.l   @(D_CAMERA+8,gbr), r0
    add     r0, r10
    mov.l   r10, @(16,r3)
    mov     r1, r0
    and     #0x3F, r0
    mov     #40, r2
    mulu.w  r0, r2
    mov.l   @(D_STATICS,gbr), r0
    sts     macl, r2
    add     r0, r2
    mov.w   @(2,r2), r0             ! its mesh in level.meshes
    extu.w  r0, r0
    add     r0, r0
    mov     r0, r5
    mov.l   @(D_MESH_INDEX,gbr), r0
    mov.w   @(r0,r5), r5
    extu.w  r5, r5
    bsr     .Lmesh
    mov.l   @(C_FLAGS,r14), r6
    lds.l   @r15+, pr
    rts
    nop

! renderSprite: the call at r14 (its place, shade, sprite)
.Lsprite:
    sts.l   pr, @-r15
    mov.l   @(C_ARGS,r14), r4
    mov.l   @(D_CAMERA,gbr), r0
    sub     r0, r4
    mov.l   @(C_ARGS+4,r14), r5
    mov.l   @(D_CAMERA+4,gbr), r0
    sub     r0, r5
    mov.l   @(C_ARGS+8,r14), r6
    mov.l   @(D_CAMERA+8,gbr), r0
    sub     r0, r6
    mov.l   @(D_VIEW,gbr), r0
    mov     r0, r3
    ROW     32, r2, r1              ! z
    mov.l   @(D_VIEW_MIN,gbr), r0
    cmp/ge  r0, r2
    bf      1f
    mov.l   @(D_VIEW_MAX,gbr), r0
    cmp/ge  r0, r2
    bf      2f
1:  bra     .Lsprited
    nop
2:  ROW     0, r1, r8               ! x
    ROW     16, r7, r8              ! y
    mov.l   @(D_K18,gbr), r0
    SAR     r1, r0
    SAR     r7, r0
    SAR     r2, r0
    ! its corners about it (the sprite's l t r b)
    mov.w   @(C_INDEX,r14), r0
    extu.w  r0, r0
    shll2   r0
    shll2   r0
    mov     r0, r3
    mov.l   @(D_SPRITES,gbr), r0
    add     r0, r3                  ! its Sprite
    mov.w   @(8,r3), r0
    mov     r0, r4
    add     r1, r4                  ! l
    mov.w   @(12,r3), r0
    mov     r0, r6
    add     r1, r6                  ! r
    mov.w   @(10,r3), r0
    mov     r0, r5
    add     r7, r5                  ! t
    mov.w   @(14,r3), r0
    add     r0, r7                  ! b
    ! PERSPECTIVE on both: d = divTable[z >> 4], each * d >> 12
    mov     r2, r1
    shar    r1
    shar    r1
    shar    r1
    shar    r1
    mov.w   .Ldiv_last2, r0
    cmp/gt  r0, r1
    bf      1f
    mov     r0, r1
1:  shll    r1
    mov.l   @(D_DIV,gbr), r0
    mov.w   @(r0,r1), r1
    extu.w  r1, r1                  ! d
    mov.l   @(D_K20,gbr), r0
    mov     r0, r3
    .irp    v, r4, r5, r6, r7
    mul.l   r1, \v
    sts     macl, \v
    SAR     \v, r3
    .endr
    mov     #88, r0
    shll    r0
    add     r0, r4                  ! l + FRAME_WIDTH / 2
    add     r0, r6
    mov     #112, r0
    add     r0, r5
    add     r0, r7
    ! off the viewport, or empty
    mov.l   @(D_VIEWPORT+8,gbr), r0
    cmp/ge  r0, r4
    bt      .Lsprited
    mov.l   @(D_VIEWPORT,gbr), r0
    cmp/gt  r6, r0
    bt      .Lsprited
    mov.l   @(D_VIEWPORT+12,gbr), r0
    cmp/ge  r0, r5
    bt      .Lsprited
    mov.l   @(D_VIEWPORT+4,gbr), r0
    cmp/gt  r7, r0
    bt      .Lsprited
    cmp/eq  r4, r6
    bt      .Lsprited
    cmp/eq  r5, r7
    bt      .Lsprited
    ! the shade, fogged past FOG_MIN
    mov.l   @(C_ARGS+12,r14), r1
    mov     #3, r0
    shll8   r0
    shll2   r0
    shll2   r0                      ! FOG_MIN
    cmp/gt  r0, r2
    bf      2f
    mov     r2, r3
    sub     r0, r3
    add     r3, r1
    mov     #32, r0
    shll8   r0
    add     #-1, r0                 ! 8191
    cmp/gt  r0, r1
    bf      2f
    mov     r0, r1
2:  VDP1COORD r4
    VDP1COORD r5
    VDP1COORD r6
    VDP1COORD r7
    mov     r4, r8
    mov     r5, r9
    mov     r6, r10
    mov.l   r7, @-r15               ! b
    mov.l   r1, @-r15               ! the shade
    ! drawn in front of the geometry it stands on: max(0, z - 128) >> 4
    mov     r2, r6
    mov     #-128, r0
    add     r0, r6
    cmp/pz  r6
    bt      3f
    mov     #0, r6
3:  shar    r6
    shar    r6
    shar    r6
    shar    r6
    mov.w   @(C_INDEX,r14), r0
    extu.w  r0, r5
    mov     #3, r0
    shll8   r0
    shll2   r0
    shll2   r0
    shll2   r0
    shll    r0                      ! FACE_TYPE_SPRITE << FACE_TYPE_SHIFT
    bsr     .Lcopy
    or      r0, r5
    mov.l   @r15+, r1
    mov.l   @r15+, r2
    mov.w   r8, @r7
    mov     r9, r0
    mov.w   r0, @(2,r7)
    mov     r1, r0
    shlr8   r0
    mov.b   r0, @(6,r7)             ! the shade >> 8
    mov     r10, r0
    mov.w   r0, @(8,r7)
    mov     r2, r0
    mov.w   r0, @(10,r7)
.Lsprited:
    lds.l   @r15+, pr
    rts
    nop

    .align  1
.Ldiv_last2: .word  1280
    .align  2

! renderRect: the call at r14 (x y width height, colour, bucket; its type)
.Lrect:
    sts.l   pr, @-r15
    mov.w   @(C_INDEX,r14), r0
    extu.w  r0, r5
    shll8   r5
    shll2   r5
    shll2   r5
    shll2   r5                      ! type << FACE_TYPE_SHIFT
    bsr     .Lcopy
    mov.l   @(C_ARGS+20,r14), r6
    mov.l   @(C_ARGS,r14), r0
    mov.w   r0, @r7
    mov.l   @(C_ARGS+4,r14), r0
    mov.w   r0, @(2,r7)
    mov.l   @(C_ARGS+16,r14), r0
    mov.w   r0, @(4,r7)             ! its colour in z
    mov.l   @(C_ARGS+8,r14), r0
    mov.w   r0, @(8,r7)
    mov.l   @(C_ARGS+12,r14), r0
    mov.w   r0, @(10,r7)
    lds.l   @r15+, pr
    rts
    nop

! setViewport: the call at r14
.Lviewport:
    mov.l   @(C_ARGS,r14), r0
    mov.l   r0, @(D_VIEWPORT,gbr)
    mov.l   @(C_ARGS+4,r14), r0
    mov.l   r0, @(D_VIEWPORT+4,gbr)
    mov.l   @(C_ARGS+8,r14), r0
    mov.l   r0, @(D_VIEWPORT+8,gbr)
    mov.l   @(C_ARGS+12,r14), r0
    rts
    mov.l   r0, @(D_VIEWPORT+12,gbr)

! the call at r14, if its thing's index has its bit (& 31) in the frame's
! hints' mask of its kind (render.cpp depthHintObject's): its HintedCall
! opened at D_HINTED, from the next draw (kind r1, owner, index r2); r0 .. r3
.Lhinted:
    mov.b   @r14, r0
    cmp/eq  #CALL_MODEL, r0
    bt      1f
    cmp/eq  #CALL_STATIC, r0
    bt      2f
    rts
    nop
1:  mov.l   @(D_HINT_ITEMS,gbr), r0
    tst     r0, r0
    bt      9f                      ! no item's
    mov     r0, r3
    mov.l   @(C_ARGS,r14), r0
    extu.w  r0, r2                  ! the item's index in the level
    bra     3f
    mov     #0, r1                  ! kind 0, owner 0
2:  mov.l   @(D_HINT_STATICS,gbr), r0
    tst     r0, r0
    bt      9f                      ! no static's
    mov     r0, r3
    mov.l   @(C_ARGS+20,r14), r0
    extu.w  r0, r2                  ! its index in its room
    mov.w   @(C_INDEX,r14), r0      ! its room
    extu.b  r0, r1
    mov     #1, r0
    shll8   r0
    or      r0, r1                  ! kind 1 << 8 | owner
3:  mov     #31, r0
    and     r2, r0
    shll2   r0
    mov.l   r4, @-r15
    mov.l   .LhintBits, r4
    mov.l   @(r0,r4), r0
    mov.l   @r15+, r4
    tst     r0, r3
    bt      9f                      ! its bit isn't in the mask
    mov.l   @(D_HINTED,gbr), r0
    mov     r0, r3
    mov     r1, r0
    mov.w   r0, @r3                 ! kind, owner
    mov     r2, r0
    mov.w   r0, @(2,r3)             ! index
    mov.l   @(D_DRAW_COUNT,gbr), r0
    mov.w   r0, @(4,r3)             ! its first draw
    mov     #1, r0
    mov.l   r0, @(D_HINT_OPEN,gbr)
9:  rts
    nop

! the open HintedCall closed: its draws counted, kept if there are any; r0 .. r2
.Lhintedclose:
    mov     #0, r0
    mov.l   r0, @(D_HINT_OPEN,gbr)
    mov.l   @(D_HINTED,gbr), r0
    mov     r0, r1
    mov.w   @(4,r1), r0
    extu.w  r0, r2
    mov.l   @(D_DRAW_COUNT,gbr), r0
    sub     r2, r0
    tst     r0, r0
    bt      9f                      ! none: dropped
    mov.w   r0, @(6,r1)
    add     #HINTED_BYTES, r1
    mov     r1, r0
    mov.l   r0, @(D_HINTED,gbr)
    mov.l   @(D_HINTED_COUNT,gbr), r0
    add     #1, r0
    mov.l   r0, @(D_HINTED_COUNT,gbr)
9:  rts
    nop

    .align  2
.LhintBits: .long   .LhintBitTable
.LhintBitTable:
    .long   1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, 1 << 6, 1 << 7
    .long   1 << 8, 1 << 9, 1 << 10, 1 << 11, 1 << 12, 1 << 13, 1 << 14, 1 << 15
    .long   1 << 16, 1 << 17, 1 << 18, 1 << 19, 1 << 20, 1 << 21, 1 << 22, 1 << 23
    .long   1 << 24, 1 << 25, 1 << 26, 1 << 27, 1 << 28, 1 << 29, 1 << 30, 1 << 31

    .global _drawsAsm
_drawsAsm:
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
    mov.l   @(D_CALLS,gbr), r0
    mov     r0, r14
    mov.l   @(D_CALL_COUNT,gbr), r0
    mov     r0, r13
    mov.l   @(D_DRAWS,gbr), r0
    mov     r0, r12
    mov.l   @(D_BASE,gbr), r0
    mov     r0, r11
    mov     #0, r0
    mov.l   r0, @(D_ITEM_COUNT,gbr)
    mov.l   r0, @(D_STATIC_COUNT,gbr)
    mov.l   r0, @(D_SHADOW_COUNT,gbr)
    mov.l   r0, @(D_FACE_COUNT,gbr)
    mov.l   r0, @(D_DRAW_COUNT,gbr)
    mov.l   r0, @(D_HINTED_COUNT,gbr)
    mov.l   r0, @(D_HINT_OPEN,gbr)
    tst     r13, r13
    bt      .Lend
.Lcall:
    bsr     .Lhinted
    nop
    mov.b   @r14, r0
    cmp/eq  #CALL_VIEWPORT, r0
    bf      1f
    bsr     .Lviewport
    nop
    bra     .Lnext
    nop
1:  cmp/eq  #CALL_MODEL, r0
    bf      2f
    bsr     .Lmodel
    nop
    bra     .Lnext
    nop
2:  cmp/eq  #CALL_STATIC, r0
    bf      3f
    bsr     .Lstatic
    nop
    bra     .Lnext
    nop
3:  cmp/eq  #CALL_SHADOW, r0
    bf      4f
    bsr     .Lshadow
    nop
    bra     .Lnext
    nop
4:  cmp/eq  #CALL_SPRITE, r0
    bf      5f
    bsr     .Lsprite
    nop
    bra     .Lnext
    nop
5:  cmp/eq  #CALL_RECT, r0
    bf      .Lnext
    bsr     .Lrect
    nop
.Lnext:
    mov.l   @(D_HINT_OPEN,gbr), r0
    tst     r0, r0
    bt      1f
    bsr     .Lhintedclose
    nop
1:  dt      r13
    bf/s    .Lcall
    add     #CALL_BYTES, r14
.Lend:
    mov     r12, r0
    mov.l   r0, @(D_DRAWS_END,gbr)
    mov     r11, r0
    mov.l   r0, @(D_BASE,gbr)
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

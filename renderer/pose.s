! Stage 1 the new way (pose.h): the items' node matrices, each exactly the
! game's: openlara draw.h drawModel (the view pushed, the item's place and
! turn), drawNodesLerp and drawLaraNodesLerp, on common.cpp's matrix
! arithmetic as the Saturn builds it (int32 entries, mul16, the divide
! table's lerp step). Every item in one call; each drawn mesh's matrix to
! its draw.
!
! void poseAsm(const PoseItem* items, int32 count, PoseContext* ctx)
!
! Each drawn mesh's matrix to its draw's DrawXform (scene.h), with its item's
! light from ctx->lit (poseLightAsm's, by item), as the mesh feed reads them:
! +0 the matrix, +48 ambient, +52 range, +56 light (int16 x y z).
!
! A rotation's x c - y s >> 14 (MATRIX_ROTXY): the products of the low
! halves by MULS.W as mul16, the sum shifted left 2, its high half sign-
! extended; exact, as the sum stays inside 2^29 (the rotation's entries and
! the sines inside 2^14). A turn by 0 is no turn (sin 0, cos 2^14), so the
! game's skipping of 0 angles is kept or not alike: angles below 16 take the
! table's first entry and turn by nothing either.
!
! Registers through the walk: GBR the context, r14 the stack's top, r13 the
! sines, r12 nodes left (Lara: her arm), r11 the next draw's matrix, r10
! the item, r9 its next node, r8 its visible mask (Lara: her arm's record).
! The subroutines use r0..r7 and keep the rest.
!
! PoseItem (scene.h): +0 lara, +1 count, +2 firstDraw, +4 nodes, +8 frameA,
! +12 frameB, +16 frameDelta, +20 frameRate, +24 visibleMask, +28 pos,
! +40 angle, +46 extraCount, +48 extra, +56 torso, +62 head, +68 weapon,
! +69 matrix, +72 arms (PoseArm: +0 frame, +4 frameRate, +8 angle, +14
! useBasis; 16).
! A frame (PFRM): its place at +12, its packed angles from +20; a node
! (PNOD): int16 x y z, flags.

    .equ    I_LARA,         0
    .equ    I_COUNT,        1
    .equ    I_FIRST,        2
    .equ    I_NODES,        4
    .equ    I_FRAME_A,      8
    .equ    I_FRAME_B,      12
    .equ    I_DELTA,        16
    .equ    I_RATE,         20
    .equ    I_MASK,         24
    .equ    I_POS,          28
    .equ    I_ANGLE,        40
    .equ    I_EXTRAS,       46
    .equ    I_TORSO,        56
    .equ    I_HEAD,         62
    .equ    I_WEAPON,       68
    .equ    I_MATRIX,       69
    .equ    I_ARMS,         72
    .equ    ITEM,           104
    .equ    A_FRAME,        0
    .equ    A_RATE,         4
    .equ    A_ANGLE,        8
    .equ    A_BASIS,        14
    .equ    ARM,            16

    .equ    C_DIV,          4
    .equ    C_NODES,        8
    .equ    C_FRAMES,       12
    .equ    C_CAMERA,       16
    .equ    C_OUT,          28
    .equ    C_STACK,        32
    .equ    C_OP,           36
    .equ    C_KIND,         40
    .equ    C_T,            44
    .equ    C_A,            48
    .equ    C_B,            52
    .equ    C_EXTRA,        56
    .equ    C_FIRST,        60
    .equ    C_LEFT,         64
    .equ    C_TORSO,        68
    .equ    C_ARMS,         80
    .equ    C_POSLERP,      104
    .equ    C_LIGHT,        112     ! the light's scratch (L_*)
    .equ    C_ROOMS,        176
    .equ    C_LIGHTS,       180
    .equ    C_LIT,          184
    .equ    C_LITNOW,       188     ! the item's PoseLit
    .equ    C_MATRICES,     196     ! the matrices items are drawn under

    .equ    X_AMBIENT,      48      ! DrawXform
    .equ    X_RANGE,        52
    .equ    X_LIGHT,        56
    .equ    DRAW_XFORM,     72
    .equ    LIT,            16      ! PoseLit

    .equ    F_POS,          12
    .equ    F_ANGLES,       20
    .equ    MATRIX,         48
    .equ    NODE,           8
    .equ    NO_FRAME,       -1
    .equ    POSE_HALF,      1
    .equ    POSE_QUARTER,   2
    .equ    POSE_THREE,     3
    .equ    WEAPON_SHOTGUN, 3
    .equ    JOINT_TORSO,    7
    .equ    JOINT_ARM_R1,   8
    .equ    JOINT_ARM_L1,   11
    .equ    NODE_POP,       1
    .equ    NODE_PUSH,      2
    .equ    NODE_ROTX,      4
    .equ    NODE_ROTY,      8
    .equ    NODE_ROTZ,      16

! The sine offsets of packed angles (DECODE_ANGLES, then the angle's top 12
! bits a longword each): r5 the angles -> r4 y, r5 x, r6 z; r0 r1 r2 r3.
! p names the pool the masks are in.
    .macro  DECODE p
    mov     r5, r6
    mov.l   .Lm3ff\p, r3
    shlr16  r6
    mov.l   .Lmfc00\p, r2
    mov     r6, r4
    mov.l   .Lm3ff0\p, r1
    and     r2, r4
    mov     r5, r0
    and     #15, r0
    shlr2   r4
    shll8   r0
    shlr2   r4
    shll2   r0
    shlr2   r4
    or      r0, r4              ! y: (a & 15) << 10 | (a >> 16 & 0xFC00) >> 6
    and     r3, r6
    shll2   r6
    shll2   r6                  ! z: (a >> 16 & 0x3FF) << 4
    and     r1, r5              ! x: a & 0x3FF0
    .endm

    .macro  POOL p
    .align  2
.Lm3ff\p:   .long   0x3FF
.Lmfc00\p:  .long   0xFC00
.Lm3ff0\p:  .long   0x3FF0
    .endm

! An angle's sine offset: (angle & 0xFFF0) >> 2.
    .macro  ANGLE r
    extu.w  \r, \r
    shlr2   \r
    shlr2   \r
    shll2   \r
    .endm

! A row's columns xo and yo turned (MATRIX_ROTXY): x c - y s, y c + x s, each
! >> 14; r1 the cosine, r2 the sine (low halves); r0 r3 r4 r7.
    .macro  ROWROT xo, yo
    mov.l   @(\xo,r14), r3
    mov.l   @(\yo,r14), r4
    muls.w  r3, r1              ! x c
    sts     macl, r0
    muls.w  r4, r2              ! y s
    sts     macl, r7
    muls.w  r4, r1              ! y c
    sub     r7, r0
    shll2   r0
    sts     macl, r4
    muls.w  r3, r2              ! x s
    shlr16  r0
    exts.w  r0, r0
    sts     macl, r3
    mov.l   r0, @(\xo,r14)
    add     r3, r4
    shll2   r4
    shlr16  r4
    exts.w  r4, r4
    mov.l   r4, @(\yo,r14)
    .endm

! The top turned about one axis, by the sine at offset off, unless 0.
    .macro  AXIS off, xo, yo
    tst     \off, \off
    bt      1f
    mov     \off, r0
    mov.l   @(r0,r13), r1       ! sin << 16 | cos
    swap.w  r1, r2
    ROWROT  \xo, \yo
    ROWROT  (\xo+16), (\yo+16)
    ROWROT  (\xo+32), (\yo+32)
1:
    .endm

! A row's translation moved by the row times the place (x y z in r1 r2 r3):
! matrixTranslateRel's mul16 dot; r0 r4 r7.
    .macro  TRANS ro
    mov.l   @(\ro,r14), r7
    muls.w  r7, r1
    mov.l   @(\ro+4,r14), r7
    sts     macl, r0
    muls.w  r7, r2
    mov.l   @(\ro+8,r14), r7
    sts     macl, r4
    muls.w  r7, r3
    add     r4, r0
    mov.l   @(\ro+12,r14), r7
    sts     macl, r4
    add     r4, r0
    add     r0, r7
    mov.l   r7, @(\ro+12,r14)
    .endm

! The top copied to the slot above (matrixPush's copy); r7 the slot above;
! r0 r1 r2 r3.
    .macro  COPY
    mov     r14, r7
    mov.l   @r14, r0
    add     #MATRIX, r7
    mov.l   @(4,r14), r1
    mov.l   @(8,r14), r2
    mov.l   @(12,r14), r3
    mov.l   r0, @r7
    mov.l   r1, @(4,r7)
    mov.l   r2, @(8,r7)
    mov.l   r3, @(12,r7)
    mov.l   @(16,r14), r0
    mov.l   @(20,r14), r1
    mov.l   @(24,r14), r2
    mov.l   @(28,r14), r3
    mov.l   r0, @(16,r7)
    mov.l   r1, @(20,r7)
    mov.l   r2, @(24,r7)
    mov.l   r3, @(28,r7)
    mov.l   @(32,r14), r0
    mov.l   @(36,r14), r1
    mov.l   @(40,r14), r2
    mov.l   @(44,r14), r3
    mov.l   r0, @(32,r7)
    mov.l   r1, @(36,r7)
    mov.l   r2, @(40,r7)
    mov.l   r3, @(44,r7)
    .endm

    .macro  PUSH
    COPY
    add     #MATRIX, r14
    .endm

    .macro  POP
    add     #-MATRIX, r14
    .endm

! The top and the item's light to the next draw's DrawXform (drawMesh); r0
! r1 r2 r3.
    .macro  OUT
    mov.l   @r14, r0
    mov.l   @(4,r14), r1
    mov.l   @(8,r14), r2
    mov.l   @(12,r14), r3
    mov.l   r0, @r11
    mov.l   r1, @(4,r11)
    mov.l   r2, @(8,r11)
    mov.l   r3, @(12,r11)
    mov.l   @(16,r14), r0
    mov.l   @(20,r14), r1
    mov.l   @(24,r14), r2
    mov.l   @(28,r14), r3
    mov.l   r0, @(16,r11)
    mov.l   r1, @(20,r11)
    mov.l   r2, @(24,r11)
    mov.l   r3, @(28,r11)
    mov.l   @(32,r14), r0
    mov.l   @(36,r14), r1
    mov.l   @(40,r14), r2
    mov.l   @(44,r14), r3
    mov.l   r0, @(32,r11)
    mov.l   r1, @(36,r11)
    mov.l   r2, @(40,r11)
    mov.l   r3, @(44,r11)
    mov.l   @(C_LITNOW,gbr), r0
    mov.l   @(0,r0), r1
    mov.l   @(4,r0), r2
    mov.l   @(8,r0), r3
    mov.w   @(12,r0), r0
    mov.l   r1, @(X_AMBIENT,r11)
    mov.l   r2, @(X_RANGE,r11)
    mov.l   r3, @(X_LIGHT,r11)
    mov     r11, r1
    add     #X_LIGHT+4, r1
    mov.w   r0, @r1
    add     #DRAW_XFORM, r11
    .endm

! matrixLerp's entries, the top towards the slot above (r7).
    .macro  HALFE k             ! (b + a) >> 1
    mov.l   @(\k,r14), r0
    mov.l   @(\k,r7), r1
    add     r1, r0
    shar    r0
    mov.l   r0, @(\k,r14)
    .endm

    .macro  QUARTE k            ! a + (b - a >> 2)
    mov.l   @(\k,r14), r0
    mov.l   @(\k,r7), r1
    sub     r0, r1
    shar    r1
    shar    r1
    add     r1, r0
    mov.l   r0, @(\k,r14)
    .endm

    .macro  THREEE k            ! b - (b - a >> 2)
    mov.l   @(\k,r14), r0
    mov.l   @(\k,r7), r1
    mov     r1, r2
    sub     r0, r2
    shar    r2
    shar    r2
    sub     r2, r1
    mov.l   r1, @(\k,r14)
    .endm

    .macro  SLOWE k             ! a + ((b - a) t >> 8): r3 t, r4 2^24
    mov.l   @(\k,r14), r0
    mov.l   @(\k,r7), r1
    sub     r0, r1
    mul.l   r3, r1
    sts     macl, r1
    dmuls.l r1, r4
    sts     mach, r1
    add     r1, r0
    mov.l   r0, @(\k,r14)
    .endm

! The rotations' nine entries; the lerps leave the translations alone where
! both slots hold the same (all but the half's, which can carry).
    .macro  ROTATIONS m
    \m      0
    \m      4
    \m      8
    \m      16
    \m      20
    \m      24
    \m      32
    \m      36
    \m      40
    .endm

    .text
    .align  2
    .global _poseAsm
_poseAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    stc.l   gbr, @-r15
    ldc     r6, gbr
    mov.l   @r6, r13
    mov     r4, r10
    mov.l   @(C_LIT,gbr), r0
    mov.l   r0, @(C_LITNOW,gbr)
    mov     r5, r0
    tst     r0, r0
    bf/s    .Litem
    mov.l   r0, @(C_LEFT,gbr)
    bra     .Ldone
    nop

! an item drawn under a matrix given (drawNodesLerp's): r0 its index + 1,
! r14 the stack's top, the matrix there
.Lgiven:
    add     #-1, r0
    mov     #MATRIX, r1
    mulu.w  r0, r1
    mov.l   @(C_MATRICES,gbr), r0
    sts     macl, r1
    add     r0, r1
    .irp    o, 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44
    mov.l   @(\o,r1), r2
    mov.l   r2, @(\o,r14)
    .endr
    bra     .Lplaced
    nop

.Litem:
    ! its draws' DrawXforms from out + firstDraw * 72
    mov.w   @(I_FIRST,r10), r0
    extu.w  r0, r1
    mov     r1, r2
    shll8   r2
    shlr2   r2
    shll2   r1
    shll    r1
    add     r2, r1
    mov.l   @(C_OUT,gbr), r0
    add     r0, r1
    mov     r1, r11
    ! the view pushed, placed at the item (matrixTranslateAbs: plain
    ! products of its rows and the place from the camera)
    mov.l   @(C_STACK,gbr), r0
    mov     r0, r1
    mov     r0, r14
    add     #MATRIX, r14
    mov     r10, r0
    add     #I_MATRIX, r0
    mov.b   @r0, r0
    extu.b  r0, r0
    tst     r0, r0
    bf      .Lgiven
    mov.l   @(I_POS,r10), r2
    mov.l   @(C_CAMERA,gbr), r0
    sub     r0, r2
    mov.l   @(I_POS+4,r10), r3
    mov.l   @(C_CAMERA+4,gbr), r0
    sub     r0, r3
    mov.l   @(I_POS+8,r10), r4
    mov.l   @(C_CAMERA+8,gbr), r0
    sub     r0, r4
    .irp    ro, 0, 16, 32
    mov.l   @(\ro,r1), r5
    mul.l   r5, r2
    mov.l   r5, @(\ro,r14)
    mov.l   @(\ro+4,r1), r5
    sts     macl, r6
    mul.l   r5, r3
    mov.l   r5, @(\ro+4,r14)
    mov.l   @(\ro+8,r1), r5
    sts     macl, r7
    mul.l   r5, r4
    add     r7, r6
    mov.l   r5, @(\ro+8,r14)
    sts     macl, r7
    add     r7, r6
    mov.l   r6, @(\ro+12,r14)
    .endr
    ! turned by its angles, y x z
    mov     r10, r7
    add     #I_ANGLE, r7
    mov.w   @r7, r5
    ANGLE   r5
    mov.w   @(2,r7), r0
    mov     r0, r4
    ANGLE   r4
    mov.w   @(4,r7), r0
    mov     r0, r6
    ANGLE   r6
    bsr     .Lrot
    nop

.Lplaced:
    ! its frames: their angles, the first node's place, the lerp
    mov.l   @(C_FRAMES,gbr), r0
    mov.l   @(I_FRAME_A,r10), r4
    mov.l   @(I_FRAME_B,r10), r5
    add     r0, r4
    add     r0, r5
    mov     r4, r0
    add     #F_ANGLES, r0
    mov.l   r0, @(C_A,gbr)
    mov     r5, r0
    add     #F_ANGLES, r0
    mov.l   r0, @(C_B,gbr)
    mov.l   @(I_DELTA,r10), r1
    tst     r1, r1
    bf      .Llerps
    mov     r4, r0
    add     #F_POS, r0
    mov.l   r0, @(C_FIRST,gbr)
    mov.l   .Lframe_, r0
    mov.l   r0, @(C_OP,gbr)
    bra     .Larms
    nop

.Llerps:
    ! t = divTable[rate] * delta: the place lerped, x y z (draw.h posLerp)
    mov.l   @(I_RATE,r10), r2
    mov.l   @(C_DIV,gbr), r0
    mov     r0, r3
    mov     r2, r0
    shll    r0
    mov.w   @(r0,r3), r3
    extu.w  r3, r3
    mul.l   r1, r3
    sts     macl, r3
    stc     gbr, r7
    add     #C_POSLERP, r7
    .irp    k, 0, 2, 4
    mov.w   @(F_POS+\k,r4), r0
    mov     r0, r6
    mov.w   @(F_POS+\k,r5), r0
    sub     r6, r0
    mul.l   r3, r0
    sts     macl, r0
    shlr16  r0
    exts.w  r0, r0
    add     r6, r0
    mov.w   r0, @(\k,r7)
    .endr
    mov     r7, r0
    mov.l   r0, @(C_FIRST,gbr)
    mov.l   .LframeLerp_, r0
    mov.l   r0, @(C_OP,gbr)
    ! matrixLerp's kind: 2 or 2 of 4 a half, 1 of 4 a quarter, others of
    ! 4 three quarters, else the slow step delta * divTable[rate] >> 8
    mov     r2, r0
    cmp/eq  #2, r0
    bt      .Lhalf_
    cmp/eq  #4, r0
    bf      .Lslow_
    mov     r1, r0
    cmp/eq  #2, r0
    bt      .Lhalf_
    cmp/eq  #1, r0
    bt/s    .Lkind
    mov     #POSE_QUARTER, r0
    bra     .Lkind
    mov     #POSE_THREE, r0
.Lhalf_:
    bra     .Lkind
    mov     #POSE_HALF, r0
.Lslow_:
    shlr8   r3
    mov     r3, r0
    mov.l   r0, @(C_T,gbr)
    mov     #4, r0
.Lkind:
    mov.l   r0, @(C_KIND,gbr)

.Larms:
    mov.b   @(I_LARA,r10), r0
    tst     r0, r0
    bt      1f
    bra     .Llara
    nop
1:  bra     .Lgeneric
    nop

.Lnext:
    mov.l   @(C_LITNOW,gbr), r0
    add     #LIT, r0
    mov.l   r0, @(C_LITNOW,gbr)
    mov.l   @(C_LEFT,gbr), r0
    add     #ITEM, r10
    dt      r0
    bt/s    .Ldone
    mov.l   r0, @(C_LEFT,gbr)
    bra     .Litem
    nop

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

    .align  2
.Lframe_:       .long   .Lframe
.LframeLerp_:   .long   .LframeLerp

! --- a node's frame: still, or lerped -------------------------------------

! matrixFrame: r4 the place (int16 x y z), r5 the packed angles.
.Lframe:
    mov.w   @r4+, r1
    mov.w   @r4+, r2
    mov.w   @r4, r3
    TRANS   0
    TRANS   16
    TRANS   32
    mov.l   @r5, r5
    DECODE  1
    bra     .Lrot
    nop

! matrixFrameLerp: r4 the place, r5 frame A's packed angles, r6 frame B's.
! Placed, then copied to the slot above and turned by B there, turned by A
! on the top, and lerped towards the slot above by the item's kind.
.LframeLerp:
    sts.l   pr, @-r15
    mov.w   @r4+, r1
    mov.w   @r4+, r2
    mov.w   @r4, r3
    TRANS   0
    TRANS   16
    TRANS   32
    COPY
    mov.l   r5, @-r15
    add     #MATRIX, r14
    mov.l   @r6, r5
    DECODE  1
    bsr     .Lrot
    nop
    add     #-MATRIX, r14
    mov.l   @r15+, r5
    mov.l   @r5, r5
    DECODE  1
    bsr     .Lrot
    nop
    mov.l   @(C_KIND,gbr), r0
    mov     r14, r7
    add     #MATRIX, r7
    cmp/eq  #POSE_HALF, r0
    bt      .Lhalf
    cmp/eq  #POSE_QUARTER, r0
    bt      .Lquarter
    cmp/eq  #POSE_THREE, r0
    bf      1f
    bra     .Lthree
    nop
1:  bra     .Lslow
    nop

    POOL    1

.Lhalf:
    ROTATIONS HALFE
    HALFE   12
    HALFE   28
    HALFE   44
    lds.l   @r15+, pr
    rts
    nop

.Lquarter:
    ROTATIONS QUARTE
    lds.l   @r15+, pr
    rts
    nop

.Lthree:
    ROTATIONS THREEE
    lds.l   @r15+, pr
    rts
    nop

.Lslow:
    mov.l   @(C_T,gbr), r0
    mov     r0, r3
    mov     #1, r4
    shll16  r4
    shll8   r4
    ROTATIONS SLOWE
    lds.l   @r15+, pr
    rts
    nop

! The top turned y, x, z (matrixRotateYXZ): r4 r5 r6 the sine offsets, 0
! for no turn.
.Lrot:
    AXIS    r4, 0, 8
    AXIS    r5, 8, 4
    AXIS    r6, 4, 0
    rts
    nop

! matrixTranslateRel: r4 the place.
.Ltrans:
    mov.w   @r4+, r1
    mov.w   @r4+, r2
    mov.w   @r4, r3
    TRANS   0
    TRANS   16
    TRANS   32
    rts
    nop

! --- drawNodesLerp: the nodes as their flags push and pop them -------------

.Lgeneric:
    mov.b   @(I_COUNT,r10), r0
    extu.b  r0, r12
    mov.l   @(I_MASK,r10), r8
    mov.l   @(I_NODES,r10), r9
    mov.l   @(C_NODES,gbr), r0
    add     r0, r9
    mov     r10, r1
    add     #I_EXTRAS, r1
    mov.w   @r1+, r0
    tst     r0, r0
    bf/s    1f
    mov     r1, r0
    mov     #0, r0
1:  mov.l   r0, @(C_EXTRA,gbr)
    mov.l   @(C_FIRST,gbr), r0
    mov     r0, r4
    mov.l   @(C_A,gbr), r0
    mov     r0, r5
    mov.l   @(C_B,gbr), r0
    mov     r0, r6
    mov.l   @(C_OP,gbr), r0
    jsr     @r0
    nop
    shlr    r8
    bf      .Lnode
    OUT
.Lnode:
    mov     #1, r0
    cmp/hi  r0, r12
    bt      1f
    bra     .Lnext
    nop
1:  mov.l   @(C_A,gbr), r0
    add     #4, r0
    mov.l   r0, @(C_A,gbr)
    mov     r0, r5
    mov.l   @(C_B,gbr), r0
    add     #4, r0
    mov.l   r0, @(C_B,gbr)
    mov     r0, r6
    mov.w   @(6,r9), r0
    tst     #NODE_POP, r0
    bt      2f
    POP
2:  tst     #NODE_PUSH, r0
    bt      3f
    PUSH
3:  mov.l   @(C_OP,gbr), r0
    jsr     @r0
    mov     r9, r4
    mov.l   @(C_EXTRA,gbr), r0
    tst     r0, r0
    bt      7f
    ! an enemy's extra angles, y x z as the node's flags take them
    mov     r0, r1
    mov.w   @(6,r9), r0
    mov     #0, r4
    mov     #0, r5
    tst     #NODE_ROTY, r0
    bt/s    4f
    mov     #0, r6
    mov.w   @r1+, r4
    ANGLE   r4
4:  tst     #NODE_ROTX, r0
    bt      5f
    mov.w   @r1+, r5
    ANGLE   r5
5:  tst     #NODE_ROTZ, r0
    bt      6f
    mov.w   @r1+, r6
    ANGLE   r6
6:  mov     r1, r0
    bsr     .Lrot
    mov.l   r0, @(C_EXTRA,gbr)
7:  shlr    r8
    bf      8f
    OUT
8:  add     #NODE, r9
    bra     .Lnode
    add     #-1, r12

! --- drawLaraNodesLerp ------------------------------------------------------

! Her arm's frames (the body's where it has none of its own), the shotgun's
! torso turn, whether it lerps: r12 the arm, r8 its record; r6 its joint's
! offset in the angles.
    .macro  ARMSET joint
    mov.l   @(A_FRAME,r8), r5
    mov     r5, r0
    cmp/eq  #NO_FRAME, r0
    bf      1f
    mov.l   @(C_A,gbr), r0
    mov     r0, r5
    mov.l   @(C_B,gbr), r0
    mov     r0, r7
    bra     3f
    mov.l   @(I_RATE,r10), r4
1:  mov.l   @(C_FRAMES,gbr), r0
    add     r0, r5
    add     #F_ANGLES, r5
    mov     r5, r7
    mov.l   @(A_RATE,r8), r4
3:  mov     r12, r0
    shll2   r0
    stc     gbr, r1
    add     #C_ARMS, r1
    add     r0, r1
    mov     r5, r0
    add     #\joint*4, r0
    mov.l   r0, @r1
    mov     r7, r0
    add     #\joint*4, r0
    mov.l   r0, @(8,r1)
    mov.l   @(I_DELTA,r10), r0
    tst     r0, r0
    bt/s    4f
    mov     #0, r0
    mov     #1, r2
    cmp/gt  r2, r4
    movt    r0
4:  mov.l   r0, @(16,r1)
    .endm

.Llara:
    ! her torso's angles, then the shotgun's turn of its own
    mov     r10, r7
    add     #I_TORSO, r7
    mov.w   @r7, r0
    mov.l   r0, @(C_TORSO+4,gbr)
    mov.w   @(2,r7), r0
    mov.l   r0, @(C_TORSO,gbr)
    mov.w   @(4,r7), r0
    mov.l   r0, @(C_TORSO+8,gbr)
    mov     #0, r12
    mov     r10, r8
    add     #I_ARMS, r8
    mov.l   @(A_FRAME,r8), r0
    cmp/eq  #NO_FRAME, r0
    bt      1f
    mov     r10, r1
    add     #I_WEAPON, r1
    mov.b   @r1, r1
    mov     #WEAPON_SHOTGUN, r2
    cmp/eq  r2, r1
    bf      1f
    mov.l   @(C_FRAMES,gbr), r0
    mov.l   @(A_FRAME,r8), r5
    add     r0, r5
    mov.l   @(F_ANGLES+JOINT_TORSO*4,r5), r5
    DECODE  2
    shll2   r4                  ! the angles: offsets << 2
    shll2   r5
    shll2   r6
    mov.l   .Ltorsox, r1
    mov.l   @(C_TORSO+4,gbr), r0
    add     r5, r0
    sub     r1, r0
    mov.l   r0, @(C_TORSO+4,gbr)
    mov.l   .Ltorsoy, r1
    mov.l   @(C_TORSO,gbr), r0
    add     r4, r0
    sub     r1, r0
    mov.l   r0, @(C_TORSO,gbr)
    mov.l   .Ltorsoz, r1
    mov.l   @(C_TORSO+8,gbr), r0
    add     r6, r0
    sub     r1, r0
    mov.l   r0, @(C_TORSO+8,gbr)
    bra     1f
    nop
    POOL    2
.Ltorsox:   .long   1216        ! DEF_TORSO_ANGLE_X
.Ltorsoy:   .long   -832
.Ltorsoz:   .long   -192
1:  ARMSET  JOINT_ARM_R1
    add     #1, r12
    add     #ARM, r8
    ARMSET  JOINT_ARM_L1
    .irp    k, 0, 4, 8
    mov.l   @(C_TORSO+\k,gbr), r0
    ANGLE   r0
    mov.l   r0, @(C_TORSO+\k,gbr)
    .endr

    mov.l   @(I_NODES,r10), r9
    mov.l   @(C_NODES,gbr), r0
    add     r0, r9
    PUSH
    ! hips
    mov.l   @(C_FIRST,gbr), r0
    bsr     .Lbody
    mov     r0, r4
    OUT
    ! legs
    mov     #2, r12
.Lleg:
    PUSH
    .rept   3
    mov     r9, r4
    bsr     .Lbody
    add     #NODE, r9
    OUT
    .endr
    POP
    dt      r12
    bt      1f
    bra     .Lleg
    nop
1:
    ! torso
    mov     r9, r4
    bsr     .Lbody
    add     #NODE, r9
    mov.l   @(C_TORSO,gbr), r0
    mov     r0, r4
    mov.l   @(C_TORSO+4,gbr), r0
    mov     r0, r5
    mov.l   @(C_TORSO+8,gbr), r0
    bsr     .Lrot
    mov     r0, r6
    OUT
    ! arms, right then left
    mov     #0, r12
    mov     r10, r8
    add     #I_ARMS, r8
.Larm:
    PUSH
    mov     r9, r4
    bsr     .Ltrans
    add     #NODE, r9
    mov.b   @(A_BASIS,r8), r0
    tst     r0, r0
    bt      1f
    ! hands turned from her basis (matrixSetBasis: the item's matrix)
    mov.l   @(C_STACK,gbr), r0
    add     #MATRIX, r0
    .irp    ro, 0, 16, 32
    mov.l   @(\ro,r0), r1
    mov.l   @(\ro+4,r0), r2
    mov.l   @(\ro+8,r0), r3
    mov.l   r1, @(\ro,r14)
    mov.l   r2, @(\ro+4,r14)
    mov.l   r3, @(\ro+8,r14)
    .endr
    mov     r8, r7
    add     #A_ANGLE, r7
    mov.w   @r7, r5
    ANGLE   r5
    mov.w   @(2,r7), r0
    mov     r0, r4
    ANGLE   r4
    mov.w   @(4,r7), r0
    mov     r0, r6
    ANGLE   r6
    bsr     .Lrot
    nop
1:  mov.l   .Lzero_, r4
    bsr     .Larmnode
    nop
    OUT
    mov     r9, r4
    bsr     .Larmnode
    add     #NODE, r9
    OUT
    mov     r9, r4
    bsr     .Larmnode
    add     #NODE, r9
    OUT
    POP
    add     #ARM, r8
    add     #1, r12
    mov     r12, r0
    cmp/eq  #2, r0
    bt      1f
    bra     .Larm
    nop
1:
    ! head: its frame angles past the arms' six
    mov     r9, r4
    mov.l   @(C_A,gbr), r0
    mov     r0, r5
    add     #6*4, r5
    mov.l   @(C_B,gbr), r0
    mov     r0, r6
    add     #6*4, r6
    mov.l   @(C_OP,gbr), r0
    jsr     @r0
    nop
    mov     r10, r7
    add     #I_HEAD, r7
    mov.w   @r7, r5
    ANGLE   r5
    mov.w   @(2,r7), r0
    mov     r0, r4
    ANGLE   r4
    mov.w   @(4,r7), r0
    mov     r0, r6
    ANGLE   r6
    bsr     .Lrot
    nop
    OUT
    POP
    bra     .Lnext
    nop

! A body node: r4 its place, the next angles of both frames.
.Lbody:
    mov.l   @(C_A,gbr), r0
    mov     r0, r5
    add     #4, r0
    mov.l   r0, @(C_A,gbr)
    mov.l   @(C_B,gbr), r0
    mov     r0, r6
    add     #4, r0
    mov.l   r0, @(C_B,gbr)
    mov.l   @(C_OP,gbr), r0
    jmp     @r0
    nop

! An arm node: r4 its place, the arm's next angles, lerped or not.
.Larmnode:
    sts.l   pr, @-r15
    mov     r12, r0
    shll2   r0
    stc     gbr, r7
    add     #C_ARMS, r7
    add     r0, r7
    mov.l   @r7, r5
    mov.l   @(8,r7), r6
    mov     r5, r0
    add     #4, r0
    mov.l   r0, @r7
    mov     r6, r0
    add     #4, r0
    mov.l   r0, @(8,r7)
    mov.l   @(16,r7), r0
    tst     r0, r0
    bt      1f
    bsr     .LframeLerp
    nop
    bra     2f
    nop
1:  bsr     .Lframe
    nop
2:  lds.l   @r15+, pr
    rts
    nop

    .align  2
.Lzero_:    .long   .Lzero
.Lzero:     .long   0, 0        ! ZERO_POS

! --- the items' light ------------------------------------------------------
!
! void poseLightAsm(const PoseItem* items, int32 count, PoseContext* ctx)
!
! Each item's as draw.h drawModel sets it before its nodes, to ctx->lit by
! item. Its depth for the fog: the view's third row times its place from the
! camera (matrixTranslateAbs). Lit by its own intensity (calcLightingStatic),
! or by its room's brightest light at its first frame's box centre turned as
! the item (calcLightingDynamic at getRelative: an identity turned by .Lrot).
! The divides on the SH-2's divider, but for a divisor of 0 or -1 (it
! overflows on x / 0 and on -2^31 / -1): those by libgcc's __sdivsi3, as the
! game divides. An arithmetic
! shift right by n is the high longword of the value times 2^(32 - n)
! (DMULS.L), exact for any longword as C's.
!
! PoseLight (scene.h): +0 roomX, +2 roomZ, +4 intensity, +5 ambient, +6
! lights, +8 first; 12 bytes. Light: int16 x y z, radius, intensity; 8.
! PoseLit (pose.h): +0 ambient, +4 range, +8 view (int16 x y z); 16.
!
! Registers: GBR the context, r14 the light's scratch (L_*), r13 the sines
! (a temporary in the light loop), r12 lights left, r11 the next PoseLit,
! r10 the item, r9 its PoseLight, r8 the next light.

    .equ    L_VIEW,         0       ! the light's direction in the view, kept from item to item
    .equ    L_PX,           8       ! the point lit
    .equ    L_DX,           20      ! the brightest light from it
    .equ    L_RX,           32      ! the room's place << 8
    .equ    L_RZ,           36
    .equ    L_AMBIENT,      40      ! 8191 - the room's ambient << 5
    .equ    L_MAX,          44      ! the brightest yet
    .equ    L_FOG,          48      ! the item's depth
    .equ    P_INTENSITY,    4
    .equ    P_AMBIENT,      5
    .equ    P_LIGHTS,       6
    .equ    P_FIRST,        8
    .equ    POSE_LIGHT,     12

! v >> n, arithmetic: k = 2^(32 - n)
    .macro  SAR v, k
    dmuls.l \v, \k
    sts     mach, \v
    .endm

! dividend / divisor (C's int division, toward 0) into the dividend's
! register; r0 r6, and where libgcc divides r1 r2 r3 kept
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
1:  mov.l   r1, @-r15
    mov.l   r2, @-r15
    mov.l   r3, @-r15
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov     \dividend, r4
    mov     r0, r5
    mov.l   .Lsdiv_\@, r0
    jsr     @r0
    nop
    mov     r0, r6
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    mov.l   @r15+, r3
    mov.l   @r15+, r2
    mov.l   @r15+, r1
    bra     2f
    mov     r6, \dividend
    .align  2
.Lsdiv_\@:  .long   ___sdivsi3
2:
    .endm

    .global _poseLightAsm
_poseLightAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    stc.l   gbr, @-r15
    ldc     r6, gbr
    mov     r4, r10
    mov.l   @(C_ROOMS,gbr), r0
    mov     r0, r9
    mov.l   @(C_LIT,gbr), r0
    mov     r0, r11
    mov     r5, r0
    tst     r0, r0
    bf/s    .Llitem
    mov.l   r0, @(C_LEFT,gbr)
    bra     .Llitdone
    nop

.Llitem:
    stc     gbr, r14
    add     #C_LIGHT, r14
    ! the depth: under a matrix given, its third row's translation (as
    ! calcLightingStatic reads the top), else the view's third row times the
    ! place from the camera; >> 14
    mov     r10, r0
    add     #I_MATRIX, r0
    mov.b   @r0, r0
    extu.b  r0, r0
    tst     r0, r0
    bt      .Lfogplaced
    add     #-1, r0
    mov     #48, r1
    mulu.w  r0, r1
    mov.l   @(C_MATRICES,gbr), r0
    sts     macl, r1
    add     r0, r1
    bra     .Lfogdepth
    mov.l   @(44,r1), r6
.Lfogplaced:
    mov.l   @(C_STACK,gbr), r0
    mov     r0, r1
    mov.l   @(I_POS,r10), r2
    mov.l   @(C_CAMERA,gbr), r0
    sub     r0, r2
    mov.l   @(I_POS+4,r10), r3
    mov.l   @(C_CAMERA+4,gbr), r0
    sub     r0, r3
    mov.l   @(I_POS+8,r10), r4
    mov.l   @(C_CAMERA+8,gbr), r0
    sub     r0, r4
    mov.l   @(32,r1), r5
    mul.l   r5, r2
    mov.l   @(36,r1), r5
    sts     macl, r6
    mul.l   r5, r3
    mov.l   @(40,r1), r5
    sts     macl, r7
    mul.l   r5, r4
    add     r7, r6
    sts     macl, r7
    add     r7, r6
.Lfogdepth:
    mov.l   .L2p18, r7
    SAR     r6, r7
    mov.l   r6, @(L_FOG,r14)
    ! lit by its own intensity
    mov.b   @(P_INTENSITY,r9), r0
    extu.b  r0, r4
    shll2   r4
    shll2   r4
    shll    r4
    tst     r4, r4
    bt      .Ldynamic
    mov.l   .L4096, r1
    sub     r1, r4
    mov     #0, r5
    mov.l   .L8191, r6
    bra     .Lfog
    nop

.Ldynamic:
    ! getRelative: an identity turned by the item's angles
    mov.l   @(C_STACK,gbr), r0
    add     #MATRIX, r0
    mov     #64, r2
    shll8   r2
    mov     #0, r3
    mov.l   r2, @(0,r0)
    mov.l   r3, @(4,r0)
    mov.l   r3, @(8,r0)
    mov.l   r3, @(16,r0)
    mov.l   r2, @(20,r0)
    mov.l   r3, @(24,r0)
    mov.l   r3, @(32,r0)
    mov.l   r3, @(36,r0)
    mov.l   r2, @(40,r0)
    mov     r14, r8
    mov     r0, r14
    mov.l   @(0,gbr), r0
    mov     r0, r13
    mov     r10, r7
    add     #I_ANGLE, r7
    mov.w   @r7, r5
    ANGLE   r5
    mov.w   @(2,r7), r0
    mov     r0, r4
    ANGLE   r4
    mov.w   @(4,r7), r0
    mov     r0, r6
    ANGLE   r6
    bsr     .Lrot
    nop
    mov     r14, r1
    mov     r8, r14
    ! its first frame's box centre
    mov.l   @(C_FRAMES,gbr), r0
    mov.l   @(I_FRAME_A,r10), r2
    add     r0, r2
    mov.w   @r2+, r3
    mov.w   @r2+, r4
    add     r3, r4
    shar    r4
    mov.w   @r2+, r3
    mov.w   @r2+, r5
    add     r3, r5
    shar    r5
    mov.w   @r2+, r3
    mov.w   @r2+, r6
    add     r3, r6
    shar    r6
    ! the point lit: the place plus the turned centre >> 14
    mov.l   .L2p18, r7
    .irp    k, 0, 4, 8
    mov.l   @(\k*4,r1), r0
    mul.l   r0, r4
    mov.l   @(\k*4+4,r1), r0
    sts     macl, r2
    mul.l   r0, r5
    mov.l   @(\k*4+8,r1), r0
    sts     macl, r3
    mul.l   r0, r6
    add     r3, r2
    sts     macl, r3
    add     r3, r2
    SAR     r2, r7
    mov.l   @(I_POS+\k,r10), r3
    add     r3, r2
    mov.l   r2, @(L_PX+\k,r14)
    .endr

    ! the room's ambient, and its brightest light
    mov.b   @(P_AMBIENT,r9), r0
    extu.b  r0, r4
    shll2   r4
    shll2   r4
    shll    r4
    mov.b   @(P_LIGHTS,r9), r0
    extu.b  r0, r12
    mov.l   .L8191, r6
    tst     r12, r12
    bf/s    1f
    mov     #0, r5
    bra     .Lfog
    nop
1:  sub     r4, r6
    mov.l   r6, @(L_AMBIENT,r14)
    mov     #0, r0
    mov.l   r0, @(L_MAX,r14)
    mov.l   r0, @(L_DX,r14)
    mov.l   r0, @(L_DX+4,r14)
    mov.l   r0, @(L_DX+8,r14)
    mov.w   @r9, r0
    shll8   r0
    mov.l   r0, @(L_RX,r14)
    mov.w   @(2,r9), r0
    shll8   r0
    mov.l   r0, @(L_RZ,r14)
    mov.l   @(P_FIRST,r9), r8
    shll2   r8
    shll    r8
    mov.l   @(C_LIGHTS,gbr), r0
    add     r0, r8
    mov.l   .L2p20, r7
.Llight:
    mov.w   @r8+, r1
    mov.w   @r8+, r2
    mov.w   @r8+, r3
    mov.b   @r8+, r4
    mov.b   @r8+, r5
    extu.b  r4, r4
    extu.b  r5, r5
    mov.l   @(L_RX,r14), r0
    add     r0, r1
    mov.l   @(L_RZ,r14), r0
    add     r0, r3
    shll8   r4                  ! radius << 8
    shll2   r5
    shll2   r5
    shll    r5                  ! intensity << 5
    ! dist = |point - light|^2 >> 12, att = (radius << 8)^2 >> 12
    mov.l   @(L_PX,r14), r6
    sub     r1, r6
    mul.l   r6, r6
    mov.l   @(L_PX+4,r14), r6
    sub     r2, r6
    sts     macl, r0
    mul.l   r6, r6
    mov.l   @(L_PX+8,r14), r6
    sub     r3, r6
    sts     macl, r13
    mul.l   r6, r6
    add     r13, r0
    sts     macl, r13
    add     r13, r0
    SAR     r0, r7
    mul.l   r4, r4
    sts     macl, r4
    SAR     r4, r7
    ! lum = intensity att / (dist + att) + 8191 - ambient
    mul.l   r4, r5
    add     r0, r4
    sts     macl, r5
    DIVIDE  r5, r4
    mov.l   @(L_AMBIENT,r14), r0
    add     r0, r5
    mov.l   @(L_MAX,r14), r0
    cmp/gt  r0, r5
    bf      3f
    mov.l   r5, @(L_MAX,r14)
    mov.l   @(L_PX,r14), r0
    sub     r0, r1
    mov.l   r1, @(L_DX,r14)
    mov.l   @(L_PX+4,r14), r0
    sub     r0, r2
    mov.l   r2, @(L_DX+4,r14)
    mov.l   @(L_PX+8,r14), r0
    sub     r0, r3
    mov.l   r3, @(L_DX+8,r14)
3:  dt      r12
    bt      4f
    bra     .Llight
    nop
4:
    ! halfway between the brightest and the ambient
    mov.l   @(L_MAX,r14), r1
    mov.l   @(L_AMBIENT,r14), r2
    mov     r1, r3
    add     r2, r3
    shar    r3
    mov.l   .L8191, r4
    sub     r3, r4              ! ambient
    mov     r1, r5
    sub     r3, r5              ! range
    cmp/pl  r5
    bt      5f
    bra     .Lcap
    nop
    ! the light's direction >> 4, its length (phd_sqrt), turned into the view
5:  mov.l   r4, @(L_AMBIENT,r14)
    mov.l   r5, @(L_MAX,r14)
    mov.l   @(L_DX,r14), r1
    mov.l   @(L_DX+4,r14), r2
    mov.l   @(L_DX+8,r14), r3
    .irp    r, r1, r2, r3
    shar    \r
    shar    \r
    shar    \r
    shar    \r
    .endr
    mul.l   r1, r1
    sts     macl, r0
    mul.l   r2, r2
    sts     macl, r4
    mul.l   r3, r3
    add     r4, r0
    sts     macl, r4
    add     r4, r0
    ! floor(sqrt(r0)), a bit pair at a time: m r6, y r7, z r4
    mov     #1, r6
    rotr    r6
    shlr    r6
    mov     #0, r7
    mov     #0, r4
6:  add     r6, r7
    cmp/hi  r0, r7
    bf      7f
    bra     8f
    mov     r4, r7
7:  sub     r7, r0
    mov     r4, r7
    add     r6, r7
8:  mov     r7, r4
    shlr    r4
    shlr2   r6
    tst     r6, r6
    bf      6b
    mov.l   @(L_AMBIENT,r14), r4
    tst     r7, r7
    bf/s    9f
    mov.l   @(L_MAX,r14), r5
    bra     .Lcap
    mov     #0, r5
9:  ! u = (d << 14) / len, then the view's rows times u >> 14
    .irp    r, r1, r2, r3
    shll8   \r
    shll2   \r
    shll2   \r
    shll2   \r
    mov     #-1, r6
    shll8   r6
    mov.l   r7, @r6
    mov.l   \r, @(4,r6)
    mov.l   @(4,r6), \r
    .endr
    mov.l   @(C_STACK,gbr), r0
    mov     r0, r6
    mov.l   .L2p18, r7
    .irp    k, 0, 1, 2
    mov.l   @(\k*16,r6), r0
    mul.l   r0, r1
    mov.l   @(\k*16+4,r6), r0
    sts     macl, r4
    mul.l   r0, r2
    mov.l   @(\k*16+8,r6), r0
    sts     macl, r5
    mul.l   r0, r3
    add     r5, r4
    sts     macl, r5
    add     r5, r4
    SAR     r4, r7
    mov     r4, r0
    mov.w   r0, @(L_VIEW+\k*2,r14)
    .endr
    mov.l   @(L_AMBIENT,r14), r4
    mov.l   @(L_MAX,r14), r5
.Lcap:
    mov.l   .L8191, r6
    add     r5, r6

    ! the fog: ambient + depth - FOG_MIN past FOG_MIN, up to the cap
.Lfog:
    mov.l   @(L_FOG,r14), r0
    mov.l   .Lfogmin, r1
    cmp/gt  r1, r0
    bf      1f
    sub     r1, r0
    add     r0, r4
    cmp/gt  r6, r4
    bf      1f
    mov     r6, r4
1:  mov.l   r4, @(0,r11)
    mov.l   r5, @(4,r11)
    mov.w   @(L_VIEW,r14), r0
    mov.w   r0, @(8,r11)
    mov.w   @(L_VIEW+2,r14), r0
    mov.w   r0, @(10,r11)
    mov.w   @(L_VIEW+4,r14), r0
    mov.w   r0, @(12,r11)
    add     #LIT, r11
    add     #ITEM, r10
    add     #POSE_LIGHT, r9
    mov.l   @(C_LEFT,gbr), r0
    dt      r0
    bt/s    .Llitdone
    mov.l   r0, @(C_LEFT,gbr)
    bra     .Llitem
    nop

.Llitdone:
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

    .align  2
.L2p18:     .long   1 << 18
.L2p20:     .long   1 << 20
.L4096:     .long   4096
.L8191:     .long   8191
.Lfogmin:   .long   1024 * 12   ! FOG_MIN

! --- the static meshes' and the shadows' transforms --------------------------
!
! void poseStaticsAsm(const PoseStatic* statics, int32 count, PoseContext* ctx)
! void poseShadowsAsm(const PoseShadow* shadows, int32 count, PoseContext* ctx)
!
! Each to its draw's DrawXform in ctx->out, as draw.h sets them up. A static
! mesh (drawRoomStatic): the view placed at it (matrixTranslateSet: plain
! products of the view's rows and its place from the camera), its columns
! turned by its quarter (matrixRotateYQ), lit by its intensity and the fog
! (calcLightingStatic; no direction). A shadow (drawShadow): its item's box
! as getBoundingBox(true) lerps it, the view placed at the item over the
! floor (matrixTranslateAbs) and turned by its yaw (matrixRotateY, .Lrot),
! the item's light from ctx->lit, the box's centre and half sizes.
!
! PoseStatic (scene.h): +0 draw, +2 quadrant, +4 intensity, +8 x y z; 20.
! PoseShadow: +0 draw, +2 item, +4 floor; 8.

    .equ    C_ITEMS,        192     ! the items, for the shadows
    .equ    S_DRAW,         0
    .equ    S_QUADRANT,     2
    .equ    S_INTENSITY,    4
    .equ    S_X,            8
    .equ    STATIC,         20
    .equ    H_DRAW,         0
    .equ    H_ITEM,         2
    .equ    H_FLOOR,        4
    .equ    SHADOW,         8
    .equ    X_SHADOW,       62

! r1 = r1 * 72: a draw's DrawXform from out; r0
    .macro  XFORM_AT
    mov     r1, r0
    shll8   r0
    shlr2   r0
    shll2   r1
    shll    r1
    add     r0, r1
    mov.l   @(C_OUT,gbr), r0
    add     r0, r1
    .endm

! a row's columns a (r5) and c (r7) turned by the quarter in r14
! (matrixRotateYQ: 0 both negated, 1 c and -a, 2 as they are, 3 -c and a); r0
    .macro  QUARTER
    mov     r14, r0
    cmp/eq  #2, r0
    bt      3f
    cmp/eq  #0, r0
    bf      1f
    neg     r5, r5
    bra     3f
    neg     r7, r7
1:  cmp/eq  #1, r0
    bf      2f
    mov     r7, r0
    neg     r5, r7
    bra     3f
    mov     r0, r5
2:  mov     r5, r0
    neg     r7, r5
    mov     r0, r7
3:
    .endm

    .global _poseStaticsAsm
_poseStaticsAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    stc.l   gbr, @-r15
    ldc     r6, gbr
    mov     r4, r10
    mov     r5, r12
    tst     r12, r12
    bf      1f
    bra     .Lstaticsdone
    nop
1:  mov.l   @(C_STACK,gbr), r0
    mov     r0, r9                  ! the view
    mov.l   .Ls2p18, r13
.Lstatic:
    mov.w   @(S_DRAW,r10), r0
    extu.w  r0, r1
    XFORM_AT
    mov     r1, r11
    ! the place from the camera
    mov.l   @(S_X,r10), r2
    mov.l   @(C_CAMERA,gbr), r0
    sub     r0, r2
    mov.l   @(S_X+4,r10), r3
    mov.l   @(C_CAMERA+4,gbr), r0
    sub     r0, r3
    mov.l   @(S_X+8,r10), r4
    mov.l   @(C_CAMERA+8,gbr), r0
    sub     r0, r4
    mov.b   @(S_QUADRANT,r10), r0
    extu.b  r0, r14
    ! each row: its translation, its columns turned
    .irp    ro, 0, 16, 32
    mov.l   @(\ro,r9), r5
    mov.l   @(\ro+4,r9), r6
    mul.l   r5, r2
    mov.l   @(\ro+8,r9), r7
    sts     macl, r8
    mul.l   r6, r3
    sts     macl, r0
    mul.l   r7, r4
    add     r0, r8
    sts     macl, r0
    add     r0, r8
    mov.l   r8, @(\ro+12,r11)
    QUARTER
    mov.l   r5, @(\ro,r11)
    mov.l   r6, @(\ro+4,r11)
    mov.l   r7, @(\ro+8,r11)
    .endr
    ! its light: its intensity - 4096, the fog past FOG_MIN, up to 8191
    mov.l   @(S_INTENSITY,r10), r1
    mov.l   .Ls4096, r0
    sub     r0, r1
    mov.l   @(44,r11), r2
    dmuls.l r2, r13
    sts     mach, r2
    mov.l   .Lsfogmin, r0
    cmp/gt  r0, r2
    bf      1f
    sub     r0, r2
    add     r2, r1
    mov.l   .Ls8191, r0
    cmp/gt  r0, r1
    bf      1f
    mov     r0, r1
1:  mov.l   r1, @(X_AMBIENT,r11)
    mov     #0, r0
    mov.l   r0, @(X_RANGE,r11)
    mov.l   r0, @(X_LIGHT,r11)
    mov     r11, r1
    add     #X_LIGHT+4, r1
    mov.w   r0, @r1
    add     #STATIC, r10
    dt      r12
    bt      .Lstaticsdone
    bra     .Lstatic
    nop
.Lstaticsdone:
    ldc.l   @r15+, gbr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    .align  2
.Ls2p18:    .long   1 << 18
.Ls2p22:    .long   1 << 22
.Ls4096:    .long   4096
.Ls8191:    .long   8191
.Lsfogmin:  .long   1024 * 12

! A box's extent along an axis, lerped from frame A's (r4) and B's (r5) by
! d (r6), getBoundingBox's int16; or frame A's when still (T): r2 min, r3
! max; r0 r1
    .macro  EXTENT k
    mov.w   @(\k,r4), r0
    mov     r0, r2
    mov.w   @(\k+2,r4), r0
    mov     r0, r3
    bt      1f
    mov.w   @(\k,r5), r0
    sub     r2, r0
    mul.l   r6, r0
    sts     macl, r0
    swap.w  r0, r0
    exts.w  r0, r0
    add     r0, r2
    exts.w  r2, r2
    mov.w   @(\k+2,r5), r0
    sub     r3, r0
    mul.l   r6, r0
    sts     macl, r0
    swap.w  r0, r0
    exts.w  r0, r0
    add     r0, r3
    exts.w  r3, r3
1:
    .endm

    .global _poseShadowsAsm
_poseShadowsAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15
    stc.l   gbr, @-r15
    ldc     r6, gbr
    mov     r4, r10
    mov     r5, r12
    tst     r12, r12
    bf      1f
    bra     .Lshadowsdone
    nop
1:  mov.l   @(0,gbr), r0
    mov     r0, r13                 ! the sines
    mov.l   @(C_STACK,gbr), r0
    mov     r0, r14
    add     #MATRIX, r14            ! the matrix made, above the view
.Lshadow:
    ! the item (104 bytes each) and its light (16)
    mov.w   @(H_ITEM,r10), r0
    extu.w  r0, r1
    mov     r1, r2
    shll2   r2
    shll2   r2
    mov.l   @(C_LIT,gbr), r0
    add     r0, r2
    mov     r2, r8                  ! its PoseLit
    mov     r1, r2
    shll2   r2
    shll    r2
    mov     r1, r3
    shll2   r3
    shll2   r3
    shll    r3
    shll2   r1
    shll2   r1
    shll2   r1
    add     r2, r1
    add     r3, r1
    mov.l   @(C_ITEMS,gbr), r0
    add     r0, r1
    mov     r1, r9                  ! its PoseItem
    ! its box, lerped: x and z extents
    mov.l   @(C_FRAMES,gbr), r0
    mov.l   @(I_FRAME_A,r9), r4
    add     r0, r4
    mov.l   @(I_FRAME_B,r9), r5
    add     r0, r5
    mov.l   @(I_DELTA,r9), r1
    tst     r1, r1
    bt      2f
    mov.l   @(C_DIV,gbr), r0
    mov     r0, r3
    mov.l   @(I_RATE,r9), r0
    shll    r0
    mov.w   @(r0,r3), r6
    extu.w  r6, r6
    mul.l   r1, r6
    sts     macl, r6                ! divTable[rate] * delta
    tst     r1, r1
2:  EXTENT  0
    mov     r2, r11                 ! minX
    mov     r3, r7                  ! maxX
    mov.l   @(I_DELTA,r9), r1
    tst     r1, r1
    EXTENT  8
    ! the centre and the half sizes: (max + min) >> 1, (max - min) 160 >> 10,
    ! kept on the stack (whole longwords: an interrupt pushes there)
    add     #-8, r15
    mov.l   .Lh2p22, r1
    mov     r7, r0
    add     r11, r0
    shar    r0
    mov.w   r0, @(6,r15)            ! x
    mov     r3, r0
    add     r2, r0
    shar    r0
    mov.w   r0, @(4,r15)            ! z
    mov     #-96, r6
    extu.b  r6, r6                  ! 160
    sub     r11, r7
    mul.l   r6, r7
    sts     macl, r7
    dmuls.l r7, r1
    sts     mach, r7
    mov     r7, r0
    mov.w   r0, @(2,r15)            ! sx
    sub     r2, r3
    mul.l   r6, r3
    sts     macl, r3
    dmuls.l r3, r1
    sts     mach, r3
    mov     r3, r0
    mov.w   r0, @(0,r15)            ! sz
    ! the view placed at the item over the floor
    mov.l   @(C_STACK,gbr), r0
    mov     r0, r1
    mov.l   @(I_POS,r9), r2
    mov.l   @(C_CAMERA,gbr), r0
    sub     r0, r2
    mov.l   @(H_FLOOR,r10), r3
    mov.l   @(C_CAMERA+4,gbr), r0
    sub     r0, r3
    mov.l   @(I_POS+8,r9), r4
    mov.l   @(C_CAMERA+8,gbr), r0
    sub     r0, r4
    .irp    ro, 0, 16, 32
    mov.l   @(\ro,r1), r5
    mul.l   r5, r2
    mov.l   r5, @(\ro,r14)
    mov.l   @(\ro+4,r1), r5
    sts     macl, r6
    mul.l   r5, r3
    mov.l   r5, @(\ro+4,r14)
    mov.l   @(\ro+8,r1), r5
    sts     macl, r7
    mul.l   r5, r4
    add     r7, r6
    mov.l   r5, @(\ro+8,r14)
    sts     macl, r7
    add     r7, r6
    mov.l   r6, @(\ro+12,r14)
    .endr
    ! turned by its yaw
    mov     r9, r7
    add     #I_ANGLE, r7
    mov.w   @(2,r7), r0
    mov     r0, r4
    ANGLE   r4
    mov     #0, r5
    bsr     .Lrot
    mov     #0, r6
    ! to its draw's DrawXform: the matrix, the item's light, the box's place
    mov.w   @(H_DRAW,r10), r0
    extu.w  r0, r1
    XFORM_AT
    mov     r1, r11
    .irp    k, 0, 16, 32
    mov.l   @(\k,r14), r0
    mov.l   @(\k+4,r14), r1
    mov.l   @(\k+8,r14), r2
    mov.l   @(\k+12,r14), r3
    mov.l   r0, @(\k,r11)
    mov.l   r1, @(\k+4,r11)
    mov.l   r2, @(\k+8,r11)
    mov.l   r3, @(\k+12,r11)
    .endr
    mov.l   @(0,r8), r1
    mov.l   @(4,r8), r2
    mov.l   @(8,r8), r3
    mov.w   @(12,r8), r0
    mov.l   r1, @(X_AMBIENT,r11)
    mov.l   r2, @(X_RANGE,r11)
    mov.l   r3, @(X_LIGHT,r11)
    mov     r11, r1
    add     #X_LIGHT+4, r1
    mov.w   r0, @r1
    add     #X_SHADOW-X_LIGHT-4, r1
    mov.w   @(0,r15), r0
    mov.w   r0, @(6,r1)             ! sz
    mov.w   @(2,r15), r0
    mov.w   r0, @(4,r1)             ! sx
    mov.w   @(4,r15), r0
    mov.w   r0, @(2,r1)             ! z
    mov.w   @(6,r15), r0
    mov.w   r0, @(0,r1)             ! x
    add     #8, r15
    add     #SHADOW, r10
    dt      r12
    bt      .Lshadowsdone
    bra     .Lshadow
    nop
.Lshadowsdone:
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

    .align  2
.Lh2p22:    .long   1 << 22

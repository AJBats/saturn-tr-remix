! Stage 2 the new way (vertices.h): the meshes fed to the DSP, a job each,
! as vertices.cpp verticesFeedC feeds them: a shadow's vertices packed; the
! bounding sphere's test (inside the frame and between the planes, or far);
! meshrot.dsp's job, its rows times 4 and its light's coefficients; how the
! mesh is shaded.
!
! void verticesFeedAsm(MeshJob* jobs, int32 count, MeshDspJob* dsp, const uint32* tails)
!   tails: a job's last three longwords (entry, place, longwords), without
!   normals then with
!
! MeshJob (vertices.cpp): +4 parts, +8 xform, +12 inside, +16 shading,
! +20 far; 24 bytes.
! MeshParts (vertices.cpp): +0 count, +8 normals, +12 shades, +16 intensity,
! +20 packed, +24 center, +28 radius.
! DrawXform (scene.h): +0 m (rows m0 m1 m2 t), +48 ambient, +52 range,
! +56 light (int16 x3), +62 shadow (int16 x, z, half sizes x, z).
! MeshDspJob (vertices.cpp): +0 k (16), +64 src, +68 groups, +72 entry,
! +76 place, +80 longwords; 84 bytes.
!
! A sum >> n is the high longword of it times 2 ^ (32 - n) (DMULS.L): the
! arithmetic shift C makes of it.

    .equ    J_PARTS,        4
    .equ    J_XFORM,        8
    .equ    J_INSIDE,       12
    .equ    J_SHADING,      16
    .equ    J_FAR,          20
    .equ    MESH_JOB,       24
    .equ    P_COUNT,        0
    .equ    P_NORMALS,      8
    .equ    P_SHADES,       12
    .equ    P_INTENSITY,    16
    .equ    P_PACKED,       20
    .equ    P_CENTER,       24
    .equ    P_RADIUS,       28
    .equ    X_AMBIENT,      48
    .equ    X_RANGE,        52
    .equ    X_LIGHT,        56
    .equ    X_SHADOW,       62
    .equ    D_K12,          48
    .equ    DSP_JOB,        84
    .equ    MESH_SHADED,    0
    .equ    MESH_SHADES,    1
    .equ    MESH_NORMALS,   2

! a row of the matrix (at r8 + o) times the sphere's centre (r1 r2 r3), plus
! the translation, >> 14 (r7 2 ^ 18) to d; r0, r13
    .macro  ROW o, d
    mov.l   @(\o, r8), r0
    mul.l   r1, r0
    mov.l   @(\o + 4, r8), r13
    sts     macl, \d
    mul.l   r2, r13
    mov.l   @(\o + 8, r8), r0
    sts     macl, r13
    mul.l   r3, r0
    add     r13, \d
    mov.l   @(\o + 12, r8), r13
    sts     macl, r0
    add     r0, \d
    add     r13, \d
    dmuls.l r7, \d
    sts     mach, \d
    .endm

! a column of the matrix (at r8 + o) times the light (r1 r2 r3) >> 21 (r7
! 2 ^ 11) to d: the light in the mesh's space (meshXformSetup); r0, r9
    .macro  LIGHT o, d
    mov.l   @(\o, r8), r0
    mul.l   r1, r0
    mov.l   @(\o + 16, r8), r9
    sts     macl, \d
    mul.l   r2, r9
    mov.l   @(\o + 32, r8), r0
    sts     macl, r9
    mul.l   r3, r0
    add     r9, \d
    sts     macl, r0
    add     r0, \d
    dmuls.l r7, \d
    sts     mach, \d
    .endm

! d = |s| (s may be d)
    .macro  ABS s, d
    cmp/pz  \s
    bt/s    1f
    mov     \s, \d
    neg     \d, \d
1:
    .endm

    .text
    .align 2
    .global _verticesFeedAsm
_verticesFeedAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    stc     gbr, r0
    mov.l   r0, @-r15
    ldc     r7, gbr             ! the tails
    tst     r5, r5
    bf      .Ljob
    bra     .Lend
    nop

    ! a shadow's 8 vertices (render.cpp shadowVertices, each coordinate
    ! int16), packed as packMesh lays a mesh without normals: x, 0, z
    ! a vertex, two groups, stored from the last back
.Lshadow:
    mov     r8, r9
    add     #X_SHADOW, r9
    mov.w   @r9+, r1            ! x
    mov.w   @r9+, r2            ! z
    mov.w   @r9+, r3            ! half size x
    mov.w   @r9, r9             ! half size z
    mov     r1, r11
    sub     r3, r11             ! xns1
    mov     r1, r12
    add     r3, r12             ! xps1
    mov     r11, r13
    sub     r3, r13             ! xns2
    mov     r12, r14
    add     r3, r14             ! xps2
    mov     r2, r1
    sub     r9, r1              ! zns1
    mov     r2, r3
    add     r9, r3              ! zps1
    mov     r1, r10
    sub     r9, r10             ! zns2
    add     r3, r9              ! zps2 (zps1 + half size z)
    mov.l   @(J_PARTS, r4), r2
    mov.l   @(P_PACKED, r2), r2
    add     #48, r2
    mov     #0, r0
    mov.w   r3, @-r2            ! group 1, from vertex 7's z back
    mov.w   r0, @-r2
    mov.w   r13, @-r2
    mov.w   r1, @-r2
    mov.w   r0, @-r2
    mov.w   r13, @-r2
    mov.w   r10, @-r2
    mov.w   r0, @-r2
    mov.w   r11, @-r2
    mov.w   r10, @-r2
    mov.w   r0, @-r2
    mov.w   r12, @-r2
    mov.w   r1, @-r2            ! group 0, from vertex 3's z back
    mov.w   r0, @-r2
    mov.w   r14, @-r2
    mov.w   r3, @-r2
    mov.w   r0, @-r2
    mov.w   r14, @-r2
    mov.w   r9, @-r2
    mov.w   r0, @-r2
    mov.w   r12, @-r2
    mov.w   r9, @-r2
    mov.w   r0, @-r2
    bra     .Lrows
    mov.w   r11, @-r2

.Ljob:
    mov.l   @(J_PARTS, r4), r9
    mov.l   @(J_XFORM, r4), r8
    mov.l   @(P_CENTER, r9), r9
    mov     #0, r0
    mov.l   r0, @(J_FAR, r4)
    tst     r9, r9
    bt/s    .Lshadow
    mov.l   r0, @(J_INSIDE, r4)

    ! the bounding sphere's centre in view space
    mov.w   @r9, r1
    mov.w   @(2, r9), r0
    mov     r0, r2
    mov.w   @(4, r9), r0
    mov     r0, r3
    mov.l   .LtwoTo18, r7
    ROW     0, r10              ! cx
    ROW     16, r11             ! cy
    ROW     32, r12             ! cz

    ! far: the sphere might reach past 30000 in z, either way
    mov.l   @(J_PARTS, r4), r13
    mov.l   @(P_RADIUS, r13), r13
    mov     r12, r14
    add     r13, r14            ! cz + radius
    mov     r12, r9
    sub     r13, r9             ! zNear = cz - radius
    mov.w   .L30000, r0
    cmp/gt  r0, r14
    bt      1f
    neg     r0, r0
    cmp/gt  r9, r0
    bf      2f
1:  mov     #1, r0
    mov.l   r0, @(J_FAR, r4)

    ! inside: in front of the near plane, short of the far one, and within
    ! the frame less 4 pixels each side
2:  mov     #64, r0             ! VIEW_MIN_F >> FIXED_SHIFT
    cmp/gt  r0, r9
    bf      .Lrows
    mov.w   .L20480, r0         ! VIEW_DIST
    cmp/gt  r14, r0
    bf      .Lrows
    ABS     r10, r0
    add     r13, r0
    shll8   r0                  ! (|cx| + radius) * 256
    mov.w   .L172, r1           ! FRAME_WIDTH / 2 - 4
    mul.l   r9, r1
    sts     macl, r1
    cmp/gt  r0, r1
    bf      .Lrows
    ABS     r11, r0
    add     r13, r0
    shll8   r0
    mov     #108, r1            ! FRAME_HEIGHT / 2 - 4
    mul.l   r9, r1
    sts     macl, r1
    cmp/gt  r0, r1
    bf      .Lrows
    mov     #1, r0
    mov.l   r0, @(J_INSIDE, r4)

    ! the rows times 4, each translation first
.Lrows:
    mov.l   @(12, r8), r0
    shll2   r0
    mov.l   r0, @r6
    mov.l   @r8, r0
    shll2   r0
    mov.l   r0, @(4, r6)
    mov.l   @(4, r8), r0
    shll2   r0
    mov.l   r0, @(8, r6)
    mov.l   @(8, r8), r0
    shll2   r0
    mov.l   r0, @(12, r6)
    mov.l   @(28, r8), r0
    shll2   r0
    mov.l   r0, @(16, r6)
    mov.l   @(16, r8), r0
    shll2   r0
    mov.l   r0, @(20, r6)
    mov.l   @(20, r8), r0
    shll2   r0
    mov.l   r0, @(24, r6)
    mov.l   @(24, r8), r0
    shll2   r0
    mov.l   r0, @(28, r6)
    mov.l   @(44, r8), r0
    shll2   r0
    mov.l   r0, @(32, r6)
    mov.l   @(32, r8), r0
    shll2   r0
    mov.l   r0, @(36, r6)
    mov.l   @(36, r8), r0
    shll2   r0
    mov.l   r0, @(40, r6)
    mov.l   @(40, r8), r0
    shll2   r0
    mov.l   r0, @(44, r6)

    ! the light: by its normals, when the DSP's arithmetic keeps a shade in
    ! 0 .. 31 (ambient - range .. ambient + range within it) and the
    ! normals' +128 comes off within int32 (|lx| + |ly| + |lz| <= 512); else
    ! by its normals here (MESH_NORMALS), by its vertices' shades, or one
    ! shade for all. r14 range * -4, r9 the ambient, r10 r11 r12 the light
    mov.l   @(X_RANGE, r8), r13
    mov     r13, r14
    shll2   r14
    neg     r14, r14
    mov.l   @(X_AMBIENT, r8), r9
    mov.l   @(J_PARTS, r4), r0
    mov.l   @(P_NORMALS, r0), r0
    tst     r0, r0
    bt      .LnoNormals
    tst     r13, r13
    bt      .LnoNormals
    mov     r8, r0
    add     #X_LIGHT, r0
    mov.w   @r0+, r1
    mov.w   @r0+, r2
    mov.w   @r0, r3
    mov.l   .LtwoTo11, r7
    LIGHT   0, r10              ! lx
    LIGHT   4, r11              ! ly
    LIGHT   8, r12              ! lz
    mov.l   @(X_AMBIENT, r8), r9
    cmp/ge  r13, r9             ! ambient >= range
    bf      .Lunsafe
    mov     r9, r0
    add     r13, r0
    mov.w   .L8191, r1
    cmp/gt  r1, r0              ! ambient + range > 0x1FFF
    bt      .Lunsafe
    ABS     r10, r0
    ABS     r11, r1
    add     r1, r0
    ABS     r12, r1
    add     r1, r0
    mov.w   .L512, r1
    cmp/gt  r1, r0
    bt      .Lunsafe

    ! by the DSP: the light's coefficients; the first less 128 times their sum
    mov     #MESH_SHADED, r0
    mov.l   r0, @(J_SHADING, r4)
    mov     r10, r0
    add     r11, r0
    add     r12, r0
    mul.l   r14, r0
    add     #1, r9
    shll16  r9
    sts     macl, r0            ! (lx + ly + lz) * range
    mul.l   r14, r10
    add     #-1, r9             ! ambient << 16 | 0xFFFF
    shll2   r0
    shll2   r0
    shll2   r0
    shll    r0                  ! bias, * 128
    sub     r0, r9
    sts     macl, r10
    mul.l   r14, r11
    mov.l   r9, @(D_K12, r6)
    mov.l   r10, @(D_K12 + 4, r6)
    sts     macl, r11
    mul.l   r14, r12
    mov.l   r11, @(D_K12 + 8, r6)
    sts     macl, r12
    bra     .Ltail
    mov.l   r12, @(D_K12 + 12, r6)

.Lunsafe:                       ! by its normals here: the ambient, no light
    bra     .Lplain
    mov     #MESH_NORMALS, r0

    .align 2                    ! within MOV.W's reach of their uses above
.L30000:        .short  30000
.L20480:        .short  20480
.L172:          .short  172
.L8191:         .short  8191
.L512:          .short  512

.LnoNormals:
    mov.l   @(J_PARTS, r4), r0
    mov.l   @(P_SHADES, r0), r0
    tst     r0, r0
    bt      .Lflat
    mov     #0, r9              ! by its vertices' shades: no ambient, no light
    bra     .Lplain
    mov     #MESH_SHADES, r0

.Lflat:                         ! one shade for all: clamp(int16((intensity + ambient) >> 8), 0, 31) << 8
    mov.l   @(J_PARTS, r4), r0
    mov.l   @(P_INTENSITY, r0), r0
    add     r0, r9
    shlr8   r9
    exts.w  r9, r9
    cmp/pz  r9
    bt      1f
    mov     #0, r9
1:  mov     #31, r0
    cmp/gt  r0, r9
    bf      2f
    mov     r0, r9
2:  shll8   r9
    mov     #MESH_SHADED, r0

.Lplain:                        ! r0 the shading, r9 the ambient; no light
    mov.l   r0, @(J_SHADING, r4)
    add     #1, r9
    shll16  r9
    add     #-1, r9
    mov.l   r9, @(D_K12, r6)
    mov     #0, r0
    mov.l   r0, @(D_K12 + 4, r6)
    mov.l   r0, @(D_K12 + 8, r6)
    mov.l   r0, @(D_K12 + 12, r6)

    ! its groups, their count, and the tail: where a group begins, where it
    ! lands, its longwords
.Ltail:
    mov.l   @(J_PARTS, r4), r3
    mov.l   @(P_PACKED, r3), r1
    mov.l   .Lphysical, r0
    and     r0, r1
    shlr2   r1
    mov.l   @(P_COUNT, r3), r2
    add     #3, r2
    shlr2   r2
    add     #64, r6
    mov.l   r1, @r6
    mov.l   r2, @(4, r6)
    mov.l   @(P_NORMALS, r3), r0
    tst     r0, r0
    bt      1f
    mov.l   @(12, gbr), r0
    mov.l   r0, @(8, r6)
    mov.l   @(16, gbr), r0
    mov.l   r0, @(12, r6)
    bra     2f
    mov.l   @(20, gbr), r0
1:  mov.l   @(0, gbr), r0
    mov.l   r0, @(8, r6)
    mov.l   @(4, gbr), r0
    mov.l   r0, @(12, r6)
    mov.l   @(8, gbr), r0
2:  mov.l   r0, @(16, r6)
    add     #DSP_JOB - 64, r6
    add     #MESH_JOB, r4
    dt      r5
    bt      .Lend
    bra     .Ljob
    nop

    ! the list's end: no groups
.Lend:
    mov     #0, r0
    add     #64, r6
    mov.l   r0, @(4, r6)
    mov.l   @r15+, r0
    ldc     r0, gbr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    .align 2
.LtwoTo18:      .long   1 << 18
.LtwoTo11:      .long   1 << 11
.Lphysical:     .long   0x07FFFFFF  ! dspAddress: physical, then in longwords

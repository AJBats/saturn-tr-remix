! The floor: each face's command and gouraud table built with nothing
! decided, from words ready made and the corners that go in them: what
! building a command costs in memory traffic alone.
!
! void floorAsm(const FloorFace* faces, int32 count, FloorXform* xf)
!
! FloorFace (scene.h): uint32 w[3]; uint16 corners[4], byte offsets into the
! vertices; uint32 flags (unread).
! FloorXform (lab.h):
!   +0  const Vertex* vertices
!   +4  uint32* cmd             the first command; returned past the last
!   +8  uint32* grd             its gouraud table
!   +12 uint32 grdLink          its last word: the table's address / 8 << 16
!   +16 const uint16* shadeGouraud  shade to gouraud colour
! Vertex: int16 x, y; int16 z; uint8 g, clip.

    .text
    .align 2
    .global _floorAsm

! the gouraud colour of corner r's shade into r, by r9; r0
    .macro  SHADE r
    mov.b   @(6, \r), r0
    extu.b  r0, r0
    add     r0, r0
    mov.w   @(r0, r9), \r
    .endm

_floorAsm:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   @r6, r8             ! vertices
    mov.l   @(4, r6), r10       ! cmd
    mov.l   @(8, r6), r11       ! grd
    mov.l   @(12, r6), r13      ! grdLink
    mov.l   @(16, r6), r9       ! shadeGouraud
    mov     #1, r12
    shll16  r12                 ! the next table's link
    tst     r5, r5
    bt      .Ldone

.Lface:
    mov.l   @r4+, r0
    mov.l   r0, @r10            ! w[0]
    mov.l   @r4+, r0
    mov.l   r0, @(4, r10)       ! w[1]
    mov.l   @r4+, r0
    mov.l   r0, @(8, r10)       ! w[2]
    mov.l   @r4+, r1            ! corners A, B
    mov.l   @r4+, r3            ! corners C, D
    add     #4, r4              ! flags
    extu.w  r1, r2
    shlr16  r1
    add     r8, r1              ! A
    add     r8, r2              ! B
    extu.w  r3, r7
    shlr16  r3
    add     r8, r3              ! C
    add     r8, r7              ! D
    mov.l   @r1, r0
    mov.l   r0, @(12, r10)      ! w[3]
    mov.l   @r2, r0
    mov.l   r0, @(16, r10)      ! w[4]
    mov.l   @r3, r0
    mov.l   r0, @(20, r10)      ! w[5]
    mov.l   @r7, r0
    mov.l   r0, @(24, r10)      ! w[6]
    mov.l   r13, @(28, r10)     ! w[7]
    SHADE   r1
    SHADE   r2
    shll16  r1
    extu.w  r2, r2
    or      r2, r1
    mov.l   r1, @r11
    SHADE   r3
    SHADE   r7
    shll16  r3
    extu.w  r7, r7
    or      r7, r3
    mov.l   r3, @(4, r11)
    add     #32, r10
    add     #8, r11
    dt      r5
    bf/s    .Lface
    add     r12, r13

.Ldone:
    mov.l   r10, @(4, r6)
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

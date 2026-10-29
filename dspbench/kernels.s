! The CPUs' memory loads for the bus tests (bench.cpp): each makes one kind
! of access, n of them, with as little else as the loop needs.

    .text
    .align 2

! void kReadLines(const void* p, uint32 n)
!   a longword from each of n 16-byte lines: from a buffer larger than the
!   cache, each a line fill
    .global _kReadLines
_kReadLines:
1:  mov.l   @r4, r0
    dt      r5
    bf/s    1b
    add     #16, r4
    rts
    nop

! void kReadLongs(const void* p, uint32 n)
!   n longwords one after another, n a multiple of 4: a line fill each 4
!   through the cache, a bus read each past it
    .global _kReadLongs
_kReadLongs:
    shlr2   r5
1:  mov.l   @r4+, r0
    mov.l   @r4+, r1
    mov.l   @r4+, r2
    mov.l   @r4+, r3
    dt      r5
    bf      1b
    rts
    nop

! void kWriteLongs(void* p, uint32 n)
!   n longwords one after another, n a multiple of 4: each a bus write (the
!   cache writes through)
    .global _kWriteLongs
_kWriteLongs:
    shlr2   r5
    mov     #0, r0
1:  mov.l   r0, @r4
    mov.l   r0, @(4, r4)
    mov.l   r0, @(8, r4)
    mov.l   r0, @(12, r4)
    dt      r5
    bf/s    1b
    add     #16, r4
    rts
    nop

! void kRotate(const int32* points, const int32* rows, int32* out, uint32 n)
!   page 2's rotation on the SH-2: each of n points (x y z) by each row (m0
!   m1 m2 t), (v . row + t) >> 16 over 64 bits, x' y' z' a point out; mac.l
!   takes both operands from memory
    .global _kRotate
_kRotate:
    mov.l   r9, @-r15
1:  mov     r5, r1              ! the rows
    mov     #3, r9
2:  mov     r4, r2              ! the point
    mov.l   @(12, r1), r0       ! t, over 64 bits
    mov     r0, r3
    shll    r3
    subc    r3, r3
    lds     r3, mach
    lds     r0, macl
    mac.l   @r2+, @r1+
    mac.l   @r2+, @r1+
    mac.l   @r2+, @r1+
    add     #4, r1              ! past t
    sts     mach, r3
    sts     macl, r0
    xtrct   r3, r0              ! bits 16..47
    mov.l   r0, @r6
    dt      r9
    bf/s    2b
    add     #4, r6
    dt      r7
    bf/s    1b
    add     #12, r4
    rts
    mov.l   @r15+, r9

! void kRegisters(uint32 n)
!   n passes of a loop that touches no memory
    .global _kRegisters
_kRegisters:
1:  dt      r4
    bf      1b
    rts
    nop

! void kFaces(void* p, uint32 n)
!   a stand-in for adding n of a mesh's faces (page_meshes.cpp): what the
!   game's face loops did a face at the ledge, about 100 cycles of which 3
!   stores, the rest in registers; the stores one after another from p
    .global _kFaces
_kFaces:
    mov     #1, r1
    mov     #3, r2
1:  mov.l   r1, @r4
    .rept 10
    add     r2, r1
    xor     r1, r3
    shll    r2
    .endr
    mov.l   r3, @(4, r4)
    .rept 10
    add     r3, r1
    xor     r1, r2
    shlr    r3
    .endr
    mov.l   r2, @(8, r4)
    .rept 10
    add     r1, r2
    xor     r2, r3
    shll    r1
    .endr
    dt      r5
    bf/s    1b
    add     #12, r4
    rts
    nop

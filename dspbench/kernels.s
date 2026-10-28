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

! void kRegisters(uint32 n)
!   n passes of a loop that touches no memory
    .global _kRegisters
_kRegisters:
1:  dt      r4
    bf      1b
    rts
    nop

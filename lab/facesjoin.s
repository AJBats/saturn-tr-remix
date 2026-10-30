! Stage 3 the new way (faces.h): the two CPUs' lists' buckets joined far to
! near, in each the rooms' chain before the meshes' (faces.cpp facesJoin):
! a range of the buckets, so that each CPU can take a part.
!
! uint16* facesJoinPairs(CommandList* rooms, CommandList* meshes, uint8* linkBase,
!                        uint16* field, uint32 pairs)
!
! linkBase: where the link of the command VDP1 links to as 0 would lie (a
! command's link field is at linkBase + its link * 8); field: the link field
! the range's first chain goes into; pairs: the range's nearest pair of
! buckets << 16 | its farthest. Each list's heads hold OT_SIZE + 1 buckets
! (the last always empty), so that they go two to a longword; the join
! empties the range's for the next frame. Returns the link field the next
! chain goes into: the range's last command's.
!
! CommandList: +20 heads (uint16, 0 for none), +24 tails (uint16).

    .equ    OT_SIZE,    1281

! the chain so far on to head h (a register), the next link field the first
! command's of bucket r0 / 2 (+ 1 when odd) in tails t; r4
    .macro  CHAIN h, t, odd
    mov.w   \h, @r13
    mov     \t, r4
    add     r0, r4
    .if \odd
    add     #2, r4
    .endif
    mov.w   @r4, r4
    extu.w  r4, r4
    shll2   r4
    shll    r4
    add     r12, r4
    mov     r4, r13
    .endm

    .text
    .align 2
    .global _facesJoinPairs
_facesJoinPairs:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    mov.l   @(20, r4), r8       ! the rooms' heads
    mov.l   @(24, r4), r9       ! ... tails
    mov.l   @(20, r5), r10      ! the meshes' heads
    mov.l   @(24, r5), r11      ! ... tails
    mov     r6, r12
    mov     r7, r13             ! the link field the first chain goes into
    mov.l   @(28, r15), r0      ! the pairs
    extu.w  r0, r14
    shlr16  r0
    shll2   r0
    mov     r0, r7              ! the nearest pair's offset
    shll2   r14
    mov     r14, r0             ! the farthest's

.Lpair:
    mov.l   @(r0, r8), r1       ! the rooms' buckets 2p (high), 2p + 1 (low)
    mov.l   @(r0, r10), r2      ! the meshes'
    mov     r1, r3
    or      r2, r3
    tst     r3, r3
    bf      .Lfull
.Lnext:
    add     #-4, r0
    cmp/ge  r7, r0
    bt      .Lpair

    mov     r13, r0             ! the next chain's link field
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    rts
    mov.l   @r15+, r8

    ! far to near: bucket 2p + 1, then 2p
.Lfull:
    extu.w  r1, r3
    tst     r3, r3
    bt      1f
    CHAIN   r3, r9, 1
1:  extu.w  r2, r3
    tst     r3, r3
    bt      2f
    CHAIN   r3, r11, 1
2:  shlr16  r1
    tst     r1, r1
    bt      3f
    CHAIN   r1, r9, 0
3:  shlr16  r2
    tst     r2, r2
    bt      4f
    CHAIN   r2, r11, 0
4:  mov     #0, r3
    mov.l   r3, @(r0, r8)       ! empty for the next frame
    bra     .Lnext
    mov.l   r3, @(r0, r10)


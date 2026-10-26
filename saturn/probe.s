! Instruction timing probes for the measurement table (debug.cpp, HW_LOG
! builds): each runs 'count' passes of eight of one kind of instruction, to
! time against a pass of eight nops.
!
! void probeNop(int32 count)
! void probeMac(int32 count, const int16* buf)            mac.w, both from buf
! void probeMacStream(int32 count, const int16* stream, const int16* buf)
!                                                         one operand streaming
! void probeMuls(int32 count)                             muls.w, sts macl
! void probeDmuls(int32 count)                            dmuls.l, sts macl
! void probeStoreLoad(int32 count, uint32* buf)           mov.l store, then load

    .text
    .align 2

    .global _probeNop
_probeNop:
1:  nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    dt      r4
    bf      1b
    rts
    nop

    .global _probeMac
_probeMac:
    clrmac
1:  mov     r5, r0
    mov     r5, r1
    mac.w   @r0+, @r1+
    mac.w   @r0+, @r1+
    mac.w   @r0+, @r1+
    mac.w   @r0+, @r1+
    mac.w   @r0+, @r1+
    mac.w   @r0+, @r1+
    mac.w   @r0+, @r1+
    mac.w   @r0+, @r1+
    dt      r4
    bf      1b
    rts
    nop

    .global _probeMacStream
_probeMacStream:
    clrmac
1:  mov     r6, r1
    mac.w   @r5+, @r1+
    mac.w   @r5+, @r1+
    mac.w   @r5+, @r1+
    mac.w   @r5+, @r1+
    mac.w   @r5+, @r1+
    mac.w   @r5+, @r1+
    mac.w   @r5+, @r1+
    mac.w   @r5+, @r1+
    dt      r4
    bf      1b
    rts
    nop

    .global _probeMuls
_probeMuls:
    mov     #3, r1
    mov     #5, r2
1:  muls.w  r1, r2
    sts     macl, r3
    muls.w  r1, r2
    sts     macl, r3
    muls.w  r1, r2
    sts     macl, r3
    muls.w  r1, r2
    sts     macl, r3
    muls.w  r1, r2
    sts     macl, r3
    muls.w  r1, r2
    sts     macl, r3
    muls.w  r1, r2
    sts     macl, r3
    muls.w  r1, r2
    sts     macl, r3
    dt      r4
    bf      1b
    rts
    nop

    .global _probeDmuls
_probeDmuls:
    mov     #3, r1
    mov     #5, r2
1:  dmuls.l r1, r2
    sts     macl, r3
    dmuls.l r1, r2
    sts     macl, r3
    dmuls.l r1, r2
    sts     macl, r3
    dmuls.l r1, r2
    sts     macl, r3
    dmuls.l r1, r2
    sts     macl, r3
    dmuls.l r1, r2
    sts     macl, r3
    dmuls.l r1, r2
    sts     macl, r3
    dmuls.l r1, r2
    sts     macl, r3
    dt      r4
    bf      1b
    rts
    nop

    .global _probeStoreLoad
_probeStoreLoad:
1:  mov.l   r0, @r5
    mov.l   @(4, r5), r1
    mov.l   r0, @(8, r5)
    mov.l   @(12, r5), r1
    mov.l   r0, @(16, r5)
    mov.l   @(20, r5), r1
    mov.l   r0, @(24, r5)
    mov.l   @(28, r5), r1
    dt      r4
    bf      1b
    rts
    nop

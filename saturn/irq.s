! Interrupt entry points registered with the BIOS (BIOS_SET_SINT).
!
! The BIOS dispatcher calls these as subroutines (they return with rts) and
! does not save the caller-saved registers, so the entries do, then call the
! C handlers with the stack holding all that (crash.cpp reads the interrupted
! context from it).

    .macro IRQ_ENTRY name, handler
    .text
    .align 2
    .global \name
\name:
    mov.l   r0, @-r15
    mov.l   r1, @-r15
    mov.l   r2, @-r15
    mov.l   r3, @-r15
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   r6, @-r15
    mov.l   r7, @-r15
    sts.l   pr, @-r15
    sts.l   mach, @-r15
    sts.l   macl, @-r15
    stc.l   gbr, @-r15

    mov.l   1f, r0
    jsr     @r0
    mov     r15, r4

    ldc.l   @r15+, gbr
    lds.l   @r15+, macl
    lds.l   @r15+, mach
    lds.l   @r15+, pr
    mov.l   @r15+, r7
    mov.l   @r15+, r6
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    mov.l   @r15+, r3
    mov.l   @r15+, r2
    mov.l   @r15+, r1
    rts
    mov.l   @r15+, r0

    .align 2
1:  .long \handler
    .endm

! The sampling profiler's timer interrupt (debug.cpp, HW_LOG builds), straight
! from the vector table: profileHandler with the interrupted PC, then rte.
    .text
    .align 2
    .global _profileEntry
_profileEntry:
    mov.l   r0, @-r15
    mov.l   r1, @-r15
    mov.l   r2, @-r15
    mov.l   r3, @-r15
    mov.l   r4, @-r15
    mov.l   r5, @-r15
    mov.l   r6, @-r15
    mov.l   r7, @-r15
    sts.l   pr, @-r15
    sts.l   mach, @-r15
    sts.l   macl, @-r15
    stc.l   gbr, @-r15

    mov.l   2f, r0
    jsr     @r0
    mov.l   @(48, r15), r4      ! the exception frame's PC

    ldc.l   @r15+, gbr
    lds.l   @r15+, macl
    lds.l   @r15+, mach
    lds.l   @r15+, pr
    mov.l   @r15+, r7
    mov.l   @r15+, r6
    mov.l   @r15+, r5
    mov.l   @r15+, r4
    mov.l   @r15+, r3
    mov.l   @r15+, r2
    mov.l   @r15+, r1
    mov.l   @r15+, r0
    rte
    nop

    .align 2
2:  .long _profileHandler
    .weak _profileHandler       ! HW_LOG builds only, which alone install the entry

    IRQ_ENTRY _vblankInEntry, _vblankInHandler
    IRQ_ENTRY _vblankOutEntry, _vblankOutHandler
    IRQ_ENTRY _spriteEndEntry, _spriteEndHandler
    IRQ_ENTRY _lineEntry, _lineHandler
    IRQ_ENTRY _dspEndEntry, _dspEndHandler

! CPU exceptions, on either CPU: crashException with the exception frame (PC,
! SR), PR, the vector and whether it is the slave. It doesn't return.
    .macro EXC_ENTRY name, vector, slave
    .text
    .align 2
    .global \name
\name:
    mov     r15, r4
    sts     pr, r5
    mov     #\vector, r6
    mov.l   1f, r0
    jmp     @r0
    mov     #\slave, r7
    .align 2
1:  .long _crashException
    .endm

    EXC_ENTRY _crashIllegalEntry, 4, 0
    EXC_ENTRY _crashSlotEntry, 6, 0
    EXC_ENTRY _crashAddressEntry, 9, 0
    EXC_ENTRY _crashDmaEntry, 10, 0
    EXC_ENTRY _crashSlaveIllegalEntry, 4, 1
    EXC_ENTRY _crashSlaveSlotEntry, 6, 1
    EXC_ENTRY _crashSlaveAddressEntry, 9, 1
    EXC_ENTRY _crashSlaveDmaEntry, 10, 1

! Slave CPU entry after SMPC SSHON (the BIOS has set its stack): mask
! interrupts, point GBR at its CpuState (common.h), then run slaveMain.
    .global _slaveEntry
    .align 2
_slaveEntry:
    mov.l   1f, r0
    ldc     r0, sr
    mov.l   3f, r0
    ldc     r0, gbr
    mov.l   2f, r0
    jmp     @r0
    nop

    .align 2
1:  .long 0xF0
2:  .long _slaveMain
3:  .long _gSlaveState

! Slave interrupts that may arrive before slaveEntry masks them.
    .global _slaveIgnoreIrq
    .align 2
_slaveIgnoreIrq:
    rte
    nop

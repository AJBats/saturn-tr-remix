! Interrupt entry points registered with the BIOS (BIOS_SET_SINT).
!
! The BIOS dispatcher calls these as subroutines (they return with rts) and
! does not save the caller-saved registers, so the entries do, then call the
! C handlers.

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
    nop

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

    IRQ_ENTRY _vblankInEntry, _vblankInHandler
    IRQ_ENTRY _vblankOutEntry, _vblankOutHandler
    IRQ_ENTRY _spriteEndEntry, _spriteEndHandler

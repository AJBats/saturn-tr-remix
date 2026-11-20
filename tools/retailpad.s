! Retail's pad from tables, for tools/retailrun.py: S_UpdateInput's call to the
! pad scan (pool word 0x06037644) turned to this. The scan runs as before; then,
! once armed, pad 1's raw word (0x0606e404, active low) is set, pads 2 and 3
! read as nothing held:
! - called for a game tick (S_UpdateInput called by ControlPhase): from the tick
!   table, one entry a tick; Lara's item, the control seed and the level, as
!   they stand before the tick, go to a 256-entry ring first;
! - called by the inventory's own loop (Display_Inventory, a frame at a time)
!   or the level's statistics screen or its "Save Game?" prompt:
!   from the script, one entry a call, up to a 0 that ends the opening (held
!   there till the next game tick steps past it) or the 1 that ends them all;
! - called from anywhere else (menus, statistics): the real pad.
!
! The code starts at its load address; the block `vars` after it (its offset in
! the symbol table) holds, set by the driver:
!   +0   armed (long, 0 = off)
!   +4   index (long): the tick table's next entry, the ticks fed so far
!   +8   tick table address (long): halfwords, one a tick
!   +12  ring address (long): 256 entries of 96 bytes
!   +16  stop at (long): when the index reaches it, the game holds here (before
!        that tick) till the driver changes it
!   +20  script address (long): halfwords, one an inventory frame
!   +24  script position (long)
!   +28  the item recorded in the ring (long): its address, 0 for Lara
! A ring entry: index (long), seed (long), level (long), 0 (long), Lara's item (72 bytes), pad (8)
! Only r0-r7 are used: the caller keeps the rest.

    .section .text
    .global _start
_start:
    sts.l   pr,@-r15
    mov.l   scan,r0
    jsr     @r0
    nop
    mova    vars,r0
    mov     r0,r1           ! r1: vars, read through the cache (the driver writes them)
    mov.l   through,r0
    or      r0,r1
    mov.l   @r1,r0          ! armed?
    tst     r0,r0
    bt      out
    mov.l   @(4,r15),r0     ! S_UpdateInput's return address, as it saved it
    mov.l   tick,r2
    cmp/eq  r2,r0
    bt      game
    mov.l   inv,r2
    cmp/eq  r2,r0
    bt      script
    mov.l   stats,r2
    cmp/eq  r2,r0
    bt      script
    mov.l   save,r2
    cmp/eq  r2,r0
    bf      out
script:
    ! an inventory frame: the script's next entry, if this opening has one
    mov.l   @(20,r1),r4
    mov.l   @(24,r1),r3
    mov     r3,r5
    shll    r5
    add     r5,r4
    mov.w   @r4,r0
    extu.w  r0,r0
    mov     #1,r2
    cmp/hi  r2,r0           ! 0 and 1 end it: nothing held
    bf      none
    add     #1,r3
    mov.l   r3,@(24,r1)
    bra     feed
    nop
none:
    mov     #-1,r0
    bra     feed
    nop

game:
    mov.l   @(4,r1),r3      ! index
hold:
    mov.l   @(16,r1),r0     ! held before this tick while stop-at is it
    cmp/eq  r0,r3
    bt      hold
    ! the script's end of an opening stepped past: the next opening reads on
    mov.l   @(20,r1),r4
    mov.l   @(24,r1),r6
    mov     r6,r5
    shll    r5
    add     r5,r4
    mov.w   @r4,r0
    extu.w  r0,r0
    tst     r0,r0
    bf      entry
    add     #1,r6
    mov.l   r6,@(24,r1)
entry:
    ! the ring entry
    mov     r3,r4
    mov.w   mask,r0
    and     r0,r4
    mov     r4,r5
    shll2   r4
    shll2   r4
    shll    r4              ! *32
    shll2   r5
    shll2   r5
    shll2   r5              ! *64
    add     r5,r4           ! *96
    mov.l   @(12,r1),r5
    add     r4,r5           ! r5: the entry
    mov.l   r3,@r5
    mov.l   seed,r0
    mov.l   @r0,r0
    mov.l   r0,@(4,r5)
    mov.l   level,r0
    mov.l   @r0,r0
    mov.l   r0,@(8,r5)
    mov     #0,r0
    mov.l   r0,@(12,r5)
    mov.l   @(28,r1),r6     ! the item watched, if any
    tst     r6,r6
    bf      have
    mov.l   lara,r0
    mov.l   @r0,r6          ! Lara's item
have:
    mov     r5,r7
    add     #16,r7
    mov     #18,r2
copy:
    mov.l   @r6+,r0
    mov.l   r0,@r7
    dt      r2
    bf/s    copy
    add     #4,r7
    ! the tick table's entry
    mov.l   @(8,r1),r4
    mov     r3,r5
    shll    r5
    add     r5,r4
    mov.w   @r4,r0
    add     #1,r3
    mov.l   r3,@(4,r1)
feed:
    mov.l   pad,r7          ! pad 1 as r0; 2 and 3 nothing held
    mov.w   r0,@r7
    mov     #-1,r0
    mov.w   r0,@(2,r7)
    mov.w   r0,@(4,r7)
out:
    lds.l   @r15+,pr
    rts
    nop
    .align 2
scan:   .long 0x06043720
tick:   .long 0x0600f6b6    ! ControlPhase, after its call to S_UpdateInput
inv:    .long 0x060175c0    ! Display_Inventory's loop, after its call
stats:  .long 0x060167f4    ! the level's statistics screen, after its call
save:   .long 0x0602cbe2    ! the "Save Game?" prompt, after its call
seed:   .long 0x06060d34
level:  .long 0x06060cb4
lara:   .long 0x060a00e4
pad:    .long 0x0606e404
through: .long 0x20000000
mask:   .word 255
    .align 2
    .global vars
vars:   .long 0, 0, 0, 0, -1, 0, 0, 0

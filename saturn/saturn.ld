/* 0MAIN.BIN: loaded by the BIOS at 0x06004000 in high work RAM. */
OUTPUT_FORMAT("elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(start)

MEMORY
{
    HWRAM (rwx) : ORIGIN = 0x06004000, LENGTH = 0x000FC000
}

SECTIONS
{
    /* The frame's hot code first, in a fixed order: what changes elsewhere
       can't move it in the cache, nor the frame's time with it: the
       renderer's assembly (faces, vertices, pose, the draw list, the walk)
       and C, the game's calls into it, then the lab's copy of the old
       renderer's (the game's way it measures against). C functions are in
       sections of their own (-ffunction-sections). */
    .text : {
        KEEP(*(.text.start))
        *facesroom.s.o(.text)
        *facesmesh.s.o(.text)
        *facesother.s.o(.text)
        *facesjoin.s.o(.text)
        *roomproject.s.o(.text)
        *meshproject.s.o(.text)
        *meshfeed.s.o(.text)
        *pose.s.o(.text)
        *draws.s.o(.text)
        *rooms.s.o(.text)
        *(.text._ZN6render*)
        *(.text.*shadeTableMiss*)
        *(.text.*flush_c*)
        *(.text.*renderModel*)
        *(.text.*renderStatic*)
        *(.text.*renderSprite*)
        *(.text.*drawRoomObjects*)
        *(.text.startup.main)
        *gameflush.s.o(.text)
        *gamefaces.s.o(.text)
        *gamemeshxform.s.o(.text)
        *gamexform.s.o(.text)
        __hot_end = .;
        *(.text .text.*)
        __text_end = .;
    } > HWRAM

    /* on a 1 KB boundary (the cache's sets repeat every 1 KB): as code grows
       or shrinks, the data after it keeps its sets */
    .rodata ALIGN(1024) : { *(.rodata .rodata.*) } > HWRAM

    .init_array : {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(.ctors))
        __init_array_end = .;
    } > HWRAM

    .data : { *(.data .data.*) } > HWRAM

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > HWRAM

    __stack_top = ORIGIN(HWRAM) + LENGTH(HWRAM);

    /* the master's stack grows down from the top into what bss leaves: it
       went 780 bytes deep through boot, a level's load, play and the
       inventory (Mednafen, 2026-10-27, HW_LOG build) */
    ASSERT(__stack_top - __bss_end >= 0x1000, "less than 4 KB of high work RAM left for the master's stack")

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.eh_frame_hdr) }
}

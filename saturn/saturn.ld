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
       can't move it in the cache, nor the frame's time with it. Each CPU's
       stages lie together: the flush both run, the face and vertex loops,
       the master's per-mesh chain, the slave's room loop. C functions are in
       sections of their own (-ffunction-sections). */
    .text : {
        KEEP(*(.text.start))
        *flush.s.o(.text)
        *faces.s.o(.text)
        *meshxform.s.o(.text)
        *xform.s.o(.text)
        *(.text.*renderMesh*)
        *(.text.*renderStaticMesh*)
        *(.text.*meshParts*)
        *(.text.*meshXformSetup*)
        *(.text.*transformMesh*)
        *(.text.*drawMesh*)
        *(.text.*drawModel*)
        *(.text.*drawNodes*)
        *(.text.*drawLaraNodes*)
        *(.text.*getFrames*)
        *(.text.*getRelative*)
        *(.text.*matrix*)
        *(.text.*calcLighting*)
        *(.text.*sphereIsVisible*)
        *(.text.*drawRoomObjects*)
        *(.text.*checkPortal*)
        *(.text.*renderSprite*)
        *(.text.*flush_c*)
        *(.text.startup.main)
        *(.text.*roomBatch*)
        *(.text.*roomBegin*)
        *(.text.*dspProject*)
        *(.text.*meshJobDraw*)
        *(.text.shadeTableMiss)
        *(.text.meshwarm)
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

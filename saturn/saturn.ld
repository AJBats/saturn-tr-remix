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
    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
        __text_end = .;
    } > HWRAM

    .rodata : { *(.rodata .rodata.*) } > HWRAM

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

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.eh_frame_hdr) }
}

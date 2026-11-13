OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    .text 0x1000000 :
    {
        /* Collect .text.* too, so compiler-generated subsections such as
         * the x86 register thunks are not left as orphans placed after the
         * read-only sections. */
        *(.text .text.*)
        . = ALIGN(4096);
    }
    
    .data :
    {
        *(.data)
        . = ALIGN(4096);
    }

    .rodata :
    {
        *(.rodata)
        . = ALIGN(4096);
    }

    .bss :
    {
        *(.bss)
        *(COMMON)
        . = ALIGN(4096);
    }

    /* No unwinder or runtime metadata consumer here; dropping these keeps
     * them from being emitted as orphan sections between the loadable ones. */
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note .note.*)
    }
}
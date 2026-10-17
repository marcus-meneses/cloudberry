ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); ro PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x400000;
    .text : { *(.text .text.*) } :code
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) } :ro
    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss : { *(.bss .bss.* COMMON) } :data
    /DISCARD/ : { *(.comment .eh_frame .note .note.*) }
}

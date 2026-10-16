OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
PHDRS { code PT_LOAD FLAGS(5); state PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0;
    image_start = .;
    .module_header : {
        QUAD(0x32444f4d4b424343)
        LONG(2) LONG(88)
        QUAD(image_end) QUAD(memory_end) QUAD(writable_start)
        QUAD(exports_start) LONG((exports_end - exports_start) / 48) LONG(48)
        KEEP(*(.module_name))
    } :code
    ASSERT(SIZEOF(.module_header) == 88, "module name must occupy exactly 32 bytes")
    .module_exports : {
        exports_start = .;
        KEEP(*(.module_exports))
        exports_end = .;
    } :code
    . = ALIGN(16);
    text_start = .;
    .text : { *(.text .text.*) } :code
    .rodata : { *(.rodata .rodata.*) } :code
    text_end = .;
    . = ALIGN(4096);
    writable_start = .;
    .data : { *(.data .data.*) *(.data.rel.ro .data.rel.ro.*) } :state
    .dynamic : { *(.dynamic) } :state
    .rela.dyn : { *(.rela.dyn) } :state
    acpi_relocations_start = ADDR(.rela.dyn);
    acpi_relocations_end = ADDR(.rela.dyn) + SIZEOF(.rela.dyn);
    .dynsym : { *(.dynsym) } :state
    .dynstr : { *(.dynstr) } :state
    .gnu.hash : { *(.gnu.hash) } :state
    /* At least one byte ensures objcopy materializes RX padding. */
    .image_tail : { BYTE(0) } :state
    image_end = .;
    . = ALIGN(16);
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :state
    . = ALIGN(4096);
    memory_end = .;
    .got : { *(.got*) } :state
    ASSERT(SIZEOF(.got) == 0, "module must not require runtime GOT relocations")
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}

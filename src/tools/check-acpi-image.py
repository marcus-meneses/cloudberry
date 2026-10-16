#!/usr/bin/env python3
"""Check ACPI's private PIE fixups without extending the format2 loader ABI."""
import pathlib
import struct
import sys


def validate(elf, image):
    assert elf[:6] == b'\x7fELF\x02\x01', 'expected little-endian ELF64'
    section_offset = struct.unpack_from('<Q', elf, 40)[0]
    section_bytes, section_count, names_index = struct.unpack_from('<HHH', elf, 58)
    assert section_bytes == 64
    sections = [struct.unpack_from('<IIQQQQIIQQ', elf, section_offset+i*64) for i in range(section_count)]
    names_section = sections[names_index]
    names = elf[names_section[4]:names_section[4]+names_section[5]]
    named = {names[s[0]:].split(b'\0', 1)[0].decode(): s for s in sections}
    symbols = {}
    symtab = named['.symtab']
    strtab = sections[symtab[6]]
    strings = elf[strtab[4]:strtab[4]+strtab[5]]
    for offset in range(symtab[4], symtab[4]+symtab[5], 24):
        name, _, _, _, value, _ = struct.unpack_from('<IBBHQQ', elf, offset)
        symbols[strings[name:].split(b'\0', 1)[0].decode()] = value
    magic, version, header, size, memory, writable, exports, count, entry = struct.unpack_from('<QIIQQQQII', image)
    assert magic == 0x32444f4d4b424343 and version == 2 and header == 88 and entry == 48
    assert size == len(image) and memory >= size and memory % 4096 == 0
    assert writable == symbols['writable_start'] and memory == symbols['memory_end']
    rela = named['.rela.dyn']
    assert rela[5] and rela[5] % 24 == 0 and rela[9] == 24
    assert symbols['acpi_relocations_start'] == rela[3]
    assert symbols['acpi_relocations_end'] == rela[3]+rela[5], 'relocation markers exclude linker-generated records'
    assert rela[3]+rela[5] <= size and named.get('.got', (0,)*6)[5] == 0
    export_offsets = {exports+i*entry+32 for i in range(count)}
    targets = set()
    for offset in range(rela[3], rela[3]+rela[5], 24):
        target, kind, addend = struct.unpack_from('<QQq', image, offset)
        assert kind == 8 and 0 <= addend < memory, 'only local R_X86_64_RELATIVE fixups are supported'
        assert target <= memory-8 and target not in targets
        targets.add(target)
        if target < writable:
            assert target in export_offsets and struct.unpack_from('<Q', image, target)[0] == addend
        else:
            assert not (rela[3] <= target < rela[3]+rela[5]), 'fixup must not rewrite its own metadata'
    assert export_offsets <= targets
    return len(targets)


if __name__ == '__main__':
    count = validate(pathlib.Path(sys.argv[1]).read_bytes(), pathlib.Path(sys.argv[2]).read_bytes())
    print(f'ACPI image: {count} checked local relocations; hub exports remain relative/RO')

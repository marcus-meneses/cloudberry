#!/usr/bin/env python3
"""Reject runtime absolute addresses in format3 module code/data before linking."""
import pathlib
import struct
import sys

for filename in sys.argv[1:]:
    image = pathlib.Path(filename).read_bytes()
    assert image[:6] == b'\x7fELF\x02\x01'
    section_offset = struct.unpack_from('<Q', image, 40)[0]
    section_bytes, section_count, names_index = struct.unpack_from('<HHH', image, 58)
    assert section_bytes == 64
    sections = [struct.unpack_from('<IIQQQQIIQQ', image, section_offset+i*64) for i in range(section_count)]
    strings = sections[names_index]
    names = image[strings[4]:strings[4]+strings[5]]
    for section in sections:
        if section[1] != 4:  # SHT_RELA
            continue
        target = sections[section[7]]
        if not target[2] & 2:  # SHF_ALLOC: debug relocations aren't runtime state.
            continue
        name = names[target[0]:].split(b'\0', 1)[0].decode()
        if name == '.module_exports':
            continue  # These are intentionally image-relative loader offsets.
        assert section[9] == 24
        for offset in range(section[4], section[4]+section[5], 24):
            location, info, _ = struct.unpack_from('<QQq', image, offset)
            if info & 0xffffffff in (1, 10, 11):
                raise SystemExit(f'{filename}: absolute runtime relocation in {name}+{location:#x}; construct pointers at runtime')
print('PIC check: no absolute runtime pointers in module code/data')

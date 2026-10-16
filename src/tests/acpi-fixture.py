#!/usr/bin/env python3
"""Tiny test-only SSDT builder. AML fixtures, never candidate firmware/code."""
import struct
import sys

def package(op, body):
    for extra in range(4):
        length = len(body) + 1 + extra
        if length < (64 if extra == 0 else 1 << (4 + 8 * extra)):
            encoded = bytes([length]) if not extra else bytes([(extra << 6) | (length & 15)]) + (length >> 4).to_bytes(extra, 'little')
            return op + encoded + body
    raise ValueError('fixture too large')

def integer(value):
    return b'\x0a' + bytes([value]) if value < 256 else b'\x0c' + struct.pack('<I', value)

def name(key, value):
    return b'\x08' + key.encode('ascii') + value

def string(value):
    return b'\x0d' + value.encode('ascii') + b'\0'

def buffer(value):
    return package(b'\x11', integer(len(value)) + value)

def device(key, body):
    return package(b'\x5b\x82', key.encode('ascii') + body)

def fixture():
    memory = b'\x86\x09\0\x01' + struct.pack('<II', 0xfedc0000, 4096)
    irq = b'\x89\x06\0\x0d\x01' + struct.pack('<I', 17)
    io = b'\x47\x01' + struct.pack('<HHBB', 0x1234, 0x1234, 1, 4)
    body = device('CBT0', name('_HID', string('CBB0001')) + name('_UID', integer(7)) + name('_STA', integer(15)) + name('_CRS', buffer(memory + irq + io + b'\x79\0')) + package(b'\x14', b'TINT\0\xa4' + integer(42)) + name('TBUF', buffer(b'cloudberry')))
    body += device('CBDP', name('_HID', string('CBB0002')) + name('_DEP', package(b'\x12', b'\x01\\\x2e_SB_CBT0')))
    body += device('CBAD', name('_HID', string('CBB0003')) + name('_CRS', integer(12)))
    # A non-PCI device with _ADR must remain a platform device, not disappear.
    body += device('CBNA', name('_HID', string('CBB0004')) + name('_ADR', integer(3)))
    memory24 = b'\x81\x09\0\x01' + struct.pack('<HHHH', 0x1000, 0x1000, 0x100, 16)
    memory24_zero_alignment = b'\x81\x09\0\x01' + struct.pack('<HHHH', 0x2000, 0x2000, 0, 16)
    body += device('CB24', name('_HID', string('CBB0005')) + name('_CRS', buffer(memory24 + memory24_zero_alignment + b'\x79\0')))
    aml = package(b'\x10', b'\\_SB_' + body)
    header = struct.pack('<4sIBB6s8sI4sI', b'SSDT', 36+len(aml), 2, 0, b'CBERRY', b'ACPITEST', 1, b'CBRY', 1)
    result = bytearray(header + aml)
    result[9] = -sum(result) & 255
    return bytes(result)

if __name__ == '__main__':
    with open(sys.argv[1], 'wb') as output:
        output.write(fixture())

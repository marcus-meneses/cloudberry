#!/usr/bin/env python3
"""Pack artwork as width/height (little endian u32) plus top-down RGB pixels."""
import struct
import sys
from PIL import Image

with Image.open(sys.argv[1]) as source:
    rgba = source.convert('RGBA')
    background = Image.new('RGBA', rgba.size, (0, 0, 0, 255))
    image = Image.alpha_composite(background, rgba).convert('RGB')
    with open(sys.argv[2], 'wb') as output:
        output.write(struct.pack('<II', *image.size))
        output.write(image.tobytes())

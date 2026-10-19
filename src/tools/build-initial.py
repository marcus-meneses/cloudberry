#!/usr/bin/env python3
"""Build the trusted initial-app request; no guest-time image preparation."""
import struct,sys
name=sys.argv[1].encode();args=name+b'\0';cwd=b'/'
open(sys.argv[2],'wb').write(struct.pack('<IHHQQQQ',40,1,0,len(name),1,len(args),len(cwd))+name+args+cwd)

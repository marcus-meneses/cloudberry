#!/usr/bin/env python3
"""Generate named, versioned export records. Offsets resolve at static link."""
import json
import re
import sys
from pathlib import Path
spec = json.loads(Path(sys.argv[1]).read_text())
name = spec['name']
assert re.fullmatch(r'[a-z][a-z0-9_-]{0,30}', name)
lines = ['.section .module_name,"a",@progbits', f'.asciz "{name}"', f'.zero {31-len(name)}',
         '.section .module_exports,"a",@progbits']
seen = set()
for entry in spec['exports']:
    signature, symbol = entry['name'], entry['symbol']
    assert re.fullmatch(r'[a-z][a-z0-9_.]{0,30}', signature) and signature not in seen
    assert re.fullmatch(r'[a-zA-Z_][a-zA-Z_0-9]*', symbol)
    seen.add(signature)
    major, minor = entry.get('version', [1, 0])
    assert 0 < major <= 65535 and 0 <= minor <= 65535
    lines += [f'.asciz "{signature}"', f'.zero {31-len(signature)}', f'.quad {symbol}',
              f'.short {major}, {minor}', '.long 0']
lines += ['.section .note.GNU-stack,"",@progbits']
Path(sys.argv[2]).write_text('\n'.join(lines)+'\n')

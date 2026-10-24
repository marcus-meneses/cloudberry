#!/usr/bin/env python3
"""Record the isolated client's rendezvous VA for validated process.copy."""
import pathlib,subprocess,sys
nm,program,output=sys.argv[1:]
entries=[line.split() for line in subprocess.check_output([nm,'--defined-only',program],text=True).splitlines()]
matches=[int(e[0],16) for e in entries if len(e)==3 and e[2]=='gpu_pair_state']
if len(matches)!=1 or matches[0]<4096 or matches[0]&7:raise SystemExit('invalid client rendezvous symbol')
pathlib.Path(output).write_text('#define GPU_PAIR_STATE_ADDRESS UINT64_C(0x%x)\n'%matches[0])

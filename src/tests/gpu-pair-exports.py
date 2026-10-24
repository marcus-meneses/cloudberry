#!/usr/bin/env python3
"""Generate exports for the disposable paired-provider observer only."""
import json,pathlib,sys
source,output=sys.argv[1:];data=json.loads(pathlib.Path(source).read_text())
for e in data['exports']:
    if e['name']=='gpu.opengl.probe':e['symbol']='gpu_pair_probe'
    if e['name']=='gpu.opengl.release':e['symbol']='gpu_pair_release'
    if e['name']=='gpu.exit':e['symbol']='gpu_pair_exit'
    if e['name']=='gpu.invoke':e['symbol']='gpu_pair_io'
    if e['name']=='gpu_virtio.discover':e['symbol']='gpu_pair_discover'
data['exports'] += [dict(name='gpu.pair.observe',symbol='gpu_pair_observe')]
pathlib.Path(output).write_text(json.dumps(data,indent=2)+'\n')

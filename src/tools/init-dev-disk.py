#!/usr/bin/env python3
"""Initialize an explicit persistent development disk once, at build time."""
import argparse, fcntl, os, pathlib, subprocess, tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=pathlib.Path)
p.add_argument('destination',type=pathlib.Path)
a=p.parse_args();source=a.source.resolve();destination=a.destination.absolute()
if not source.is_file():p.error('build the candidate image first')
if source==destination.resolve():p.error('destination must differ from candidate')
destination.parent.mkdir(parents=True,exist_ok=True)
# Serialize initializers. O_EXCL/link below also protects against outside writers.
with open(str(destination)+'.init.lock','a') as lock:
    fcntl.flock(lock,fcntl.LOCK_EX)
    if destination.exists() or destination.is_symlink():p.error('destination already exists; retained without modification')
    fd,name=tempfile.mkstemp(prefix=destination.name+'.init-',dir=destination.parent);os.close(fd)
    temporary=pathlib.Path(name)
    try:
        subprocess.run(['cp','--reflink=auto','--sparse=always','--',str(source),str(temporary)],check=True)
        with temporary.open('rb') as copied:os.fsync(copied.fileno())
        os.link(temporary,destination) # atomic publication, never replaces an existing disk
        directory=os.open(destination.parent,os.O_RDONLY|os.O_DIRECTORY)
        try:os.fsync(directory)
        finally:os.close(directory)
    finally:temporary.unlink(missing_ok=True)
print(f'Initialized {destination}. Normal builds and run-dev preserve this disk.')

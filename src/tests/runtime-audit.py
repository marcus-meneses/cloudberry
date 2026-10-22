#!/usr/bin/env python3
"""Inventory a pinned engine's dependencies. Does not build a guest interpreter."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
VERSION = '2026-06-04'
URL = f'https://bellard.org/quickjs/quickjs-{VERSION}.tar.xz'
SHA256 = 'b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a'
UNITS = ('quickjs', 'dtoa', 'libregexp', 'libunicode', 'cutils')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--download', action='store_true', help='explicitly fetch the pinned official archive')
    parser.add_argument('--cc', default='gcc', help='host compiler for dependency inventory, not target validation')
    args = parser.parse_args()
    if not args.archive and not args.download:parser.error('supply --archive or --download')
    work = ROOT/'bin/tests/runtime-audit'
    work.mkdir(parents=True, exist_ok=True)
    archive = args.archive or work/f'quickjs-{VERSION}.tar.xz'
    if args.download and not args.archive:
        with urllib.request.urlopen(URL, timeout=30) as response:
            data = response.read(16*1024*1024+1)
        if len(data)>16*1024*1024:raise RuntimeError('archive exceeds audit download bound')
        if hashlib.sha256(data).hexdigest()!=SHA256:raise RuntimeError('archive hash mismatch')
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('archive hash mismatch')
    source = work/'source'
    source.mkdir(exist_ok=True)
    # Copy only regular source/header files from the expected release directory;
    # no tar paths, symlinks, build hooks or upstream Makefile are executed.
    with tarfile.open(archive) as tar:
        for entry in tar:
            name=Path(entry.name)
            if entry.isfile() and len(name.parts)==2 and name.parts[0]==f'quickjs-{VERSION}' and name.suffix in ('.c', '.h'):
                (source/name.name).write_bytes(tar.extractfile(entry).read())
    original = (source/'quickjs.c').read_text()
    guard = '#if !defined(__EMSCRIPTEN__)\n#define CONFIG_ATOMICS\n#endif'
    if original.count(guard)!=1:raise RuntimeError('upstream Atomics configuration changed')
    # Do not impersonate Emscripten: that would also disable stack checking.
    (source/'quickjs-audit.c').write_text(original.replace(guard, '/* Audit: OS-dependent Atomics disabled. */'))
    objects=[]
    commands=[]
    for unit in UNITS:
        path=source/('quickjs-audit.c' if unit=='quickjs' else unit+'.c')
        target=work/(unit+'.o')
        command=[args.cc, '-std=gnu11', '-O2', '-fno-builtin', '-fno-stack-protector', '-fno-pie', '-fno-pic',
                 '-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=0',
                 '-ffunction-sections', '-fdata-sections', f'-DCONFIG_VERSION="{VERSION}"', '-c', str(path), '-o', str(target)]
        commands.append(command)
        subprocess.run(command, check=True)
        objects.append(target)
    # A relocatable link removes references resolved between engine units, but
    # keeps the whole core (including default allocator/diagnostic functions).
    linked=work/'engine-core.o'
    subprocess.run([args.cc, '-nostdlib', '-r', *map(str,objects), '-o', str(linked)], check=True)
    symbols=sorted({line.split()[-1] for line in subprocess.check_output(['nm','-u',str(linked)], text=True).splitlines() if line.split()})
    target=ROOT/'.toolchain/cross/bin/x86_64-elf-gcc'
    probe=work/'target-header-probe.c'
    probe.write_text('#include <stdlib.h>\n#include <math.h>\nint audit_header_probe;\n')
    sdk_headers=ROOT/'bin/sdk/sysroot/x86_64-elf/include'
    includes=['-isystem',str(sdk_headers)] if sdk_headers.exists() else []
    result=subprocess.run([str(target), '-ffreestanding', *includes, '-c', str(probe), '-o', str(work/'target-header-probe.o')], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (work/'target-header-probe.log').write_text(result.stdout)
    report=dict(version=1, engine='QuickJS', release=VERSION, url=URL, archive_sha256=SHA256,
                profile=dict(units=UNITS, os_atomics=False, stack_checks=True, quickjs_libc=False, jit=False),
                inventory_compiler=subprocess.check_output([args.cc,'--version'],text=True).splitlines()[0],
                commands=commands, undefined_symbols=symbols, target_headers_available=result.returncode==0, target_header_source='optional-sdk' if includes else 'compiler-default',
                guest_port_validated=False, shell_deferred=True,
                caveat='Host-header whole-core dependency inventory; not a target link, libc conformance check or guest engine execution.')
    path=work/'report.json'
    path.write_text(json.dumps(report,indent=2)+'\n')
    print(f'AUDIT: {len(symbols)} whole-core external symbols; target headers available: {result.returncode==0}')
    print('Report: '+str(path.relative_to(ROOT)))

if __name__=='__main__':main()

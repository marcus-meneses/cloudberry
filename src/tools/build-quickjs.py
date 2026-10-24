#!/usr/bin/env python3
"""Verified, static QuickJS core for the Cloudberry SDK (no upstream OS shim)."""
import fcntl, hashlib, json, os, pathlib, shutil, subprocess, tarfile, urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[2]
VERSION='2026-06-04'
SHA256='b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a'
UNITS=('quickjs','dtoa','libregexp','libunicode','cutils')
def main():
    work=ROOT/'bin/sdk/quickjs';work.mkdir(parents=True,exist_ok=True)
    with (work/'build.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        archive=work/f'quickjs-{VERSION}.tar.xz'
        if not archive.exists():
            cached=ROOT/f'bin/tests/runtime-audit/quickjs-{VERSION}.tar.xz'
            if cached.exists():shutil.copy2(cached,archive)
            else:
                with urllib.request.urlopen(f'https://bellard.org/quickjs/quickjs-{VERSION}.tar.xz',timeout=60) as response:data=response.read(16*1024*1024+1)
                if len(data)>16*1024*1024 or hashlib.sha256(data).hexdigest()!=SHA256:raise RuntimeError('QuickJS download verification failed')
                archive.write_bytes(data)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('QuickJS archive hash mismatch')
        source=work/'source';source.mkdir(exist_ok=True)
        with tarfile.open(archive) as tar:
            for entry in tar:
                path=pathlib.Path(entry.name)
                if entry.isfile() and len(path.parts)==2 and path.parts[0]==f'quickjs-{VERSION}' and (path.suffix in ('.c','.h') or path.name=='LICENSE'):
                    (source/path.name).write_bytes(tar.extractfile(entry).read())
        original=(source/'quickjs.c').read_text();guard='#if !defined(__EMSCRIPTEN__)\n#define CONFIG_ATOMICS\n#endif'
        if original.count(guard)!=1:raise RuntimeError('QuickJS Atomics guard changed')
        timezone='        localtime_r(&ti, &tm);\n        res = -tm.tm_gmtoff / 60;'
        if original.count(timezone)!=1:raise RuntimeError('QuickJS timezone adapter changed')
        patched=original.replace(guard,'/* Cloudberry single-user-thread profile: no OS Atomics. Stack checks retained. */').replace(timezone,'        (void)ti; (void)tm;\n        res = 0; /* Cloudberry initial UTC-only profile. */')
        # Upstream ignores wall-clock failure. Never seed from uninitialized stack
        # data or fabricate an epoch when UTC is unavailable/unauthorized.
        wall='    struct timeval tv;\n    gettimeofday(&tv, NULL);'
        if patched.count(wall)!=2:raise RuntimeError('QuickJS wall-clock adapter changed')
        patched=patched.replace(wall,'    struct timeval tv = {0};\n    gettimeofday(&tv, NULL);',1)
        patched=patched.replace('static int64_t date_now(void)', 'static double date_now(void)')
        patched=patched.replace(wall,'    struct timeval tv;\n    if (gettimeofday(&tv, NULL)) return NAN;')
        patched=patched.replace('JS_NewInt64(ctx, date_now())','JS_NewFloat64(ctx, date_now())')
        (source/'quickjs.c').write_text(patched)
        prefix=ROOT/'.toolchain/cross/bin/x86_64-elf-';cc=str(prefix)+'gcc'
        flags=['-std=gnu11','-include','malloc.h','-O2','-g','-fno-builtin','-fno-stack-protector','-fno-pie','-mno-red-zone','-ffunction-sections','-fdata-sections',f'-DCONFIG_VERSION="{VERSION}"','-isystem',str(ROOT/'bin/sdk/sysroot/x86_64-elf/include')]
        identity=dict(release=VERSION,sha256=SHA256,flags=flags,compiler=subprocess.check_output([cc,'--version'],text=True).splitlines()[0],os_atomics=False,stack_checks=True,timezone="UTC",port_revision=2)
        stamp=work/'build.json';library=work/'libquickjs.a'
        if not stamp.exists() or json.loads(stamp.read_text())!=identity or not library.exists():
            objects=[]
            with (work/'build.log').open('w') as log:
                for unit in UNITS:
                    obj=work/(unit+'.o');subprocess.run([cc,*flags,'-c',str(source/(unit+'.c')),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True);objects.append(str(obj))
            temporary=work/'libquickjs.tmp.a';temporary.unlink(missing_ok=True)
            subprocess.run([str(prefix)+'ar','rcs',str(temporary),*objects],check=True);temporary.replace(library);stamp.write_text(json.dumps(identity,indent=2)+'\n')
        shutil.copy2(source/'LICENSE',work/'LICENSE')
        print('QuickJS: bin/sdk/quickjs (verified C core, stack checks, no OS shim/Atomics/Workers)')
if __name__=='__main__':main()

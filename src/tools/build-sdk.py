#!/usr/bin/env python3
"""Build an isolated static x86-64 SDK; never install into the host/toolchain."""
import argparse,fcntl,hashlib,json,os,pathlib,shutil,subprocess,tarfile,urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[2]
VERSION='4.5.0.20241231'
SHA256='33f12605e0054965996c25c1382b3e463b0af91799001f5bb8c0630f2ec8c852'
URL=f'https://sourceware.org/pub/newlib/newlib-{VERSION}.tar.gz'
FLAGS='-O2 -g -std=gnu11 -mno-red-zone -fno-pie -fno-stack-protector -ffunction-sections -fdata-sections'
def run(command,log,**kwargs):
    with log.open('w') as out:subprocess.run(command,stdout=out,stderr=subprocess.STDOUT,check=True,**kwargs)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--jobs',type=int,default=4);a=p.parse_args()
    if a.jobs<1:p.error('jobs must be positive')
    work=ROOT/'bin/sdk';work.mkdir(parents=True,exist_ok=True)
    with (work/'build.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        cache=work/'cache';cache.mkdir(exist_ok=True);archive=cache/f'newlib-{VERSION}.tar.gz'
        if not archive.exists():
            print('Fetching pinned Newlib source: '+URL,flush=True)
            with urllib.request.urlopen(URL,timeout=60) as source:data=source.read(32*1024*1024+1)
            if len(data)>32*1024*1024 or hashlib.sha256(data).hexdigest()!=SHA256:raise RuntimeError('Newlib archive verification failed')
            archive.write_bytes(data)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('Newlib archive hash mismatch')
        source=cache/f'newlib-{VERSION}'
        if not source.exists():
            with tarfile.open(archive) as tar:tar.extractall(cache,filter='data')
        target=ROOT/'.toolchain/cross/bin';prefix=target/'x86_64-elf-';cc=str(prefix)+'gcc'
        compiler=subprocess.check_output([cc,'--version'],text=True).splitlines()[0]
        identity=dict(newlib=VERSION,sha256=SHA256,flags=FLAGS,compiler=compiler,single_thread=True,supplied_syscalls=False)
        stamp=work/'newlib.json';sysroot=work/'sysroot'
        if not stamp.exists() or json.loads(stamp.read_text())!=identity or not (sysroot/'x86_64-elf/lib/libm.a').exists():
            build=work/'newlib-build'
            if stamp.exists():shutil.rmtree(build,ignore_errors=True)
            build.mkdir(exist_ok=True);tmp=build/'tmp';tmp.mkdir(exist_ok=True)
            env=dict(os.environ,PATH=str(target)+os.pathsep+os.environ['PATH'],TMPDIR=str(tmp),CFLAGS_FOR_TARGET=FLAGS)
            command=[str(source/'configure'),'--build=x86_64-pc-linux-gnu','--target=x86_64-elf',f'--prefix={sysroot}',
                     '--disable-multilib','--disable-newlib-supplied-syscalls','--disable-libgloss','--disable-nls','--disable-shared',
                     '--disable-newlib-multithread','--enable-newlib-io-long-long','--enable-newlib-io-c99-formats','--enable-newlib-io-long-double']
            run(command,build/'configure.log',cwd=build,env=env)
            run(['make',f'-j{a.jobs}','all-target-newlib'],build/'build.log',cwd=build,env=env)
            run(['make','install-target-newlib'],build/'install.log',cwd=build,env=env)
            stamp.write_text(json.dumps(identity,indent=2)+'\n')
        shutil.copy2(source/'COPYING.NEWLIB',work/'COPYING.NEWLIB')
        flags=FLAGS.split()+['-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin','-isystem',str(sysroot/'x86_64-elf/include')]
        (work/'tmp').mkdir(exist_ok=True)
        objects=[]
        for name in ('heap','runtime','system','libc','crt0'):
            obj=work/(name+'.o');temporary=work/(name+'.tmp.o');run([cc,*flags,'-c',str(ROOT/f'src/sdk/{name}.c'),'-o',str(temporary)],work/(name+'.log'));temporary.replace(obj)
            if name!='crt0':objects.append(str(obj))
        library=work/'libcloudberry.a';temporary=work/'libcloudberry.tmp.a';temporary.unlink(missing_ok=True)
        subprocess.run([str(prefix)+'ar','rcs',str(temporary),*objects],check=True);temporary.replace(library)
        print('SDK: '+str(work.relative_to(ROOT))+' (static libc/libm, handle-backed runtime, SSE2 user code)')
if __name__=='__main__':main()

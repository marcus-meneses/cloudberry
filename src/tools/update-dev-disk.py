#!/usr/bin/env python3
"""Offline development-image update. Never replaces either input image."""
import argparse,contextlib,fcntl,hashlib,importlib.util,json,os,pathlib,shutil,stat,struct,subprocess,tempfile
HERE=pathlib.Path(__file__).resolve().parent

def load(name):
    spec=importlib.util.spec_from_file_location(name.replace('-','_'),HERE/(name+'.py'));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
image=load('build-boot-image');version=load('image-version')

def run(command):
    result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    if result.returncode:raise ValueError('command failed: '+command[0]+': '+result.stderr[-1000:])
    return result.stdout

def quote(path):
    text=str(path)
    if any(ord(c)<32 or ord(c)==127 for c in text):raise ValueError('unsupported control character in path')
    return '"'+text.replace('\\','\\\\').replace('"','\\"')+'"'

def debug(fs,command):
    result=run(['debugfs','-R',command,str(fs)])
    return result

def extract(source,part,destination,recover=False):
    with source.open('rb') as src,destination.open('wb') as dst:
        src.seek(part['start']*512);left=part['sectors']*512
        while left:
            data=src.read(min(left,1024*1024))
            if not data:raise ValueError('truncated partition')
            dst.write(data);left-=len(data)
    if recover:
        with destination.open('rb') as file:file.seek(1024+96);needs_replay=bool(struct.unpack('<I',file.read(4))[0]&4)
        if needs_replay:
            result=subprocess.run(['e2fsck','-p','-E','journal_only',str(destination)],capture_output=True)
            if result.returncode not in (0,1):raise ValueError('journal replay failed on disposable copy')
    run(['e2fsck','-f','-n',str(destination)]) # refuse dirty/error inputs; do not repair

def inventory(fs):
    entries={};inodes=set()
    def visit(directory):
        rows=debug(fs,'ls -p '+quote(directory))
        for row in rows.splitlines():
            if not row:continue
            fields=row.split('/')
            if len(fields)!=8 or fields[0] or fields[-1]:raise ValueError('unparseable filesystem directory')
            inode,mode,uid,gid=int(fields[1]),int(fields[2],8),int(fields[3]),int(fields[4]);name=fields[5]
            if not inode:continue
            if name=='..':continue
            path=directory if name=='.' else directory.rstrip('/')+'/'+name
            quote(path)
            if name=='.':
                if path=='/':entries[path]=(uid,gid,mode)
                continue
            if inode in inodes:raise ValueError('hard-linked inode unsupported by offline updater')
            inodes.add(inode)
            if stat.S_IFMT(mode) not in (stat.S_IFREG,stat.S_IFDIR):raise ValueError('unsupported inode type: '+path)
            entries[path]=(uid,gid,mode)
            if stat.S_ISDIR(mode):visit(path)
    visit('/');return entries

def export(fs,root):
    root.mkdir();debug(fs,'rdump / '+quote(root))

def merge(old,new,metadata):
    # System code/config come entirely from candidate. Persistent namespaces and
    # identity/root state come from old media. Temporary/recovery trees are fresh.
    installed=load('installation-state').verify(old,new)
    preserved=['/home','/volumes','/system/root','/system/config/identity','/system/config/installations']
    for record in installed:
        if record['owner']==2**64-1:preserved.extend([record['installation'],record['installation']+'.manifest.json',record['installation']+'.resources'])
    for p in old.iterdir():
        if p.name not in ('home','volumes','boot','system','tmp','lost+found'):preserved.append('/'+p.name)
    authoritative_index=(new/'system/apps/_index').read_bytes()
    copied={}
    for name in preserved:
        source=old/name.lstrip('/');target=new/name.lstrip('/')
        if not source.exists():continue
        if source.is_dir():shutil.copytree(source,target,dirs_exist_ok=True)
        else:target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        for path,attributes in metadata.items():
            if path==name or path.startswith(name+'/'):copied[path]=attributes
    # Catalog caches confer no installation authority. Refresh them explicitly
    # offline and add empty data containers for newly shipped system apps.
    cache=json.loads(authoritative_index)
    homes=[new/'system/root']+[p for p in (new/'home').iterdir() if p.is_dir() and (p/'_apps').is_dir()]
    for home in homes:
        apps=home/'_apps';apps.mkdir(exist_ok=True);relative='/'+home.relative_to(new).as_posix();uid,gid,_=copied.get(relative,(0,0,0o40700))
        additional=[dict(app_id=r['app_id'],name=r['name'],installation=r['installation']) for r in installed if r['owner'] in (uid,2**64-1)]
        (apps/'_index').write_text(json.dumps(dict(version=1,entries=cache['entries']+additional),indent=2)+'\n');copied[relative+'/_apps/_index']=(uid,gid,0o100600)
        for record in cache['entries']:
            name=record['name'];path=apps/(name+'.data')
            if not path.exists():path.mkdir(mode=0o700);copied[relative+'/_apps/'+name+'.data']=(uid,gid,0o40700)
    global_apps=[dict(app_id=r['app_id'],name=r['name'],installation=r['installation']) for r in installed if r['owner']==2**64-1]
    (new/'system/apps/_index').write_text(json.dumps(dict(version=1,entries=cache['entries']+global_apps),indent=2)+'\n')
    return copied

def update(candidate,old,output,allow_unversioned=False):
    for path in (candidate,old):
        if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):raise ValueError('inputs must be regular non-symlink images')
    if candidate!=old and os.path.samefile(candidate,old):raise ValueError('aliased candidate/previous paths are unsupported')
    sidecar=pathlib.Path(str(output)+'.update.json')
    if output.exists() or output.is_symlink() or sidecar.exists() or sidecar.is_symlink():raise ValueError('output/report exists; retained without modification')
    output.parent.mkdir(parents=True,exist_ok=True)
    with contextlib.ExitStack() as stack:
        # QEMU's byte-range/OFD image locks conflict with these POSIX locks.
        for path in sorted({candidate,old}):
            file=stack.enter_context(path.open('r+b' if path==old else 'rb'))
            fcntl.lockf(file,fcntl.LOCK_EX|fcntl.LOCK_NB if path==old else fcntl.LOCK_SH|fcntl.LOCK_NB)
        signatures={p:(p.stat().st_size,p.stat().st_mtime_ns) for p in (candidate,old)}
        with tempfile.TemporaryDirectory(prefix=output.name+'.update-',dir=output.parent) as temporary:
            work=pathlib.Path(temporary);parts={}
            for key,path in [('old',old),('candidate',candidate)]:
                choices=[p for p in image.partitions(path) if p['type']==str(image.LINUX_DATA)]
                if len(choices)!=1:raise ValueError('expected exactly one system partition')
                parts[key]=choices[0];extract(path,choices[0],work/(key+'.ext4'),recover=key=='old')
            if parts['old']['guid']!=parts['candidate']['guid']:raise ValueError('installation partition identity mismatch')
            attributes=inventory(work/'old.ext4');inventory(work/'candidate.ext4')
            export(work/'old.ext4',work/'old');export(work/'candidate.ext4',work/'candidate')
            boot=work/'boot';boot.mkdir();run(['xorriso','-osirrox','on','-indev',str(candidate),'-extract','/',str(boot)])
            identity=version.verify(boot,work/'candidate')
            old_id=work/'old/system/state/build.id'
            if old_id.exists():
                old_boot=work/'old-boot';old_boot.mkdir();run(['xorriso','-osirrox','on','-indev',str(old),'-extract','/',str(old_boot)]);version.verify(old_boot,work/'old')
            elif not allow_unversioned:raise ValueError('unversioned input; explicit --allow-unversioned-input required for one-time data import')
            # Never import an unresolved account write-ahead transaction.
            journal=work/'old/system/config/identity/transaction.bin'
            if not journal.is_file() or journal.stat().st_size:raise ValueError('identity database requires guest recovery before offline update')
            load('identity-db').validate(work/'old/system/config/identity')
            retained=merge(work/'old',work/'candidate',attributes)
            merged=work/'merged.ext4';size=parts['candidate']['sectors']*512
            with merged.open('wb') as stream:stream.truncate(size)
            with (work/'old.ext4').open('rb') as stream:stream.seek(1024+104);fs_uuid=image.uuid.UUID(bytes=stream.read(16))
            run(['mke2fs','-q','-t','ext4','-F','-b','4096','-I','256','-m','0','-O',image.EXT4_FEATURES,'-U',str(fs_uuid),'-L','CLOUDBERRY_SYS','-E','lazy_itable_init=0,lazy_journal_init=0','-d',str(work/'candidate'),str(merged)])
            commands=[]
            for p in [work/'candidate']+sorted((work/'candidate').rglob('*')):
                name='/'+p.relative_to(work/'candidate').as_posix() if p!=work/'candidate' else '/'
                uid,gid,mode=retained.get(name,(0,0,p.stat().st_mode))
                for field,value in [('uid',uid),('gid',gid),('mode',oct(mode)[2:])]:commands.append('set_inode_field '+quote(name)+' '+field+' '+('0'+value if field=='mode' else str(value)))
            batch=work/'metadata.debugfs';batch.write_text('\n'.join(commands)+'\n');run(['debugfs','-w','-f',str(batch),str(merged)]);run(['e2fsck','-f','-n',str(merged)])
            # Verify ownership restoration independently of the host's UID.
            restored=inventory(merged)
            for name,value in retained.items():
                if restored.get(name)!=value:raise ValueError('metadata preservation failed: '+name)
            result=work/'updated.img';run(['cp','--reflink=auto','--sparse=always','--',str(candidate),str(result)])
            with result.open('r+b') as target,merged.open('rb') as source:
                target.seek(parts['candidate']['start']*512);shutil.copyfileobj(source,target,1024*1024);target.flush();os.fsync(target.fileno())
            # Retain disk identity too; GPT CRCs are regenerated by sgdisk.
            with old.open('rb') as source:source.seek(512+56);disk_guid=image.uuid.UUID(bytes_le=source.read(16))
            run(['sgdisk','-U',str(disk_guid),str(result)]);image.verify(result,image.uuid.UUID(parts['candidate']['guid']),size)
            if any((p.stat().st_size,p.stat().st_mtime_ns)!=signatures[p] for p in signatures):raise ValueError('input changed during update')
            with result.open('rb') as source:os.fsync(source.fileno())
            report=dict(version=1,build_id=identity,previous_build=old_id.read_text().strip() if old_id.exists() else None,preserved_paths=len(retained),candidate=str(candidate),previous=str(old),output=str(output))
            report_file=work/'update.json';report_file.write_text(json.dumps(report,indent=2)+'\n')
            with report_file.open('rb') as source:os.fsync(source.fileno())
            os.link(report_file,sidecar)
            try:os.link(result,output) # no overwrite, atomic image publication
            except Exception:sidecar.unlink();raise
            fd=os.open(output.parent,os.O_DIRECTORY|os.O_RDONLY)
            try:os.fsync(fd)
            finally:os.close(fd)
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('candidate',type=pathlib.Path);p.add_argument('previous',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);p.add_argument('--allow-unversioned-input',action='store_true');a=p.parse_args()
    try:report=update(a.candidate.absolute(),a.previous.absolute(),a.output.absolute(),a.allow_unversioned_input)
    except (ValueError,OSError,subprocess.SubprocessError) as error:p.exit(1,'Update refused: '+str(error)+'\n')
    print('Updated image: '+report['output']+'; build '+report['build_id']+'; original retained')

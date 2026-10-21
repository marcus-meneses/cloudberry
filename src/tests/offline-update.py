#!/usr/bin/env python3
"""Disposable update acceptance: identity/data/ownership, coherence and refusal."""
import argparse,hashlib,importlib.util,json,os,pathlib,shutil,struct,subprocess,sys,uuid
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('updater',root/'src/tools/update-dev-disk.py');u=importlib.util.module_from_spec(spec);spec.loader.exec_module(u)
work=root/'bin/tests/offline-update'/uuid.uuid4().hex;work.mkdir(parents=True);passed=False
try:
    candidate=work/'candidate.img';stage=work/'stage';shutil.copytree(a.build/'src-iso-root',stage)
    with (stage/'cloudberry.cnf').open('a') as out:out.write('# offline update acceptance candidate\n')
    u.image.build(stage,candidate,drivers_source=a.build/'src-handler-bundles')
    previous=work/'previous.img';u.run(['cp','--reflink=auto','--sparse=always',str(a.build/'cloudberry.iso'),str(previous)])
    part=next(x for x in u.image.partitions(previous) if x['index']==4);fs=work/'previous.ext4';u.extract(previous,part,fs)
    old_root=work/'old-root';u.export(fs,old_root);identity=u.load('identity-db');identity_dir=old_root/'system/config/identity'
    old_credentials=(identity_dir/'credentials.bin').read_bytes();groups=(identity_dir/'groups.bin').read_bytes()[64:]
    files=[identity.encode(0,[identity.user(0,'root',0),identity.user(123,'tester')],generation=2,next_id=124),identity.encode(1,[identity.group(0,'root',(0,)),identity.group(1,'admin'),identity.group(2,'users')],generation=2,next_id=3),identity.encode(2,[old_credentials[64:],identity.credential(123,b'update-test')],generation=2)]
    commands=['mkdir /home/tester','mkdir /home/tester/documents'];payload=work/'payload.bin';payload.write_bytes(b'persistent update data\x00\xff\n');commands.append('write '+u.quote(payload)+' /home/tester/documents/data.bin')
    for name in ('/home/tester','/home/tester/documents','/home/tester/documents/data.bin'):
        for field,value in [('uid','123'),('gid','2'),('mode','0100640' if name.endswith('.bin') else '040700')]:commands.append('set_inode_field '+u.quote(name)+' '+field+' '+value)
    for name,data in zip(('users.bin','groups.bin','credentials.bin'),files):
        source=work/name;source.write_bytes(data);target='/system/config/identity/'+name;commands+=['rm '+target,'write '+u.quote(source)+' '+target,'set_inode_field '+target+' mode 0100600']
    batch=work/'prepare.debugfs';batch.write_text('\n'.join(commands)+'\n');u.run(['debugfs','-w','-f',str(batch),str(fs)])
    with previous.open('r+b') as disk,fs.open('rb') as source:disk.seek(part['start']*512);shutil.copyfileobj(source,disk)
    original=hashlib.sha256(previous.read_bytes()).hexdigest();output=work/'updated.img';report=u.update(candidate,previous,output)
    assert hashlib.sha256(previous.read_bytes()).hexdigest()==original
    updated_part=next(x for x in u.image.partitions(output) if x['index']==4);updated_fs=work/'updated.ext4';u.extract(output,updated_part,updated_fs);updated_root=work/'updated-root';u.export(updated_fs,updated_root)
    assert (updated_root/'home/tester/documents/data.bin').read_bytes()==payload.read_bytes()
    assert u.inventory(updated_fs)['/home/tester/documents/data.bin']==(123,2,0o100640)
    for name,data in zip(('users.bin','groups.bin','credentials.bin'),files):assert (updated_root/'system/config/identity'/name).read_bytes()==data
    identity.validate(updated_root/'system/config/identity')
    assert (updated_root/'system/state/build.id').read_text().strip()==report['build_id'] and report['build_id']!=report['previous_build']
    for path in (output,previous):
        before=hashlib.sha256(path.read_bytes()).hexdigest()
        try:u.update(candidate,previous,path);raise AssertionError('overwrite accepted')
        except ValueError:pass
        assert before==hashlib.sha256(path.read_bytes()).hexdigest()
    # A live writer's POSIX lock must prevent importing a moving source image.
    locker=subprocess.Popen([sys.executable,'-c','import fcntl,sys;f=open(sys.argv[1],"r+b");fcntl.lockf(f,fcntl.LOCK_EX);print("locked",flush=True);sys.stdin.read()',str(previous)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    try:
        assert locker.stdout.readline().strip()=='locked'
        try:u.update(candidate,previous,work/'locked.img');raise AssertionError('active source accepted')
        except BlockingIOError:pass
    finally:locker.stdin.close();locker.wait(timeout=10)
    # A modified immutable managed file without a new manifest is rejected.
    broken=work/'broken.img';u.run(['cp','--reflink=auto','--sparse=always',str(candidate),str(broken)]);broken_part=next(x for x in u.image.partitions(broken) if x['index']==4);broken_fs=work/'broken.ext4';u.extract(broken,broken_part,broken_fs)
    replacement=work/'bad-config';replacement.write_text('layout=broken\n');batch.write_text('rm /system/config/keyboard/config.cnf\nwrite '+u.quote(replacement)+' /system/config/keyboard/config.cnf\n');u.run(['debugfs','-w','-f',str(batch),str(broken_fs)])
    with broken.open('r+b') as disk,broken_fs.open('rb') as source:disk.seek(broken_part['start']*512);shutil.copyfileobj(source,disk)
    try:u.update(broken,previous,work/'mixed.img');raise AssertionError('mixed build accepted')
    except ValueError as e:assert 'managed file' in str(e)
    assert not (work/'mixed.img').exists() and not (work/'locked.img').exists()
    passed=True
finally:
    (work/'result.json').write_text(json.dumps(dict(passed=passed),indent=2)+'\n')
    if passed:
        for name in ('old-root','updated-root','stage'):shutil.rmtree(work/name,ignore_errors=True)
        subprocess.run([sys.executable,str(root/'src/tools/clean-tests.py'),'--root',str(work)],check=True)
print('PASS offline update: new build, retained credentials/home bytes/UID/GID/mode, rollback source, overwrite/active/mixed-build refusals; '+str(work/'result.json'))

#!/usr/bin/env python3
"""Per-user V2 approval/upgrade, reboot and offline update on disposable media."""
import argparse,hashlib,importlib.util,json,pathlib,re,struct,subprocess,sys,tempfile,traceback,uuid
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=pathlib.Path);p.add_argument('--cpus',type=int,default=4);p.add_argument('--created-session',type=pathlib.Path,help='reuse passed isolated user-maint-create media');a=p.parse_args()
build=a.build.resolve();work=build/'tests/installer-user-lifecycle'/uuid.uuid4().hex;work.mkdir(parents=True)
spec=importlib.util.spec_from_file_location('installer_media',root/'src/tests/installer-media.py');media=importlib.util.module_from_spec(spec);spec.loader.exec_module(media)
result={'status':'running','cpus':a.cpus,'sessions':[]}
def run(command,name):
    with (work/(name+'.log')).open('w') as log:completed=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
    if completed.returncode:raise RuntimeError(name+' failed; see '+str(work/(name+'.log')))
    return (work/(name+'.log')).read_text()
def console(phase,extra):
    output=run([sys.executable,str(root/'src/tests/console-boot.py'),str(build),'--installer-phase','user-maint-'+phase,'--cpus',str(a.cpus),'--keep-artifacts',*extra],phase)
    match=re.search(r'PASSED: console, report (.*)/result.json',output)
    if not match:raise RuntimeError('missing console acceptance report')
    session=pathlib.Path(match[1]);result['sessions'].append(str(session));return session/'boot.img'
def verify(disk,label):
    with tempfile.TemporaryDirectory(dir=work) as directory:
        directory=pathlib.Path(directory);part=next(p for p in media.image.partitions(disk) if p['index']==4);fs=directory/'system.ext4'
        media.updater.extract(disk,part,fs,recover=True);attributes=media.updater.inventory(fs);tree=directory/'tree';media.updater.export(fs,tree)
        state=tree/'system/config/installations';raw=(state/'registry.bin').read_bytes()
        assert hashlib.sha512(raw[:-64]).digest()==raw[-64:]
        magic,length,generation,count=struct.unpack_from('<QQQQ',raw);assert magic==0x324745524243 and length==len(raw)-64 and count==1
        owner,revision=struct.unpack_from('<QQ',raw,32);assert (owner,revision)==(1,3)
        size,nb,pb,mb,identity,flags,cpu,stack=struct.unpack_from('<QQQQ16sIIQ',raw,48)
        assert length==48+size and raw[112:112+nb]==b'sample'
        base='/home/alice/_apps/_112233445566778899aabbccddeeff00.sample'
        assert raw[112+nb:112+nb+pb].decode()==base
        approved=json.loads(raw[112+nb+pb:112+nb+pb+mb]);ops={r['name']:set(r['operations']) for r in approved['resources']}
        assert ops=={'/@streams':{'read','write'},'/@filesystem':{'open'},'/@applications':{'data','resources'}}
        signed=(state/'packages/0000000000000001-112233445566778899aabbccddeeff00.cbp').read_bytes()
        bundle=media.bundle.verify_bundle(signed,(build/'installer-trusted-key.bin').read_bytes());assert bundle['revision']==3
        requested=json.loads(bundle['manifest']);assert requested['resources'][1]['operations']==['open','stat','mkdir']
        assert (tree/base.lstrip('/')).read_bytes()==bundle['code']
        assert (tree/(base+'.manifest.json').lstrip('/')).read_bytes()==bundle['manifest']
        assert (tree/(base+'.resources/sub/hello.txt').lstrip('/')).read_bytes()==b'upgraded payload\n'
        assert attributes[base]==(0,0,0o100555) and attributes[base+'.data']==(1,2,0o40700)
        assert attributes[base+'.data/state']==(1,2,0o100600)
        assert (tree/(base+'.data/state').lstrip('/')).read_bytes()==b'sample-state\n'
        assert not (tree/'system/apps'/base.rsplit('/',1)[-1]).exists()
        assert not (tree/'system/root/_apps'/(base.rsplit('/',1)[-1]+'.data')).exists()
        user_index=json.loads((tree/'home/alice/_apps/_index').read_text());root_index=json.loads((tree/'system/root/_apps/_index').read_text())
        assert any(e['name']=='sample' for e in user_index['entries']) and not any(e['name']=='sample' for e in root_index['entries'])
        assert not (state/'transaction.bin').read_bytes()
        result[label]={'generation':generation,'owner':owner,'revision':revision,'approved':approved,'private_bytes':13,'metadata':'verified'}
try:
    if a.created_session:
        session=a.created_session.resolve()
        if not session.is_relative_to(root/'bin/tests/console') or json.loads((session/'result.json').read_text()).get('status')!='passed':raise ValueError('created-session must be a passed isolated console run')
        created=session/'boot.img';result['sessions'].append(str(session))
    else:created=console('create',['--fixtures'])
    verify(created,'created')
    rebooted=console('reboot',['--disk-source',str(created)]);verify(rebooted,'rebooted')
    updated=work/'updated.img';run([sys.executable,str(root/'src/tools/update-dev-disk.py'),str(build/'cloudberry.iso'),str(rebooted),str(updated)],'offline-update');verify(updated,'offline_updated')
    final=console('verify',['--disk-source',str(updated)]);verify(final,'verified');result['status']='passed'
    for directory in [*[pathlib.Path(s) for s in result['sessions']],work]:run([sys.executable,str(root/'src/tools/clean-tests.py'),'--root',str(directory)],'cleanup-'+directory.name)
except Exception as error:result.update(status='failed',error=str(error) or type(error).__name__,traceback=traceback.format_exc())
finally:(work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['status'].upper()+': per-user installer lifecycle; report '+str(work/'result.json'))
if result['status']!='passed':raise SystemExit(result['error'])

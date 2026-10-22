#!/usr/bin/env python3
"""Abrupt installer exit at durable boundaries, real guest ext4, then reboot."""
import argparse,hashlib,importlib.util,json,pathlib,re,subprocess,sys,uuid,struct
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=pathlib.Path);p.add_argument('--cpus',type=int,default=4);p.add_argument('--case',choices=['1-2','1-6','2-1','2-3','3-2','3-6','4-2','4-3','5-2','5-3','6-2','6-3'],action='append');p.add_argument('--keep-artifacts',action='store_true');a=p.parse_args();build=a.build.resolve();work=root/'bin/tests/installer-crash'/uuid.uuid4().hex;work.mkdir(parents=True)
spec=importlib.util.spec_from_file_location('media',root/'src/tests/installer-media.py');media=importlib.util.module_from_spec(spec);spec.loader.exec_module(media)
report={'status':'running','cpus':a.cpus,'cases':[]}
def command(args,log):
 with (work/(log+'.log')).open('w') as out:r=subprocess.run(args,stdout=out,stderr=subprocess.STDOUT)
 if r.returncode:raise RuntimeError(log+' failed; see '+str(work/(log+'.log')))
 return (work/(log+'.log')).read_text()
def console(phase,extra,label):
 output=command([sys.executable,str(root/'src/tests/console-boot.py'),str(build),'--installer-phase',phase,'--cpus',str(a.cpus),'--keep-artifacts',*extra],label)
 match=re.search(r'PASSED: console, report (.*)/result.json',output)
 if not match:raise RuntimeError('missing accepted console result')
 return pathlib.Path(match[1])
def check(disk,expected,case):
 part=next(p for p in media.image.partitions(disk) if p['index']==4);fs=work/(case+'.ext4');media.updater.extract(disk,part,fs,recover=True)
 entries=media.updater.inventory(fs)
 def read(path):return subprocess.check_output(['debugfs','-R','cat '+media.updater.quote(path),str(fs)],stderr=subprocess.DEVNULL)
 state=read('/system/config/installations/registry.bin');assert hashlib.sha512(state[:-64]).digest()==state[-64:];assert not read('/system/config/installations/transaction.bin')
 base='/system/apps/_112233445566778899aabbccddeeff00.sample';data='/system/root/_apps/'+base.rsplit('/',1)[-1]+'.data'
 if case[0] in '456':
  check_maintenance(read,entries,state,base,data,expected);assert '/system/config/installations/test-cut.bin' in entries;fs.unlink();return
 installed=expected!='recover-empty';assert (base in entries)==installed;assert (data in entries)==installed
 if installed:
  package=read('/system/config/installations/packages/ffffffffffffffff-112233445566778899aabbccddeeff00.cbp');signed=media.bundle.verify_bundle(package,(build/'installer-trusted-key.bin').read_bytes());assert read(base)==signed['code'];assert read(base+'.manifest.json')==signed['manifest']
  for name,contents in signed['resources'].items():assert read(base+'.resources/'+name)==contents
  assert read(data+'/state')==(b'sample-state\n' if expected=='recover-installed' else b'sample-state\n'*2)
 assert '/system/config/installations/test-cut.bin' in entries
 fs.unlink()
def check_maintenance(read,entries,state,base,data,expected):
 assert data in entries
 assert read(data+'/state')==b'sample-state\n'*(2 if expected in ('recover-repaired','recover-unapproved') else 1)
 records=[];at=32
 while at<len(state)-64:
  owner,revision=struct.unpack_from('<QQ',state,at);size,nb,pb,mb,identity,flags,cpu,stack=struct.unpack_from('<QQQQ16sIIQ',state,at+16)
  name=state[at+80:at+80+nb].decode();manifest=state[at+80+nb+pb:at+16+size];records.append((name,revision,manifest));at+=16+size
 target=next((r for r in records if r[0]=='sample'),None)
 archive='/system/config/installations/packages/ffffffffffffffff-112233445566778899aabbccddeeff00.cbp'
 if expected=='recover-forgotten':assert not target and base not in entries and archive not in entries;return
 assert target
 if expected=='recover-quarantined':
  assert target[1] in (0,1) and (base not in entries or read(base)==b'broken');return
 signed=media.bundle.verify_bundle(read(archive),(build/'installer-trusted-key.bin').read_bytes());assert read(base)==signed['code'] and read(base+'.manifest.json')==signed['manifest']
 for name,contents in signed['resources'].items():assert read(base+'.resources/'+name)==contents
 if expected=='recover-repaired':assert target[1]==2 and signed['revision']==2
 approved=json.loads(target[2])
 if expected=='recover-approved':
  assert len(approved['resources'])==1 and approved['resources'][0]['name']=='/@streams' and approved['resources'][0]['operations']==['read']
 else:assert any(r['name']=='/@streams' and 'write' in r['operations'] for r in approved['resources'])
try:
 for case in a.case or ['1-2','1-6','2-1','2-3','3-2','3-6']:
  operation,point=map(int,case.split('-'));elf=build/'tests'/('installer-cut-'+case+'.elf')
  if not elf.is_file():raise ValueError('build isolated test ELF first: '+str(elf))
  phase={1:'cut-install',2:'cut-remove',3:'cut-upgrade',4:'cut-repair',5:'cut-forget',6:'cut-approve'}[operation]
  crashed=console(phase,['--fixtures','--installer-cut',str(elf)],case+'-cut')
  expected=('recover-empty' if point<3 else 'recover-installed') if operation==1 else ('recover-old' if point<3 else 'recover-empty') if operation==2 else ('recover-old' if point<3 else 'recover-new')
  if operation>=4:expected={4:('recover-quarantined','recover-repaired'),5:('recover-quarantined','recover-forgotten'),6:('recover-unapproved','recover-approved')}[operation][point>=3]
  recovered=console(expected,['--disk-source',str(crashed/'boot.img')],case+'-reboot');check(recovered/'boot.img',expected,case)
  report['cases'].append(dict(case=case,cut=str(crashed),reboot=str(recovered),outcome=expected));(work/'result.json').write_text(json.dumps(report,indent=2)+'\n')
  if not a.keep_artifacts:
   for d in (crashed,recovered):command([sys.executable,str(root/'src/tools/clean-tests.py'),'--root',str(d)],case+'-clean-'+d.name)
 report['status']='passed'
except Exception as error:report.update(status='failed',error=str(error))
finally:(work/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(report['status'].upper()+': installer guest crash recovery; report '+str(work/'result.json'))
if report['status']!='passed':raise SystemExit(report['error'])

"""Offline edits to disposable installer test images; no host mounts."""
import importlib.util,pathlib,subprocess,tempfile,shutil,struct,hashlib
HERE=pathlib.Path(__file__).resolve().parents[1]
def load(name):
 s=importlib.util.spec_from_file_location(name,HERE/'tools'/(name+'.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
image=load('build-boot-image');updater=load('update-dev-disk');bundle=load('build-app-bundle')
def edit(disk,files,directories=()):
 disk=pathlib.Path(disk).resolve();assert disk.is_relative_to(HERE.parent/'bin/tests')
 part=next(p for p in image.partitions(disk) if p['index']==4)
 with tempfile.TemporaryDirectory(dir=disk.parent) as temp:
  temp=pathlib.Path(temp);fs=temp/'system.ext4';updater.extract(disk,part,fs,recover=True);commands=[]
  for path in directories:commands+=['mkdir '+updater.quote(path),'set_inode_field '+updater.quote(path)+' mode 040700']
  for i,(path,data) in enumerate(files.items()):
   host=temp/str(i);host.write_bytes(data);commands+=['rm '+updater.quote(path),'write '+updater.quote(host)+' '+updater.quote(path),'set_inode_field '+updater.quote(path)+' uid 0','set_inode_field '+updater.quote(path)+' gid 0','set_inode_field '+updater.quote(path)+' mode '+('0100555' if path.startswith('/system/apps/') and '.' not in path.rsplit('.',1)[-1] and not path.endswith('.json') else '0100644')]
  batch=temp/'edits';batch.write_text('\n'.join(commands)+'\n');updater.run(['debugfs','-w','-f',str(batch),str(fs)]);updater.run(['e2fsck','-fn',str(fs)])
  with disk.open('r+b') as out,fs.open('rb') as src:out.seek(part['start']*512);shutil.copyfileobj(src,out)
def preset(disk,build,corrupt=False):
 key=(build/'installer-trusted-key.bin').read_bytes();files={};directories=[];records=[]
 for name in ('sample','shared'):
  raw=(build/'tests/installer'/(name+'.cbp')).read_bytes();b=bundle.verify_bundle(raw,key);base='/system/apps/_'+b['identity'].hex()+'.'+name;nb=name.encode();pb=base.encode();mb=b['manifest'];r=struct.pack('<QQQQ16sIIQ',64+len(nb)+len(pb)+len(mb),len(nb),len(pb),len(mb),b['identity'],b['flags'],0,b['stack'])+nb+pb+mb;records.append(struct.pack('<QQ',2**64-1,0)+r)
  files[base]=b'broken' if corrupt and name=='sample' else b['code'];files[base+'.manifest.json']=mb;files['/system/config/installations/packages/ffffffffffffffff-'+b['identity'].hex()+'.cbp']=raw
  directories.extend([base+'.resources',base+'.resources/sub','/system/root/_apps/'+base.rsplit('/',1)[-1]+'.data'])
  for n,data in b['resources'].items():files[base+'.resources/'+n]=data
 payload=struct.pack('<QQQQ',0x314745524243,32+sum(map(len,records)),2,len(records))+b''.join(records);files['/system/config/installations/registry.bin']=payload+hashlib.sha512(payload).digest();edit(disk,files,directories)

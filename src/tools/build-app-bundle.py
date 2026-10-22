#!/usr/bin/env python3
"""Create signed local Cloudberry bundles. Private keys are host-only."""
import argparse,json,os,pathlib,re,struct,subprocess,tempfile

def key(public, private):
    private=pathlib.Path(private);public=pathlib.Path(public)
    if not private.exists():
        private.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
        fd=os.open(private,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        with os.fdopen(fd,'wb') as out:subprocess.run(['openssl','genpkey','-algorithm','ED25519'],stdout=out,check=True)
    raw=subprocess.check_output(['openssl','pkey','-in',str(private),'-pubout','-outform','DER'])
    if len(raw)!=44 or raw[:12]!=bytes.fromhex('302a300506032b6570032100'):raise ValueError('not an Ed25519 key')
    public.parent.mkdir(parents=True,exist_ok=True);data=raw[12:]
    if not public.exists() or public.read_bytes()!=data:public.write_bytes(data)
    return data

def build(name,identity,executable,manifest,resources,private,output,launcher=False,stack=262144,revision=1):
    identity=bytes.fromhex(identity)
    if not re.fullmatch('[A-Za-z0-9][A-Za-z0-9_-]{0,206}',name) or len(identity)!=16 or not any(identity) or stack<4096 or not 1<=revision<2**64:raise ValueError('invalid name, identity or stack')
    code=pathlib.Path(executable).read_bytes();metadata=pathlib.Path(manifest).read_bytes();policy=json.loads(metadata)
    if set(policy)!={'version','resources'} or policy['version']!=1:raise ValueError('invalid manifest')
    for r in policy['resources']:
        if r.get('scope') in ('process-manager','display','filesystem','graphics') or (r.get('name','').startswith('/@') and r.get('publish')) or (r.get('name')=='/@io' and (r.get('scope','self')!='self' or any(x!='now_ns' for x in r['operations']))):raise ValueError('ordinary app authority ceiling')
    if code[:4]!=b'\x7fELF':raise ValueError('executable is not ELF')
    entries=[]
    if resources:
        root=pathlib.Path(resources)
        for p in sorted(root.rglob('*')):
            if p.is_symlink() or (not p.is_file() and not p.is_dir()):raise ValueError('unsupported resource type')
            if p.is_file():
                n=p.relative_to(root).as_posix().encode();data=p.read_bytes()
                if any(x in (b'',b'.',b'..') or len(x)>255 for x in n.split(b'/')) or b'\0' in n:raise ValueError('invalid resource name')
                entries.append(struct.pack('<QQ',len(n),len(data))+n+data)
    with tempfile.TemporaryDirectory() as temporary:
        pub=key(pathlib.Path(temporary)/'pub',private)
        payload=name.encode()+metadata+code+b''.join(entries);total=120+len(payload)+64
        header=b'CBPKG002'+struct.pack('<QQQQQQQ',total,len(name),len(metadata),len(code),len(entries),3 if launcher else 1,stack)+identity+pub+struct.pack('<Q',revision)
        assert len(header)==120
        message=pathlib.Path(temporary)/'message';message.write_bytes(header+payload)
        signature=subprocess.check_output(['openssl','pkeyutl','-sign','-rawin','-inkey',str(private),'-in',str(message)])
        if len(signature)!=64:raise ValueError('invalid signature size')
        pathlib.Path(output).write_bytes(header+payload+signature)
def verify_bundle(data,public):
    header=120 if data[:8]==b'CBPKG002' else 112
    revision=struct.unpack_from('<Q',data,112)[0] if header==120 and len(data)>=120 else 1
    if len(data)<header+64 or (header==112 and data[:8]!=b'CBPKG001') or not revision:raise ValueError('invalid signed bundle header')
    total,nb,mb,cb,count,flags,stack=struct.unpack_from('<QQQQQQQ',data,8)
    if total!=len(data) or not 1<=nb<=207 or not mb or not cb or flags not in (1,3) or stack<4096 or header+nb+mb+cb>len(data)-64 or not any(data[64:80]) or data[80:112]!=public:raise ValueError('invalid bundle fields or untrusted signing key')
    with tempfile.TemporaryDirectory() as temporary:
        root=pathlib.Path(temporary);(root/'key.der').write_bytes(bytes.fromhex('302a300506032b6570032100')+public);(root/'message').write_bytes(data[:-64]);(root/'signature').write_bytes(data[-64:])
        result=subprocess.run(['openssl','pkeyutl','-verify','-rawin','-pubin','-keyform','DER','-inkey',str(root/'key.der'),'-in',str(root/'message'),'-sigfile',str(root/'signature')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        if result.returncode:raise ValueError('invalid bundle signature')
    name=data[header:header+nb].decode('ascii')
    if not re.fullmatch('[A-Za-z0-9][A-Za-z0-9_-]{0,206}',name):raise ValueError('invalid application name')
    manifest=data[header+nb:header+nb+mb];code=data[header+nb+mb:header+nb+mb+cb];at=header+nb+mb+cb;resources={}
    for _ in range(count):
        if at+16>len(data)-64:raise ValueError('truncated resource')
        n,b=struct.unpack_from('<QQ',data,at);at+=16
        if not n or n>4095 or at+n+b>len(data)-64:raise ValueError('invalid resource lengths')
        raw=data[at:at+n];path=raw.decode('utf-8')
        if any(x in ('','.','..') or len(x.encode())>255 for x in path.split('/')) or any(ord(x)<32 or x=='\\' for x in path):raise ValueError('invalid resource path')
        if any(path==old or path.startswith(old+'/') or old.startswith(path+'/') for old in resources):raise ValueError('resource path collision')
        resources[path]=data[at+n:at+n+b];at+=n+b
    if at!=len(data)-64:raise ValueError('trailing bundle bytes')
    return dict(name=name,manifest=manifest,code=code,resources=resources,identity=data[64:80],flags=flags,stack=stack,revision=revision,issuer=data[80:112])

if __name__=='__main__':
    p=argparse.ArgumentParser();sub=p.add_subparsers(dest='command',required=True)
    k=sub.add_parser('key');k.add_argument('--private',required=True);k.add_argument('--public',required=True)
    b=sub.add_parser('build');b.add_argument('--name',required=True);b.add_argument('--identity',required=True);b.add_argument('--executable',required=True);b.add_argument('--manifest',required=True);b.add_argument('--resources');b.add_argument('--key',required=True);b.add_argument('--output',required=True);b.add_argument('--launcher',action='store_true');b.add_argument('--stack',type=int,default=262144);b.add_argument('--revision',type=int,default=1)
    a=p.parse_args()
    if a.command=='key':key(a.public,a.private)
    else:build(a.name,a.identity,a.executable,a.manifest,a.resources,a.key,a.output,a.launcher,a.stack,a.revision)

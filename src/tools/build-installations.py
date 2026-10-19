#!/usr/bin/env python3
"""Build the OS-owned installation catalog; never runs in the guest."""
import argparse,json,pathlib,re,struct

def build(spec,root,output):
    entries=json.loads(pathlib.Path(spec).read_text());records=[];names=set();identities=set();managers=0
    for e in entries:
        if set(e)!={'name','executable','manifest','installation_id','flags','cpu','stack_bytes'}:raise ValueError('unknown/missing installation fields')
        name=e['name'];identity=bytes.fromhex(e['installation_id']);flags=e['flags']
        if not re.fullmatch('[A-Za-z0-9][A-Za-z0-9_-]*',name) or name in names or len(identity)!=16 or not any(identity) or identity in identities or flags not in (1,3,4,8):raise ValueError('invalid/duplicate installation')
        names.add(name);identities.add(identity);managers+=flags==4
        path=pathlib.PurePosixPath(e['executable'])
        if not path.is_absolute() or '..' in path.parts or not str(path).startswith(('/system/apps/','/system/handlers/','/boot/kmodules/drivers/')):raise ValueError('executable escapes installation tree')
        if str(path).startswith('/system/'):
            expected=('/system/handlers/' if flags in (4,8) else '/system/apps/')+name
            if str(path)!=expected:raise ValueError('invalid flat executable name')
            program=pathlib.Path(root)/name/(name+'.elf')
        else:program=pathlib.Path(root)/str(path).removeprefix('/boot/kmodules/drivers/')
        if not program.is_file():raise ValueError('missing executable '+str(program))
        manifest_path=pathlib.PurePosixPath(e['manifest'])
        if manifest_path.is_absolute() or '..' in manifest_path.parts:raise ValueError('manifest escapes installation tree')
        manifest=(pathlib.Path(root)/manifest_path).read_bytes();policy=json.loads(manifest)
        if policy.get('version')!=1 or set(policy)!={'version','resources'}:raise ValueError('invalid manifest')
        if flags in (1,3):
            for grant in policy['resources']:
                if grant.get('scope') in ('process-manager','display','filesystem','graphics') or any(op!='now_ns' for op in grant.get('operations',[]) if grant.get('name')=='/@io') or (grant.get('name','').startswith('/@') and grant.get('publish')):raise ValueError('ordinary installation cannot claim kernel-resource publication or process authority')
        if flags==4 and e['cpu']!=0:raise ValueError('current lifecycle manager requires CPU0')
        if not isinstance(e['cpu'],int) or not 0<=e['cpu']<2**32 or not isinstance(e['stack_bytes'],int) or not 4096<=e['stack_bytes']<2**64:raise ValueError('invalid execution settings')
        n=name.encode();p=str(path).encode();size=64+len(n)+len(p)+len(manifest)
        records.append(struct.pack('<QQQQ16sIIQ',size,len(n),len(p),len(manifest),identity,flags,e['cpu'],e['stack_bytes'])+n+p+manifest)
    if managers!=1:raise ValueError('exactly one lifecycle manager required')
    content=b''.join(records);data=struct.pack('<IHHQQ',24,1,0,24+len(content),len(records))+content
    pathlib.Path(output).write_bytes(data)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('spec');p.add_argument('root');p.add_argument('output');a=p.parse_args();build(a.spec,a.root,a.output)

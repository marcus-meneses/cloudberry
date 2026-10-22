#!/usr/bin/env python3
"""Offline preservation refusals on small directory fixtures, no VM or mounts."""
import hashlib,importlib.util,pathlib,struct,sys,tempfile
root=pathlib.Path(__file__).resolve().parents[1];build=pathlib.Path(sys.argv[1]).resolve()
spec=importlib.util.spec_from_file_location('state',root/'tools/installation-state.py');state=importlib.util.module_from_spec(spec);spec.loader.exec_module(state)
key=(build/'installer-trusted-key.bin').read_bytes();signed=(build/'tests/installer/sample.cbp').read_bytes();bundle=state.bundle.verify_bundle(signed,key)
with tempfile.TemporaryDirectory(dir=build/'tests',prefix='installation-state-') as temporary:
    tree=pathlib.Path(temporary)/'old';candidate=pathlib.Path(temporary)/'candidate'
    for directory in (tree,candidate):
        (directory/'system/config/installations/packages').mkdir(parents=True);(directory/'system/handlers/installer.resources').mkdir(parents=True);(directory/'system/apps').mkdir(parents=True);(directory/'system/state').mkdir(parents=True);(directory/'system/handlers/installer.resources/trusted-key.bin').write_bytes(key)
    (candidate/'system/state/installed.bin').write_bytes((build/'src-handler-bundles/installed.bin').read_bytes())
    name=bundle['name'].encode();path=('/system/apps/_'+bundle['identity'].hex()+'.'+bundle['name']).encode();metadata=bundle['manifest'];size=64+len(name)+len(path)+len(metadata)
    record=struct.pack('<QQQQ16sIIQ',size,len(name),len(path),len(metadata),bundle['identity'],bundle['flags'],0,bundle['stack'])+name+path+metadata
    raw=struct.pack('<QQQQ',0x314745524243,32+16+size,2,1)+struct.pack('<QQ',2**64-1,0)+record
    protected=tree/'system/config/installations';(protected/'registry.bin').write_bytes(raw+hashlib.sha512(raw).digest());(protected/'transaction.bin').write_bytes(b'');(protected/'packages'/('ffffffffffffffff-'+bundle['identity'].hex()+'.cbp')).write_bytes(signed)
    for relative,data in {path.decode():bundle['code'],path.decode()+'.manifest.json':metadata,**{path.decode()+'.resources/'+n:b for n,b in bundle['resources'].items()}}.items():
        p=tree/relative.lstrip('/');p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    assert len(state.verify(tree,candidate))==1
    # Version-two records preserve approval reductions without rewriting the signed manifest.
    reduced=b'{"version":1,"resources":[{"name":"/@streams","operations":["read"]}]}'
    def v2_policy(policy,revision=1):
        n=64+len(name)+len(path)+len(policy);r=struct.pack('<QQQQ16sIIQ',n,len(name),len(path),len(policy),bundle['identity'],bundle['flags'],0,bundle['stack'])+name+path+policy
        p=struct.pack('<QQQQ',0x324745524243,32+16+n,3,1)+struct.pack('<QQ',2**64-1,revision)+r
        (protected/'registry.bin').write_bytes(p+hashlib.sha512(p).digest())
    v2_policy(reduced);assert len(state.verify(tree,candidate))==1
    def refused():
        try:state.verify(tree,candidate)
        except ValueError:return
        raise AssertionError('invalid installation accepted')
    v2_policy(reduced,2);refused()
    v2_policy(b'{"version":1,"resources":[{"name":"/@streams","operations":["invented"]}]}');refused()
    v2_policy(reduced)
    code=tree/path.decode().lstrip('/');code.write_bytes(b'changed');refused();code.write_bytes(bundle['code'])
    (protected/'transaction.bin').write_bytes(b'intent');refused();(protected/'transaction.bin').write_bytes(b'')
    trusted=candidate/'system/handlers/installer.resources/trusted-key.bin';trusted.write_bytes(bytes(32));refused();trusted.write_bytes(key)
    extra=tree/(path.decode().lstrip('/')+'.resources/unregistered');extra.write_bytes(b'extra');refused();extra.unlink()
    (candidate/'system/state/installed.bin').write_bytes(struct.pack('<IHHQQ',24,1,0,24+len(record),1)+record);refused()
print('PASS offline installed state: signed bytes, revisions/approval subsets, registry framing, modified code, unresolved WAL, issuer rotation, unregistered resources and built-in conflict')

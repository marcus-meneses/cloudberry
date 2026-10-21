import importlib.util,pathlib,sys,struct,zlib
spec=importlib.util.spec_from_file_location('identity',pathlib.Path(__file__).resolve().parents[1]/'tools/identity-db.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
root=pathlib.Path(sys.argv[1]);m.provision(root);directory=root/'system/config/identity'
first={p.name:p.read_bytes() for p in directory.iterdir()};m.provision(root);assert first=={p.name:p.read_bytes() for p in directory.iterdir()}
for i,name in enumerate(('users.bin','groups.bin','credentials.bin')):
    data=first[name];assert len(data)>=64 and data[:8]==m.MAGICS[i]
    saved=struct.unpack_from('<I',data,48)[0];copy=bytearray(data);struct.pack_into('<I',copy,48,0);assert saved==zlib.crc32(copy)
    assert (directory/name).stat().st_mode&0o777==0o600
assert directory.stat().st_mode&0o777==0o700 and len(first['credentials.bin'])==160
assert b'root' in first['users.bin'] and all(n in first['groups.bin'] for n in (b'root',b'admin',b'users'))
print('PASS identity image provisioning: three explicit binary formats, hash-only credential, root/groups and private modes; preserves existing data')

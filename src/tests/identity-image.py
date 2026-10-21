#!/usr/bin/env python3
"""Read a stopped disposable guest image; never mount or modify it."""
import importlib.util,json,pathlib,struct,subprocess,sys,zlib
image=pathlib.Path(sys.argv[1]).resolve();work=image.parent
spec=importlib.util.spec_from_file_location('boot_image',pathlib.Path(__file__).resolve().parents[1]/'tools/build-boot-image.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
partition=next(p for p in m.partitions(image) if p['index']==4);raw=work/'identity-ext4-check.img'
if raw.exists():raise RuntimeError('refusing to overwrite existing verification scratch image')
try:
    with image.open('rb') as source,raw.open('xb') as target:
        raw.chmod(0o600);source.seek(partition['start']*512);remaining=partition['sectors']*512
        while remaining:
            data=source.read(min(1024*1024,remaining));assert data;target.write(data);remaining-=len(data)
    report={};generations=set()
    for name,magic,count in (('users.bin',b'CBUSER01',1),('groups.bin',b'CBGRUP01',3),('credentials.bin',b'CBCRED01',1)):
        data=subprocess.run(['debugfs','-R','cat /system/config/identity/'+name,str(raw)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE).stdout
        assert len(data)>=64 and data[:8]==magic
        header=struct.unpack_from('<8sHHIQQQQI12s',data);assert header[1:4]==(1,0,64) and header[4]==len(data) and header[6]==count
        copy=bytearray(data);struct.pack_into('<I',copy,48,0);assert header[8]==zlib.crc32(copy);generations.add(header[5])
        if name=='users.bin':assert header[7]==2 and struct.unpack_from('<Q',data,72)[0]==0
        report[name]=dict(bytes=len(data),generation=header[5],count=count,next_id=header[7])
    journal=subprocess.run(['debugfs','-R','cat /system/config/identity/transaction.bin',str(raw)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE).stdout
    assert generations=={4} and journal==b''
    for path,uid in (('/tmp/fixture-owned.bin',1),('/tmp/root-private.bin',0)):
        metadata=subprocess.run(['debugfs','-R','stat '+path,str(raw)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE).stdout.decode()
        import re
        assert int(re.search(r'User:\s*(\d+)',metadata).group(1))==uid
        assert re.search(r'Mode:\s*0600',metadata)
    report['journal_bytes']=0;(work/'identity-persistence.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS durable identity database: coherent generation 4, root-only users/credentials, preserved UID high-water and empty journal')
finally:raw.unlink(missing_ok=True)

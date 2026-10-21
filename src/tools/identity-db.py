"""Versioned identity image provisioning; never executed by guest boot."""
import hashlib,pathlib,secrets,struct,zlib
MAGICS=(b'CBUSER01',b'CBGRUP01',b'CBCRED01')

def encode(kind,records,generation=1,next_id=0):
    body=b''.join(records)
    header=struct.pack('<8sHHIQQQQI12s',MAGICS[kind],1,0,64,64+len(body),generation,len(records),next_id,0,b'\0'*12)
    data=bytearray(header+body);struct.pack_into('<I',data,48,zlib.crc32(data));return bytes(data)

def user(uid,name,gid=2,active=1):
    n=name.encode('ascii');return struct.pack('<IIQQII',32+len(n),active,uid,gid,len(n),0)+n

def group(gid,name,members=()):
    n=name.encode('ascii');return struct.pack('<IIQIIQ',32+len(n)+8*len(members),int(gid<3),gid,len(n),len(members),0)+n+b''.join(struct.pack('<Q',u) for u in members)

def credential(uid,secret=None):
    # A boot-only root secret is generated in memory and never printed/stored.
    salt=secrets.token_bytes(32);password=secrets.token_bytes(32) if secret is None else secret
    digest=hashlib.pbkdf2_hmac('sha256',password,salt,600000,32)
    return struct.pack('<IIQIIII',96,1,uid,600000,32,32,0)+salt+digest

def provision(root):
    directory=pathlib.Path(root)/'system/config/identity';directory.mkdir(parents=True,exist_ok=True)
    paths=[directory/name for name in ('users.bin','groups.bin','credentials.bin')]
    present=[p.exists() for p in paths]
    if any(present) and not all(present):raise ValueError('incomplete identity database; refusing to replace account data')
    if not any(present):
        files=(encode(0,[user(0,'root',0)],next_id=1),encode(1,[group(0,'root',(0,)),group(1,'admin'),group(2,'users')],next_id=3),encode(2,[credential(0)]))
        for path,data in zip(paths,files):path.write_bytes(data)
    journal=directory/'transaction.bin'
    if not journal.exists():journal.write_bytes(b'')
    directory.chmod(0o700)
    for path in (*paths,journal):path.chmod(0o600)

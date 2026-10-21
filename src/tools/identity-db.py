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

def validate(directory):
    """Validate an existing version-1 snapshot without deriving/exposing secrets."""
    import re
    files=[];generations=[]
    for kind,name in enumerate(('users.bin','groups.bin','credentials.bin')):
        data=(pathlib.Path(directory)/name).read_bytes()
        if len(data)<64:raise ValueError('truncated identity snapshot')
        magic,major,minor,header,total,generation,count,next_id,crc,reserved=struct.unpack_from('<8sHHIQQQQI12s',data)
        check=bytearray(data);check[48:52]=bytes(4)
        if magic!=MAGICS[kind] or (major,minor,header)!=(1,0,64) or total!=len(data) or not generation or any(reserved) or zlib.crc32(check)!=crc:raise ValueError('invalid identity header/checksum')
        records={};names=set();at=64;previous=-1
        for _ in range(count):
            if at+32>len(data):raise ValueError('truncated identity record')
            size,flag,id=struct.unpack_from('<IIQ',data,at)
            if size<32 or size>len(data)-at or id<=previous or (kind<2 and id>=next_id):raise ValueError('invalid identity record extent/order')
            record=data[at:at+size];previous=id
            if kind<2:
                n=struct.unpack_from('<I',record,16 if kind else 24)[0];label=record[32:32+n]
                if flag>1 or not re.fullmatch(rb'[a-z_][a-z0-9_-]*',label) or label in names:raise ValueError('invalid identity name/status')
                names.add(label)
                if not kind:
                    if size!=32+n or struct.unpack_from('<I',record,28)[0]:raise ValueError('invalid user record')
                    records[id]=(label,struct.unpack_from('<Q',record,16)[0],flag)
                else:
                    members=struct.unpack_from('<I',record,20)[0]
                    if size!=32+n+8*members or struct.unpack_from('<Q',record,24)[0]:raise ValueError('invalid group record')
                    users=struct.unpack_from('<'+'Q'*members,record,32+n)
                    if list(users)!=sorted(set(users)):raise ValueError('invalid group membership order')
                    records[id]=(label,users,flag)
            else:
                if size!=96 or flag!=1 or struct.unpack_from('<I',record,16)[0]<600000 or struct.unpack_from('<III',record,20)!=(32,32,0):raise ValueError('invalid credential record')
                records[id]=True
            at+=size
        if at!=len(data) or (kind==2 and next_id) or (kind<2 and not next_id):raise ValueError('invalid identity count/high-water')
        files.append(records);generations.append(generation)
    users,groups,credentials=files
    if len(set(generations))!=1 or users.get(0)!=(b'root',0,1) or 0 not in credentials:raise ValueError('invalid root/generation')
    for id,label in enumerate((b'root',b'admin',b'users')):
        if id not in groups or groups[id][0]!=label or groups[id][2]!=1:raise ValueError('missing protected group')
    if groups[0][1]!=(0,) or any(gid not in groups or (uid and not gid) for uid,(_,gid,_) in users.items()) or any(uid not in users for _,members,_ in groups.values() for uid in members) or any(uid not in users for uid in credentials):raise ValueError('inconsistent identity references')
    return dict(generation=generations[0],users=len(users),groups=len(groups))

"""Host verification/preservation of explicitly installed signed applications."""
import hashlib,importlib.util,pathlib,re,struct
HERE=pathlib.Path(__file__).parent
spec=importlib.util.spec_from_file_location('app_bundle',HERE/'build-app-bundle.py');bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
DYNAMIC=re.compile(r'^_[0-9a-f]{32}\.[A-Za-z0-9][A-Za-z0-9_-]*(?:\.manifest\.json|\.resources(?:/.*)?)?$')

def dynamic_path(path):return path.startswith('system/apps/') and bool(DYNAMIC.fullmatch(path.removeprefix('system/apps/')))

def verify(root,candidate=None):
    root=pathlib.Path(root);state=root/'system/config/installations';path=state/'registry.bin'
    if not path.exists():return []
    if (state/'transaction.bin').read_bytes():raise ValueError('installation transaction requires guest recovery before offline update')
    data=path.read_bytes()
    if len(data)<96 or hashlib.sha512(data[:-64]).digest()!=data[-64:]:raise ValueError('invalid installation registry checksum')
    magic,bytes_,generation,count=struct.unpack_from('<QQQQ',data)
    if magic!=0x314745524243 or bytes_!=len(data)-64 or not generation:raise ValueError('invalid installation registry')
    key=(root/'system/handlers/installer.resources/trusted-key.bin').read_bytes()
    if len(key)!=32:raise ValueError('invalid installation trust key')
    if count and candidate and key!=(pathlib.Path(candidate)/'system/handlers/installer.resources/trusted-key.bin').read_bytes():raise ValueError('installed bundle signing key changed; explicit key-rotation support is required')
    builtin_names=set();builtin_ids=set()
    if candidate:
        raw=(pathlib.Path(candidate)/'system/state/installed.bin').read_bytes();offset=24
        while offset<len(raw):
            length,nb,pb,mb,identity,flags,cpu,stack=struct.unpack_from('<QQQQ16sIIQ',raw,offset);builtin_names.add(raw[offset+64:offset+64+nb].decode());builtin_ids.add(identity);offset+=length
    entries=[];at=32;names=set();ids=set();expected_global=set()
    for _ in range(count):
        if at+80>bytes_:raise ValueError('truncated installation record')
        uid,reserved=struct.unpack_from('<QQ',data,at);size,nb,pb,mb,identity,flags,cpu,stack=struct.unpack_from('<QQQQ16sIIQ',data,at+16)
        if reserved or size!=64+nb+pb+mb or at+16+size>bytes_ or not nb or flags not in (1,3) or cpu or stack<4096 or (uid,identity) in ids:raise ValueError('invalid installation record')
        name=data[at+80:at+80+nb].decode('ascii');path=data[at+80+nb:at+80+nb+pb].decode('utf-8');manifest=data[at+80+nb+pb:at+16+size];at+=16+size
        if (uid,name) in names or not re.fullmatch('[A-Za-z0-9][A-Za-z0-9_-]{0,206}',name):raise ValueError('duplicate/invalid application name')
        if name in builtin_names or identity in builtin_ids:raise ValueError('candidate built-in conflicts with installed application')
        names.add((uid,name));ids.add((uid,identity));p=pathlib.PurePosixPath(path);filename='_'+identity.hex()+'.'+name
        if p.name!=filename or '..' in p.parts or not p.is_absolute() or (uid==2**64-1 and p.parent!=pathlib.PurePosixPath('/system/apps')) or (uid!=2**64-1 and (len(p.parts)!=5 or p.parts[1]!='home' or p.parts[3]!='_apps')):raise ValueError('invalid installation path')
        signed=(state/'packages'/(f'{uid:016x}-'+identity.hex()+'.cbp')).read_bytes();verified=bundle.verify_bundle(signed,key)
        if verified['identity']!=identity or verified['name']!=name or verified['manifest']!=manifest or verified['flags']!=flags or verified['stack']!=stack:raise ValueError('installation metadata differs from signed bundle')
        files={path:verified['code'],path+'.manifest.json':manifest,**{path+'.resources/'+n:b for n,b in verified['resources'].items()}}
        for name_,contents in files.items():
            target=root/name_.lstrip('/')
            if not target.is_file() or target.is_symlink() or target.read_bytes()!=contents:raise ValueError('installed file differs from signed bundle: '+name_)
            if uid==2**64-1:expected_global.add(name_.lstrip('/'))
        entries.append(dict(owner=uid,name=name,app_id=identity.hex(),installation=path,files=list(files)))
    if at!=len(data)-64:raise ValueError('trailing installation records')
    actual={p.relative_to(root).as_posix() for p in (root/'system/apps').rglob('*') if p.is_file() and dynamic_path(p.relative_to(root).as_posix())}
    if actual!=expected_global:raise ValueError('unregistered/missing system application files')
    return entries

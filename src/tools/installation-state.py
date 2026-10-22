"""Host verification/preservation of explicitly installed signed applications."""
import hashlib,importlib.util,pathlib,re,struct,json
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
    if magic not in (0x314745524243,0x324745524243) or bytes_!=len(data)-64 or not generation:raise ValueError('invalid installation registry')
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
        if (reserved if magic==0x314745524243 else not reserved) or size!=64+nb+pb+mb or at+16+size>bytes_ or not nb or flags not in (1,3) or cpu or stack<4096 or (uid,identity) in ids:raise ValueError('invalid installation record')
        name=data[at+80:at+80+nb].decode('ascii');path=data[at+80+nb:at+80+nb+pb].decode('utf-8');manifest=data[at+80+nb+pb:at+16+size];at+=16+size
        if (uid,name) in names or not re.fullmatch('[A-Za-z0-9][A-Za-z0-9_-]{0,206}',name):raise ValueError('duplicate/invalid application name')
        if name in builtin_names or identity in builtin_ids:raise ValueError('candidate built-in conflicts with installed application')
        names.add((uid,name));ids.add((uid,identity));p=pathlib.PurePosixPath(path);filename='_'+identity.hex()+'.'+name
        if p.name!=filename or '..' in p.parts or not p.is_absolute() or (uid==2**64-1 and p.parent!=pathlib.PurePosixPath('/system/apps')) or (uid!=2**64-1 and (len(p.parts)!=5 or p.parts[1]!='home' or p.parts[3]!='_apps')):raise ValueError('invalid installation path')
        signed=(state/'packages'/(f'{uid:016x}-'+identity.hex()+'.cbp')).read_bytes();verified=bundle.verify_bundle(signed,key)
        if verified['revision']!=(1 if magic==0x314745524243 else reserved) or verified['identity']!=identity or verified['name']!=name or not policy_subset(manifest,verified['manifest']) or verified['flags']!=flags or verified['stack']!=stack:raise ValueError('installation metadata differs from signed bundle')
        files={path:verified['code'],path+'.manifest.json':verified['manifest'],**{path+'.resources/'+n:b for n,b in verified['resources'].items()}}
        for name_,contents in files.items():
            target=root/name_.lstrip('/')
            if not target.is_file() or target.is_symlink() or target.read_bytes()!=contents:raise ValueError('installed file differs from signed bundle: '+name_)
            if uid==2**64-1:expected_global.add(name_.lstrip('/'))
        entries.append(dict(owner=uid,name=name,app_id=identity.hex(),installation=path,files=list(files)))
    if at!=len(data)-64:raise ValueError('trailing installation records')
    actual={p.relative_to(root).as_posix() for p in (root/'system/apps').rglob('*') if p.is_file() and dynamic_path(p.relative_to(root).as_posix())}
    if actual!=expected_global:raise ValueError('unregistered/missing system application files')
    return entries

def policy_subset(approved,requested):
    def unique(pairs):
        result={}
        for name,value in pairs:
            if name in result:raise ValueError('duplicate JSON key')
            result[name]=value
        return result
    def parse(raw):
        doc=json.loads(raw,object_pairs_hook=unique)
        if not isinstance(doc,dict) or set(doc)!={'version','resources'} or type(doc['version']) is not int or doc['version']!=1 or not isinstance(doc['resources'],list):raise ValueError('invalid policy')
        result={}
        for r in doc['resources']:
            if not isinstance(r,dict) or not {'name','operations'}<=set(r) or set(r)-{'name','operations','scope','publish'}:raise ValueError('invalid resource')
            name=r['name'];scope=r.get('scope','self');publish=r.get('publish',False);ops=r['operations']
            if not isinstance(name,str) or not name or not name.isascii() or not isinstance(scope,str) or not scope or not scope.isascii() or type(publish) is not bool or not isinstance(ops,list) or any(not isinstance(o,str) or not o or not o.isascii() for o in ops):raise ValueError('invalid permission fields')
            if name in result or len(set(ops))!=len(ops):raise ValueError('duplicate permission')
            result[name]=(scope,publish,set(ops))
        return result
    try:
        a,r=parse(approved),parse(requested)
        return all(n in r and value[:2]==r[n][:2] and value[2]<=r[n][2] for n,value in a.items())
    except (ValueError,KeyError,TypeError):return False

"""Build-owned coherence manifest; excludes mutable identity and application data."""
import hashlib,json,pathlib

def normalized_config(data):
    return b'\n'.join(line for line in data.split(b'\n') if not line.startswith(b'system.build_id=')).rstrip(b'\n')+b'\n'

def managed(root):
    for p in sorted(pathlib.Path(root).rglob('*')):
        relative=p.relative_to(root).as_posix()
        if p.is_file() and (relative=='system/state/installed.bin' or (relative.startswith('system/') and not relative.startswith(('system/root/','system/config/identity/','system/state/')))):
            yield p,relative

def generate(stage,root):
    config=stage/'cloudberry.cnf';config.write_bytes(normalized_config(config.read_bytes()))
    records=[]
    for p in sorted(stage.rglob('*')):
        if p.is_file() and p.name!='build.id':records.append(dict(tree='boot',path=p.relative_to(stage).as_posix(),sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
    for p,name in managed(root):records.append(dict(tree='system',path=name,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),mode=p.stat().st_mode&0o7777))
    data=json.dumps(dict(version=1,files=records),sort_keys=True,separators=(',',':')).encode()+b'\n';identity=hashlib.sha256(data).hexdigest()
    (root/'system/state/build.manifest.json').write_bytes(data);(root/'system/state/build.id').write_text(identity+'\n');(stage/'build.id').write_text(identity+'\n')
    with config.open('ab') as stream:stream.write(('system.build_id='+identity+'\n').encode())
    return identity

def verify(boot,root):
    raw=(root/'system/state/build.manifest.json').read_bytes();identity=hashlib.sha256(raw).hexdigest();document=json.loads(raw)
    if document.get('version')!=1 or (root/'system/state/build.id').read_text().strip()!=identity or (boot/'build.id').read_text().strip()!=identity:raise ValueError('image build identity mismatch')
    values=[line.split(b'=',1)[1] for line in (boot/'cloudberry.cnf').read_bytes().splitlines() if line.startswith(b'system.build_id=')]
    if values!=[identity.encode()]:raise ValueError('boot configuration/build identity mismatch')
    seen=set()
    for r in document['files']:
        if r['tree'] not in ('boot','system'):raise ValueError('invalid manifest tree')
        name=pathlib.PurePosixPath(r['path'])
        if name.is_absolute() or '..' in name.parts or not name.parts or (r['tree'],str(name)) in seen:raise ValueError('invalid/duplicate manifest path')
        seen.add((r['tree'],str(name)));file=(boot if r['tree']=='boot' else root)/name
        if file.is_symlink() or not file.is_file():raise ValueError('missing managed file: '+str(name))
        data=file.read_bytes()
        if r['tree']=='boot' and str(name)=='cloudberry.cnf':data=normalized_config(data)
        if hashlib.sha256(data).hexdigest()!=r['sha256']:raise ValueError('mixed/corrupt managed file: '+str(name))
        if r['tree']=='system' and file.stat().st_mode&0o7777!=r['mode']:raise ValueError('managed file mode mismatch: '+str(name))
    if {name for p,name in managed(root)}!={r['path'] for r in document['files'] if r['tree']=='system'}:raise ValueError('unrecorded managed system files')
    return identity

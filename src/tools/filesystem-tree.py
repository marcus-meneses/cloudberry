"""Build the guest namespace from compiler bundle intermediates; never run at boot."""
import json,pathlib,shutil,struct,importlib.util

DIRECTORIES = ('boot','system/apps',
 'system/handlers','system/kmodules/drivers','system/kmodules/helpers','system/fonts',
 'system/config/keyboard','system/config/identity','system/state','system/root/_config','system/root/_apps',
 'home','volumes','tmp')

def populate(root, bundles=None):
    root=pathlib.Path(root)
    for name in DIRECTORIES:(root/name).mkdir(parents=True,exist_ok=True)
    indexes={'system/apps':[], 'system/root/_apps':[]}
    if bundles is not None:
        bundles=pathlib.Path(bundles)
        # Loose low-level driver fixtures remain in the explicitly configured driver area.
        drivers=root/'system/kmodules/drivers'
        shutil.copytree(bundles,drivers,dirs_exist_ok=True)
        catalog=bundles/'installed.bin'
        if catalog.exists():
            data=catalog.read_bytes();header,major,minor,total,count=struct.unpack_from('<IHHQQ',data)
            if header!=24 or major!=1 or minor or total!=len(data):raise ValueError('invalid filesystem installation catalog')
            at=24
            for _ in range(count):
                size,nb,pb,mb,identity,flags,cpu,stack=struct.unpack_from('<QQQQ16sIIQ',data,at)
                if size!=64+nb+pb+mb or at+size>len(data):raise ValueError('invalid installation record')
                name=data[at+64:at+64+nb].decode();target=data[at+64+nb:at+64+nb+pb].decode();manifest=data[at+64+nb+pb:at+size];at+=size
                if target.startswith(('/system/apps/','/system/handlers/')):
                    expected='/system/handlers/' if flags in (4,8) else '/system/apps/'
                    if target!=expected+name or name.startswith('_') or '/' in name:raise ValueError('invalid flat installation path')
                    source=bundles/name;destination=root/target.lstrip('/');destination.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copy2(source/(name+'.elf'),destination)
                    destination.with_name(name+'.manifest.json').write_bytes(manifest)
                    resources=destination.with_name(name+'.resources');resources.mkdir()
                    for item in source.iterdir():
                        if item.name in (name+'.elf','manifest.json','provider.cnf') or (name=='keyboard' and item.name in ('config.cnf','layouts')):continue
                        if item.is_dir():shutil.copytree(item,resources/item.name)
                        else:shutil.copy2(item,resources/item.name)
                    definition=source/'provider.cnf'
                    if definition.exists():
                        values=dict(line.split('=',1) for line in definition.read_text().splitlines() if line and not line.startswith('#'))
                        values.update(executable=name,manifest=name+'.manifest.json')
                        aliases=[]
                        for item in values.get('files','').split(','):
                            if not item:continue
                            target_file=expected+name+'.resources/'+item
                            if name=='keyboard':
                                target_file='/system/config/keyboard/'+('config.cnf' if item=='config.cnf' else pathlib.PurePosixPath(item).stem+'.keymap')
                                shutil.copy2(source/item,root/target_file.lstrip('/'))
                            aliases.append(item+'='+target_file)
                        if 'files' in values:values['files']=','.join(aliases)
                        destination.with_name(name+'.provider.cnf').write_text(''.join(k+'='+v+'\n' for k,v in values.items()))
                    shutil.rmtree(drivers/name)
                    record=dict(app_id=identity.hex(),name=name,installation=target)
                    if flags in (1,3):
                        indexes['system/apps'].append(record)
                        indexes['system/root/_apps'].append(record)
                        (root/'system/root/_apps'/(name+'.data')).mkdir()
            if at!=len(data):raise ValueError('trailing installation data')
            shutil.copy2(catalog,root/'system/state/installed.bin')
            (drivers/'installed.bin').unlink()
        # Build markers and source specifications are never guest files.
        for name in ('.ready','test-installed.json'):(drivers/name).unlink(missing_ok=True)
    for directory,entries in indexes.items():
        (root/directory/'_index').write_text(json.dumps(dict(version=1,entries=entries),indent=2)+'\n')

    for entry in root.rglob('*'):
        entry.chmod(0o755 if entry.is_dir() else 0o644)
    for entry in (root/'system/apps').iterdir():
        if entry.is_file() and not entry.name.startswith('_') and '.' not in entry.name:entry.chmod(0o755)
    for entry in (root/'system/handlers').iterdir():
        if entry.is_file() and '.' not in entry.name:entry.chmod(0o755)
    for name in ('system/root','system/root/_config','system/root/_apps','system/state'):
        (root/name).chmod(0o700)
    for entry in (root/'system/root/_apps').glob('*.data'):entry.chmod(0o700)
    (root/'tmp').chmod(0o1777)

    spec=importlib.util.spec_from_file_location('identity_db',pathlib.Path(__file__).with_name('identity-db.py'))
    identity=importlib.util.module_from_spec(spec);spec.loader.exec_module(identity);identity.provision(root)

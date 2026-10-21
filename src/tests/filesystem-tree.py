#!/usr/bin/env python3
"""Check production image namespace and catalog-derived names without a VM."""
import importlib.util,json,pathlib,struct,sys,tempfile
source=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tree',source/'tools/filesystem-tree.py')
tree=importlib.util.module_from_spec(spec);spec.loader.exec_module(tree)
bundles=pathlib.Path(sys.argv[1]);stage=pathlib.Path(sys.argv[2]);work=pathlib.Path(sys.argv[3]);work.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=work) as temporary:
    root=pathlib.Path(temporary);tree.populate(root,bundles)
    for name in tree.DIRECTORIES:assert (root/name).is_dir(),name
    assert not (root/'system/users').exists() and not (root/'home/root').exists()
    assert not (root/'boot/system').exists()
    assert not list((root/'boot').iterdir())
    assert not list((root/'system/kmodules/drivers').iterdir())
    assert (root/'system/root').stat().st_mode&0o777==0o700
    records=json.loads((source/'apps/installed.json').read_text())
    apps=[r for r in records if r['flags'] in (1,3)]
    index=json.loads((root/'system/apps/_index').read_text())
    assert {r['app_id'] for r in index['entries']}=={r['installation_id'] for r in apps}
    assert json.loads((root/'system/root/_apps/_index').read_text())==index
    for record in records:
        executable=root/record['executable'].lstrip('/');name=record['name']
        assert executable.read_bytes()==(bundles/name/(name+'.elf')).read_bytes()
        assert executable.with_name(name+'.manifest.json').read_bytes()==(bundles/record['manifest']).read_bytes()
        assert executable.with_name(name+'.resources').is_dir()
        if record['flags'] in (1,3):assert (root/'system/root/_apps'/(name+'.data')).is_dir()
    for layout in ('ibm','abnt2'):
        assert (root/'system/config/keyboard'/(layout+'.keymap')).read_bytes()==(bundles/'keyboard/layouts'/(layout+'.json')).read_bytes()
    keyboard=(root/'system/handlers/keyboard.provider.cnf').read_text()
    assert 'layouts/ibm.json=/system/config/keyboard/ibm.keymap' in keyboard
    assert (root/'system/state/installed.bin').read_bytes()==(root/'system/handlers/applications.resources/catalog.bin').read_bytes()
    assert not list((root/'system/apps').glob('*.elf'))
    config=(stage/'limine/limine.conf').read_text()
    for line in config.splitlines():
        if 'module_path: boot():/' not in line:continue
        path=line.split('boot():/',1)[1]
        assert (stage/path).is_file(),path
        if path.endswith('.kmod'):assert path.startswith(('kmodules/drivers/','kmodules/helpers/')),path
print('PASS filesystem tree: flat apps/providers, identity indexes, private root state, keyboard aliases, clean boot directory and Limine module paths')

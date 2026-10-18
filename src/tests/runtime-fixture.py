"""Extend only a disposable driver tree with installed application fixtures."""
import hashlib,importlib.util,json,pathlib,shutil,struct

def install(root,build):
    source=pathlib.Path(__file__).resolve().parents[1]
    registry=json.loads((source/'apps/installed.json').read_text());folder=root/'runtime-test';folder.mkdir(exist_ok=True)
    for name,flags in [('runtime-probe',3),('runtime-context',1),('runtime-fault',1),('runtime-cooperative',1),('runtime-attacker',1),('runtime-abandon',3)]:
        shutil.copy2(build/'tests/apps/application-runtime.elf',folder/(name+'.elf'))
        grants=[]
        if name in ('runtime-probe','runtime-attacker','runtime-abandon'):grants.append(dict(name='/@applications',operations=['launch','info','wait','stop','release']))
        if name=='runtime-probe':grants.append(dict(name='/@io',scope='keyboard',operations=['now_ns']))
        (folder/(name+'.json')).write_text(json.dumps(dict(version=1,resources=grants)))
        registry.append(dict(name=name,executable='/boot/system/drivers/runtime-test/'+name+'.elf',manifest='runtime-test/'+name+'.json',installation_id=hashlib.sha256(name.encode()).hexdigest()[:32],flags=flags,cpu=0,stack_bytes=131072))
    spec=root/'test-installed.json';spec.write_text(json.dumps(registry))
    loader=importlib.util.spec_from_file_location('installation_builder',source/'tools/build-installations.py');module=importlib.util.module_from_spec(loader);loader.loader.exec_module(module);module.build(spec,root,root/'installed.bin')
    shutil.copy2(root/'installed.bin',root/'applications/catalog.bin')
    name=b'runtime-probe';args=name+b'\0';cwd=b'/'
    (root/'applications/initial.bin').write_bytes(struct.pack('<IHHQQQQ',40,1,0,len(name),1,len(args),len(cwd))+name+args+cwd)

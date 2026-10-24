"""Compile-time fixture staging; never modifies candidate bundles or disks."""
import importlib.util,json,pathlib,shutil,struct
ROOT=pathlib.Path(__file__).resolve().parents[2]
SRC=ROOT/'src'
def prepare(build,work,backend,fault=None,profile=False):
    build=pathlib.Path(build);work=pathlib.Path(work)
    if backend not in ('vga','virtio','virgl'):raise ValueError('invalid paired backend')
    if fault not in (None,'open','scanout','graphics','release') or (fault and backend not in ('virtio','virgl')) or (backend=='virtio' and fault not in (None,'open','release')):raise ValueError('invalid startup fault/backend')
    if profile and (backend!='virgl' or fault):raise ValueError('profiling requires a healthy VirGL pair')
    stage=work/'stage';bundles=work/'bundles';shutil.copytree(build/'src-iso-root',stage);shutil.copytree(build/'src-handler-bundles',bundles)
    records=json.loads((SRC/'apps/installed.json').read_text())
    # Both provider bundles/catalog records are now production image contents.
    for name in ('display-opengl','graphics-opengl'):
        if not (bundles/name/(name+'.elf')).is_file() or sum(r['name']==name for r in records)!=1:raise ValueError('missing production renderer installation')
    name='gpu-pair';directory=bundles/name;directory.mkdir();shutil.copy2(build/('tests/apps/gpu-pair-profile.elf' if profile else 'tests/apps/gpu-pair.elf'),directory/'gpu-pair.elf')
    manifest=dict(version=1,resources=[dict(name='@gpu-pair-test',publish=True,operations=['phase']),dict(name='/@graphics',operations=['info','fill','surface_create','rectangle','text_batch','surface_place','present']),dict(name='/@io',scope='self',operations=['now_ns']),dict(name='/@keyboard',scope='boot-input',operations=['focus_claim','read']),dict(name='/@mouse',scope='boot-input',operations=['cursor_info','cursor_hide','cursor_show'])])
    (directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    records.append(dict(name=name,executable='/system/apps/'+name,manifest=name+'/manifest.json',installation_id='43425445535447505550414952000001',flags=3,cpu=0,stack_bytes=262144))
    spec=work/'installed.json';spec.write_text(json.dumps(records,indent=2)+'\n')
    module=importlib.util.spec_from_file_location('build_installations',SRC/'tools/build-installations.py');builder=importlib.util.module_from_spec(module);module.loader.exec_module(builder)
    builder.build(spec,bundles,bundles/'installed.bin')
    for provider in ('applications','identity','installer'):shutil.copy2(bundles/'installed.bin',bundles/provider/'catalog.bin')
    name=b'gpu-pair';args=name+b'\0';(bundles/'applications/initial.bin').write_bytes(struct.pack('<IHHQQQQ',40,1,0,len(name),1,len(args),1)+name+args+b'/')
    if fault:
        name='graphics-opengl' if fault=='graphics' else 'display-opengl'
        shutil.copy2(build/'tests/apps'/('gpu-pair-fail-'+fault+'.elf'),bundles/name/(name+'.elf'))
    shutil.copy2(build/'tests'/('gpu-pair-'+('profile' if profile else 'fault' if fault else 'virgl' if backend=='virgl' else 'vga')+'.kmod'),bundles/'gpu-virtio.kmod')
    if profile:
        paths=list(stage.rglob('handles.kmod'))
        if len(paths)!=1:raise ValueError('expected one boot broker module')
        shutil.copy2(build/'tests/handles-profile.kmod',paths[0])
    # Keep normal boot roles and parser schema. Test admission lives exclusively
    # in the replaced module; no production config flag enables it.
    return stage,bundles

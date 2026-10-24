#!/usr/bin/env python3
"""Fixture isolation/catalog and screen-evidence checks, without QEMU."""
import importlib.util,json,pathlib,struct,tempfile,unittest,subprocess
from gpu_pair_image import prepare,ROOT,SRC
from gpu_pair_frame import inspect,validate_capture,complete_log,phase_records,profile_records
class Fixture(unittest.TestCase):
    def test_lifecycle_self_exit(self):
        (ROOT/'bin/tests').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT/'bin/tests',prefix='pair-life-') as directory:
            binary=pathlib.Path(directory)/'lifecycle'
            subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(SRC/'tests/gpu-pair-lifecycle.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
    def test_profile_evidence(self):
        rows=[dict(kind='window',samples=32,total_ns=528,max_ns=32,clock_source=1)]
        rows.extend(dict(kind='frame',sample=i,ns=i+1) for i in range(32))
        rows.extend([dict(kind='gpu_fence',id=0x207,calls=32),dict(kind='broker_invoke',pid=1,count=64)])
        def encode(values):return ''.join('GPU_PROFILE '+json.dumps(r)+'\n' for r in values)+'GPU_PROFILE PASS\n'
        self.assertEqual(profile_records(encode(rows)),rows)
        for bad in (rows[:-1],rows[:3]+rows[4:],rows+[dict(kind='gpu_io',id=13,calls=1)]):
            with self.assertRaises(ValueError):profile_records(encode(bad))
        with self.assertRaises(ValueError):profile_records(encode(rows)[:-1])
        with self.assertRaises(ValueError):profile_records(encode(rows).replace('"total_ns": 528','"total_ns": 529'))
    def test_copied_catalog_only(self):
        (ROOT/'bin/tests').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT/'bin/tests',prefix='pair-host-') as temporary:
            root=pathlib.Path(temporary);build=root/'build';stage=build/'src-iso-root';bundles=build/'src-handler-bundles';stage.mkdir(parents=True);bundles.mkdir();(stage/'cloudberry.cnf').write_text('version=1\n')
            records=json.loads((SRC/'apps/installed.json').read_text())
            for record in records:
                name=record['name'];directory=bundles/name;directory.mkdir();(directory/(name+'.elf')).write_bytes(b'test executable')
                source=SRC/('handlers' if record['flags'] in (4,8) else 'apps')/name
                (directory/'manifest.json').write_bytes((source/'manifest.json').read_bytes())
            for name in ('applications','identity','installer'):(bundles/name/'catalog.bin').write_bytes(b'original catalog')
            (bundles/'installed.bin').write_bytes(b'original catalog');(bundles/'gpu-virtio.kmod').write_bytes(b'original module');(bundles/'applications/initial.bin').write_bytes(b'original app')
            for name in ('UbuntuMono-R.ttf','FreeType-LICENSE.txt','FTL.TXT','COPYING.NEWLIB'):(bundles/'graphics-vga'/name).write_bytes(b'fixture asset')
            (build/'apps').mkdir();(build/'tests/apps').mkdir(parents=True)
            for name in ('display-opengl','graphics-opengl'):(build/'apps'/(name+'.elf')).write_bytes(b'experimental '+name.encode())
            (build/'tests/apps/gpu-pair.elf').write_bytes(b'client')
            for backend in ('vga','virgl'):(build/'tests'/('gpu-pair-'+backend+'.kmod')).write_bytes(backend.encode())
            work=root/'work';work.mkdir();copied,catalog=prepare(build,work,'virgl')
            self.assertEqual((stage/'cloudberry.cnf').read_text(),'version=1\n');self.assertEqual((bundles/'installed.bin').read_bytes(),b'original catalog');self.assertEqual((bundles/'gpu-virtio.kmod').read_bytes(),b'original module');self.assertEqual((bundles/'applications/initial.bin').read_bytes(),b'original app')
            self.assertEqual((bundles/'graphics-opengl/graphics-opengl.elf').read_bytes(),b'test executable');self.assertEqual((catalog/'gpu-virtio.kmod').read_bytes(),b'virgl')
            (build/'tests/gpu-pair-fault.kmod').write_bytes(b'fault module')
            for case in ('open','scanout','graphics','release'):
                (build/'tests/apps'/('gpu-pair-fail-'+case+'.elf')).write_bytes(case.encode())
                fixture=root/('fault-'+case);fixture.mkdir();_,staged=prepare(build,fixture,'virgl',case)
                provider='graphics-opengl' if case=='graphics' else 'display-opengl'
                self.assertEqual((staged/provider/(provider+'.elf')).read_bytes(),case.encode())
                self.assertEqual((staged/'gpu-virtio.kmod').read_bytes(),b'fault module')
                self.assertEqual((build/'apps'/(provider+'.elf')).read_bytes(),b'experimental '+provider.encode());self.assertEqual((bundles/provider/(provider+'.elf')).read_bytes(),b'test executable')
                self.assertEqual((bundles/'gpu-virtio.kmod').read_bytes(),b'original module')
            (stage/'kmodules/helpers').mkdir(parents=True);(stage/'kmodules/helpers/handles.kmod').write_bytes(b'production broker')
            (build/'tests/handles-profile.kmod').write_bytes(b'profile broker');(build/'tests/gpu-pair-profile.kmod').write_bytes(b'profile GPU')
            (build/'tests/apps/gpu-pair-profile.elf').write_bytes(b'profile app')
            profiling=root/'profiling';profiling.mkdir();profile_stage,profile_bundles=prepare(build,profiling,'virgl',profile=True)
            self.assertEqual((profile_stage/'kmodules/helpers/handles.kmod').read_bytes(),b'profile broker')
            self.assertEqual((profile_bundles/'gpu-virtio.kmod').read_bytes(),b'profile GPU')
            self.assertEqual((profile_bundles/'gpu-pair/gpu-pair.elf').read_bytes(),b'profile app')
            self.assertEqual((stage/'kmodules/helpers/handles.kmod').read_bytes(),b'production broker')
            with self.assertRaises(ValueError):prepare(build,root/'bad-profile','vga',profile=True)
            with self.assertRaises(ValueError):prepare(build,root/'bad','virtio','graphics')
            with self.assertRaises(ValueError):prepare(build,root/'bad','vga','open')

            spec=json.loads((work/'installed.json').read_text());by_name={r['name']:r for r in spec}
            for name in ('display-opengl','graphics-opengl'):self.assertEqual(by_name[name]['flags'],8);self.assertEqual(by_name[name]['executable'],'/system/handlers/'+name)
            for provider in ('applications','identity','installer'):self.assertEqual((catalog/provider/'catalog.bin').read_bytes(),(catalog/'installed.bin').read_bytes())
            initial=(catalog/'applications/initial.bin').read_bytes();self.assertEqual(struct.unpack_from('<IHHQQQQ',initial),(40,1,0,8,1,9,1));self.assertEqual(initial[40:],b'gpu-pairgpu-pair\0/')
    def test_fragmented_log_records(self):
        record='GPU_PAIR PHASE 8 width=1280 height=800 x=1219 y=753\n'
        for split in range(len(record)):
            self.assertEqual(phase_records(record[:split]),[])
            self.assertEqual(complete_log(record[:split]),'')
        self.assertEqual(phase_records(record),[(8,1280,800,1219,753)])
        previous='GPU_PAIR PHASE 1 width=640 height=480 x=0 y=0\n'
        self.assertEqual(phase_records(previous+record[:-2]),[(1,640,480,0,0)])
        self.assertEqual(phase_records(previous+record),[(1,640,480,0,0),(8,1280,800,1219,753)])
        for text in ('GPU_PAIR PASS accelerated=1 captures=14 graphics_calls=114\n',
                     'GPU_PAIR STARTUP REJECTED code=204 display=2 failed=2\n'):
            for split in range(len(text)):
                self.assertEqual(complete_log(previous+text[:split]),previous)
            self.assertEqual(complete_log(previous+text),previous+text)
    def test_strict_scanout_gate(self):
        blank={'state':'empty-ram-surface'}
        validate_capture(blank,1,False)
        with self.assertRaises(ValueError):validate_capture(blank,1,True)
        with self.assertRaises(ValueError):validate_capture(blank,10,False)
        validate_capture({'state':'matched'},10,True)
    def test_restored_frame(self):
        w,h=320,160;pixels=bytearray(bytes((48,64,80))*w*h)
        def put(x,y,rgb):pixels[(y*w+x)*3:(y*w+x)*3+3]=rgb.to_bytes(3,'big')
        for y in range(24,72):
            for x in range(32,112):put(x,y,0x55aa11)
        for x in range(16,32):put(x,100,0xffffff)
        header=f'P6\n{w} {h}\n255\n'.encode();state=dict(width=w,height=h,x=0,y=0)
        self.assertEqual(inspect(header+pixels,10,state)['state'],'matched')
        put(32,24,0);self.assertEqual(inspect(header+pixels,10,state)['state'],'mismatch')
    def test_visible_evidence(self):
        w,h=320,160;state=dict(width=w,height=h,x=270,y=120);pixels=bytearray(bytes((16,32,48))*w*h)
        def put(x,y,value):pixels[(y*w+x)*3:(y*w+x)*3+3]=value.to_bytes(3,'big')
        for x,y,c in [(20,20,0xff0000),(48,20,0x00ff00),(20,48,0x0000ff),(48,48,0xffffff)]:put(x,y,c)
        for x in range(100,120):put(x,18,0xffffff);put(x,19,0xffaa44)
        header=f'P6\n{w} {h}\n255\n'.encode()
        self.assertEqual(inspect(header+pixels,1,state)['state'],'matched')
        put(20,20,0);self.assertEqual(inspect(header+pixels,1,state)['state'],'mismatch')
        self.assertEqual(inspect(header+bytes(w*h*3),1,state)['state'],'empty-ram-surface')
        with self.assertRaises(ValueError):inspect(b'not an image',1,state)
if __name__=='__main__':unittest.main()

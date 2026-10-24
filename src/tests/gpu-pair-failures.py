#!/usr/bin/env python3
"""Run the isolated startup rollback matrix; keep one aggregate result."""
import argparse,json,pathlib,re,subprocess,sys,uuid
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('build',type=pathlib.Path)
p.add_argument('--cpus',type=int,default=4)
p.add_argument('--accel',choices=['tcg','kvm'],default='kvm')
a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2]
work=root/'bin/tests/gpu-pair-failures'/uuid.uuid4().hex;work.mkdir(parents=True)
result=dict(status='running',accelerator=a.accel,cpus=a.cpus,cases={})
def save():(work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
save()
for case in ('open','scanout','graphics','release'):
    print('Startup rollback: '+case,flush=True)
    command=[sys.executable,str(root/'src/tests/gpu-pair-boot.py'),str(a.build.resolve()),'--backend=virgl','--display=egl-headless','--require-screen','--accel='+a.accel,'--cpus='+str(a.cpus),'--startup-fault='+case]
    with (work/(case+'.log')).open('w') as out:
        completed=subprocess.run(command,stdout=out,stderr=subprocess.STDOUT)
    text=(work/(case+'.log')).read_text();print(text,end='',flush=True)
    result['cases'][case]=dict(exit_code=completed.returncode,log=str(work/(case+'.log')))
    match=re.search(r'report (.+/result.json)',text)
    if match:
        child_path=pathlib.Path(match.group(1));child=json.loads(child_path.read_text())
        result['cases'][case].update(report=str(child_path),status=child['status'],startup_fallback_verified=child.get('startup_fallback_verified',False),cleanup_failure_rejected=child.get('cleanup_failure_rejected',False),screen_capture_verified=child.get('screen_capture_verified',False),production_probe_verified=child.get('production_probe_verified',False),production_release_verified=child.get('production_release_verified',False))

    save()
    if completed.returncode:
        result['status']='failed';break
else:result['status']='passed'
save();print(result['status'].upper()+': startup rollback matrix, report '+str(work/'result.json'),flush=True)
raise SystemExit(0 if result['status']=='passed' else 1)

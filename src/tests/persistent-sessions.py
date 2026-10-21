#!/usr/bin/env python3
"""Two isolated boots: provision/login/write, then authenticate/read persisted data."""
import argparse,json,pathlib,re,subprocess,sys,uuid
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('--accel',choices=['tcg','kvm'],default='tcg');p.add_argument('--cpus',type=int,default=4);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2];work=root/'bin/tests/persistent-sessions'/uuid.uuid4().hex;work.mkdir(parents=True)
image=work/'initialized.img';subprocess.run([sys.executable,str(root/'src/tools/init-dev-disk.py'),str(a.build.resolve()/'cloudberry.iso'),str(image)],check=True)
reports=[];success=False
try:
    for phase in ('create','verify'):
        command=[sys.executable,str(root/'src/tests/console-boot.py'),str(a.build),'--persistence-phase',phase,'--disk-source',str(image),'--keep-artifacts','--accel',a.accel,'--cpus',str(a.cpus)]
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);(work/(phase+'.log')).write_text(result.stdout);print(result.stdout,end='',flush=True)
        match=re.search(r'report (.+/result.json)',result.stdout)
        if not match:raise RuntimeError('missing console report')
        report=pathlib.Path(match.group(1));reports.append(str(report))
        if result.returncode:raise RuntimeError('guest '+phase+' failed')
        if phase=='create':image=report.parent/'boot.img'
    success=True
finally:
    (work/'result.json').write_text(json.dumps(dict(passed=success,reports=reports,cpus=a.cpus,accelerator=a.accel),indent=2)+'\n')
    if success:
        for report in reports:subprocess.run([sys.executable,str(root/'src/tools/clean-tests.py'),'--root',str(pathlib.Path(report).parent)],check=True)
        subprocess.run([sys.executable,str(root/'src/tools/clean-tests.py'),'--root',str(work)],check=True)
print('PASS persistent sessions: accounts/passwords/homes/metadata/data survive two boots; nested terminal and monitor restore keyboard focus')

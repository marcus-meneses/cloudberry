#!/usr/bin/env python3
"""Install, update and reboot disposable media; never attach developer disks."""
import argparse,json,pathlib,re,subprocess,sys,uuid
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=pathlib.Path);p.add_argument('--cpus',type=int,default=4);p.add_argument('--created-session',type=pathlib.Path,help='reuse a passed isolated installer create run with retained media');p.add_argument('--keep-artifacts',action='store_true');a=p.parse_args()
build=a.build.resolve();work=build/'tests/installer-persistence'/uuid.uuid4().hex;work.mkdir(parents=True);result={'status':'running','cpus':a.cpus}
def run(command,name):
    with (work/(name+'.log')).open('w') as log:
        completed=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
    if completed.returncode:raise RuntimeError(name+' failed; see '+str(work/(name+'.log')))
    return (work/(name+'.log')).read_text()
def console(phase,extra):
    output=run([sys.executable,str(root/'src/tests/console-boot.py'),str(build),'--installer-phase',phase,'--cpus',str(a.cpus),'--keep-artifacts',*extra],phase)
    match=re.search(r'PASSED: console, report (.*)/result.json',output)
    if not match:raise RuntimeError('missing console acceptance report')
    return pathlib.Path(match[1])
try:
    created=a.created_session.resolve() if a.created_session else console('create',['--fixtures'])
    if not created.is_relative_to(root/'bin/tests') or json.loads((created/'result.json').read_text())['status']!='passed' or not (created/'boot.img').is_file():raise ValueError('created-session must be passed isolated installer media')
    result['create_session']=str(created)
    updated=work/'updated.img'
    run([sys.executable,str(root/'src/tools/update-dev-disk.py'),str(build/'cloudberry.iso'),str(created/'boot.img'),str(updated)],'offline-update')
    verified=console('verify',['--disk-source',str(updated)]);result['verify_session']=str(verified);result['status']='passed'
    if not a.keep_artifacts:
        for directory in (created,verified,work):run([sys.executable,str(root/'src/tools/clean-tests.py'),'--root',str(directory)],'cleanup-'+directory.name)
except Exception as error:
    result.update(status='failed',error=str(error))
finally:(work/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['status'].upper()+': persistent installations; report '+str(work/'result.json'))
if result['status']!='passed':raise SystemExit(result['error'])

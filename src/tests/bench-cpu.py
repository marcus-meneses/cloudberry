#!/usr/bin/env python3
"""Serial, disposable before/after CPU comparison; no developer disks attached."""
import argparse,datetime,fcntl,hashlib,json,os,pathlib,platform,re,statistics,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
def run(command,log):
    print('Log:',log.relative_to(ROOT),flush=True)
    with log.open('w') as out:r=subprocess.run(command,cwd=ROOT,stdout=out,stderr=subprocess.STDOUT)
    if r.returncode:raise RuntimeError('command failed: '+str(log))
    return log.read_text()
def capture(command):return subprocess.check_output(command,cwd=ROOT,text=True).strip()
def host_processor():
    fields={}
    for line in pathlib.Path('/proc/cpuinfo').read_text().split('\n\n',1)[0].splitlines():
        if ':' not in line:continue
        key,value=line.split(':',1)
        if key.strip() in ('vendor_id','model name','cpu family','model','stepping','microcode','flags'):fields[key.strip()]=value.strip()
    return fields
def fingerprint():
    h=hashlib.sha256()
    for name in sorted(set(capture(['git','ls-files','-co','--exclude-standard','src']).splitlines())):
        p=ROOT/name
        if name.startswith('src/tests/') or name.endswith('.md'):continue
        if p.is_file():h.update(name.encode()+b'\0'+p.read_bytes())
    return h.hexdigest()
def aggregate(reports):
    phases=('idle','quiet_monitor','terminal','live_monitor')
    host={phase:statistics.median(r['host'][phase]['host_cpu_percent'] for r in reports) for phase in phases}
    quiet={name:statistics.median(r['summary']['quiet'][name]['cpu_percent_median'] for r in reports if name in r['summary']['quiet']) for name in sorted({n for r in reports for n in r['summary']['quiet']})}
    return dict(host_cpu_percent_median=host,quiet_guest_cpu_percent_median=quiet)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference-ref',default='d11e0b2');p.add_argument('--runs',type=int,default=3);p.add_argument('--cpus',type=int,default=4);p.add_argument('--jobs',type=int,default=4);p.add_argument('--accel',choices=['kvm','tcg'],default='kvm');a=p.parse_args()
    if min(a.runs,a.cpus,a.jobs)<1:p.error('runs, cpus and jobs must be positive')
    if a.accel=='kvm' and not os.access('/dev/kvm',os.R_OK|os.W_OK):p.error('/dev/kvm is unavailable or inaccessible; run on your KVM host (TCG is only a separate diagnostic)')
    base=ROOT/'bin/tests/cpu-comparison';base.mkdir(parents=True,exist_ok=True)
    with (base/'run.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        work=base/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ');work.mkdir()
        report=dict(version=1,status='running',topology=dict(sockets=1,cores=a.cpus,threads=1),accelerator=a.accel,cpus=a.cpus,runs=a.runs,reference_ref=capture(['git','rev-parse',a.reference_ref+'^{commit}']),candidate_ref=capture(['git','rev-parse','HEAD']),candidate_source_sha256=fingerprint(),host=dict(uname=platform.uname()._asdict(),processor=host_processor(),qemu=capture(['qemu-system-x86_64','--version']),clock_ticks=os.sysconf('SC_CLK_TCK')),samples=[])
        path=work/'result.json'
        def save():path.write_text(json.dumps(report,indent=2)+'\n')
        save();print('Session:',work.relative_to(ROOT),flush=True)
        try:
            source=work/'reference';source.mkdir();archive=work/'reference.tar'
            with archive.open('wb') as out:subprocess.run(['git','archive',report['reference_ref'],'src'],cwd=ROOT,stdout=out,check=True)
            subprocess.run(['tar','-xf',str(archive),'-C',str(source)],check=True);archive.unlink()
            reference=work/'reference-build'
            run(['make','-C',str(source/'src'),'ROOT='+str(ROOT),'TOOLCHAIN_DIR='+str(ROOT/'.toolchain'),'BUILD='+str(reference),'-j'+str(a.jobs),'build'],work/'reference-build.log')
            run(['make','-C','src','-j'+str(a.jobs),'build'],work/'candidate-build.log')
            paths={'reference':reference,'candidate':ROOT/'bin'};values={k:[] for k in paths}
            # Alternate order to expose warmup/order effects; VMs never overlap.
            for index in range(a.runs):
                for label in (('reference','candidate') if index%2==0 else ('candidate','reference')):
                    output=run([sys.executable,'src/tests/console-boot.py',str(paths[label]),'--cpu-only','--diagnostics','--cpus',str(a.cpus),'--accel',a.accel],work/(label+'-'+str(index+1)+'.log'))
                    match=re.search(r'PASSED: console, report (.*)/result.json',output)
                    if not match:raise RuntimeError('missing console acceptance report')
                    console=pathlib.Path(match[1]);sample=json.loads((console/'cpu.json').read_text());values[label].append(sample)
                    report['samples'].append(dict(label=label,run=index+1,console=str(console.relative_to(ROOT)),metrics=sample));save()
            clocks={(sample['clock_start']['clock']['source'],sample['clock_start']['clock']['smp_tsc']) for group in values.values() for sample in group}
            if len(clocks)!=1:raise RuntimeError('clock sources differ across boots; inspect saved metrics before comparing CPU percentages')
            report['clock_mode']=list(next(iter(clocks)))
            report['candidate_source_sha256_after']=fingerprint()
            if report['candidate_source_sha256_after']!=report['candidate_source_sha256']:raise RuntimeError('candidate source changed during comparison')
            report['summary']={k:aggregate(v) for k,v in values.items()}
            report['host_delta_percentage_points']={phase:report['summary']['candidate']['host_cpu_percent_median'][phase]-report['summary']['reference']['host_cpu_percent_median'][phase] for phase in report['summary']['reference']['host_cpu_percent_median']}
            report['status']='passed'
            # Keep the compiled reference cache and reports; remove only disposable media.
            run([sys.executable,'src/tools/clean-tests.py','--root',str(work)],work/'cleanup.log')
        except Exception as error:report.update(status='failed',error=str(error))
        finally:save()
        print(report['status'].upper()+': CPU comparison, report '+str(path),flush=True)
        if report['status']!='passed':raise SystemExit(report['error'])
if __name__=='__main__':main()

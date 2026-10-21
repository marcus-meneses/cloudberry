#!/usr/bin/env python3
"""Writable ext4 tests on generated regular files; no host mounts or real disks."""
import pathlib, subprocess, sys, uuid, shutil, re, json, atexit
root=pathlib.Path(__file__).resolve().parents[2]
work=root/'bin/tests/isolated/tests/ext4-write';work.mkdir(parents=True,exist_ok=True)
exe=work.parent/'src-ext4-write'
run_id=uuid.uuid4().hex
state_report={'run_id':run_id,'status':'running'}
report_path=work/'report.json'
def save_report():report_path.write_text(json.dumps(state_report,indent=2)+'\n')
def on_exit():
    if state_report['status']=='running':state_report['status']='failed';save_report()
save_report();atexit.register(on_exit)
source=work/'root';(source/'system/kmodules/drivers').mkdir(parents=True,exist_ok=True)
features=('none,has_journal,ext_attr,resize_inode,dir_index,filetype,extent,64bit,'
          'flex_bg,sparse_super,large_file,huge_file,dir_nlink,extra_isize,metadata_csum')
guid='9beaeafd-7835-446a-8258-00331878d4f3'
fs=work/'filesystem.img';base=work/'base.img'
with fs.open('wb') as f:f.truncate(32*1024**2)
subprocess.run(['mke2fs','-q','-t','ext4','-F','-b','4096','-I','256','-O',features,'-E','lazy_itable_init=0,lazy_journal_init=0','-d',str(source),str(fs)],check=True)
with base.open('wb') as f:f.truncate(40*1024**2)
subprocess.run(['sgdisk','--clear','--new=1:2048:67583','--typecode=1:8300',f'--partition-guid=1:{guid}',str(base)],check=True,stdout=subprocess.DEVNULL)
with base.open('r+b') as out,fs.open('rb') as data:
    out.seek(1024**2);shutil.copyfileobj(data,out)
cached=work/'read-cache.img';shutil.copyfile(base,cached)
subprocess.run([str(exe),str(cached),'read-cache'],check=True)
disconnected=work/'disconnected.img';shutil.copyfile(base,disconnected)
subprocess.run([str(exe),str(disconnected),'disconnect'],check=True)
normal=work/'normal.img';shutil.copyfile(base,normal)
subprocess.run([str(exe),str(normal),'normal'],check=True)
with normal.open('rb') as source,fs.open('wb') as out:
    source.seek(1024**2);left=32*1024**2
    while left:
        data=source.read(min(left,1024**2));out.write(data);left-=len(data)
subprocess.run(['e2fsck','-fn',str(fs)],check=True)
print('PASS normal journaled mutations and independent e2fsck')
# Trace journal boundaries, then repeat the operation with abrupt process exit
# immediately after selected stable block writes. Each child starts fresh state.
trace=work/'trace.img';shutil.copyfile(base,trace)
result=subprocess.run([str(exe),str(trace),'action'],check=True,text=True,capture_output=True)
(work/'trace.log').write_text(result.stdout)
records=[tuple(map(int,m)) for m in re.findall(r'^WRITE (\d+) (\d+) (\d+)$',result.stdout,re.M)]
commits=[sequence for sequence,_,kind in records if kind==2]
assert len(commits)>=2,records
cuts={1}
for sequence in commits:cuts.update((max(1,sequence-1),sequence,sequence+1,sequence+3))
for sequence,_,kind in records:
    if kind==1:cuts.add(sequence)
completed=[]
for point in sorted(cuts):
    disk=work/f'cut-{point}.img';shutil.copyfile(base,disk)
    child=subprocess.run([str(exe),str(disk),'action',str(point)],text=True,capture_output=True)
    assert child.returncode==77,(point,child.returncode,child.stdout,child.stderr)
    recovered=subprocess.run([str(exe),str(disk),'recover'],text=True,capture_output=True)
    (work/f'cut-{point}.log').write_text(child.stdout+child.stderr+'\nRECOVERY\n'+recovered.stdout+recovered.stderr)
    assert recovered.returncode==0,(point,recovered.stdout,recovered.stderr)
    if 'ACK WRITE' in child.stdout:assert 'RECOVERED 8192' in recovered.stdout
    elif 'ACK CREATE' in child.stdout:assert 'RECOVERED ABSENT' not in recovered.stdout
    with disk.open('rb') as source,fs.open('wb') as out:
        source.seek(1024**2);left=32*1024**2
        while left:
            data=source.read(min(left,1024**2));out.write(data);left-=len(data)
    checked=subprocess.run(['e2fsck','-fn',str(fs)],text=True,capture_output=True)
    assert checked.returncode==0,(point,checked.stdout,checked.stderr)
    completed.append(point);disk.unlink()
state_report.update(cuts=completed,commit_flushes=commits,model='whole block writes followed by flush; abrupt process exit; no torn-sector/physical qualification');save_report()
print('PASS journal crash recovery:',len(completed),'commit/checkpoint cuts; acknowledged contents preserved and e2fsck clean')
# Repeat crash checks for mutation of a previously acknowledged file.
extra_completed={}
for operation in ('append','truncate','remove','rename'):
    reference=work/f'{operation}-trace.img';shutil.copyfile(trace,reference)
    result=subprocess.run([str(exe),str(reference),operation],check=True,text=True,capture_output=True)
    (work/f'{operation}-trace.log').write_text(result.stdout)
    records=[tuple(map(int,m)) for m in re.findall(r'^WRITE (\d+) (\d+) (\d+)$',result.stdout,re.M)]
    last=max(row[0] for row in records);cuts={1,last}
    for sequence,_,kind in records:
        if kind==2:cuts.update((sequence-1,sequence,sequence+1))
        elif kind==1:cuts.add(sequence)
    passed=[]
    for point in sorted(c for c in cuts if 0<c<=last):
        disk=work/f'{operation}-cut-{point}.img';shutil.copyfile(trace,disk)
        child=subprocess.run([str(exe),str(disk),operation,str(point)],text=True,capture_output=True)
        assert child.returncode==77,(operation,point,child.returncode,child.stdout,child.stderr)
        recovered=subprocess.run([str(exe),str(disk),'recover-rename' if operation=='rename' else 'recover'],text=True,capture_output=True)
        (work/f'{operation}-cut-{point}.log').write_text(child.stdout+child.stderr+'\nRECOVERY\n'+recovered.stdout+recovered.stderr)
        assert recovered.returncode==0,(operation,point,recovered.stdout,recovered.stderr)
        if operation=='rename':assert 'RECOVERED SOURCE' in recovered.stdout or 'RECOVERED DESTINATION' in recovered.stdout
        elif operation=='remove':assert 'RECOVERED ABSENT' in recovered.stdout or 'RECOVERED 8192' in recovered.stdout
        elif operation=='truncate':assert 'RECOVERED 8192' in recovered.stdout or 'RECOVERED 137' in recovered.stdout
        else:assert 'RECOVERED 8192' in recovered.stdout or 'RECOVERED 16384' in recovered.stdout
        with disk.open('rb') as source,fs.open('wb') as out:
            source.seek(1024**2);left=32*1024**2
            while left:
                data=source.read(min(left,1024**2));out.write(data);left-=len(data)
        checked=subprocess.run(['e2fsck','-fn',str(fs)],text=True,capture_output=True)
        assert checked.returncode==0,(operation,point,checked.stdout,checked.stderr)
        passed.append(point);disk.unlink()
    extra_completed[operation]=passed
state_report['existing_file_cuts']=extra_completed;save_report()
print('PASS existing-file journal crash recovery:',{key:len(value) for key,value in extra_completed.items()})

failure_cases=[]
for failure in ('fail-write','fail-flush'):
    for point in (1,3,6):
        disk=work/f'{failure}-{point}.img';shutil.copyfile(base,disk)
        child=subprocess.run([str(exe),str(disk),failure,str(point)],capture_output=True,text=True)
        assert child.returncode==78,(failure,point,child.returncode,child.stdout,child.stderr)
        recovered=subprocess.run([str(exe),str(disk),'recover'],capture_output=True,text=True)
        assert recovered.returncode==0,(failure,point,recovered.stdout,recovered.stderr)
        with disk.open('rb') as source,fs.open('wb') as out:
            source.seek(1024**2);left=32*1024**2
            while left:
                data=source.read(min(left,1024**2));out.write(data);left-=len(data)
        checked=subprocess.run(['e2fsck','-fn',str(fs)],capture_output=True,text=True)
        assert checked.returncode==0,(failure,point,checked.stdout,checked.stderr)
        failure_cases.append([failure,point]);disk.unlink()
state_report['failed_io_cases']=failure_cases;state_report['status']='passed';save_report()
print('PASS failed writes/barriers:',len(failure_cases),'poison/ownership/recovery cases')

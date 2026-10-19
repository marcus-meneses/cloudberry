"""GDB-only, read-only stalled-console snapshot. Source after remote attach."""
import gdb,os,pathlib,struct
build=pathlib.Path(os.environ['CB_DIAG_BUILD'])
inferior=gdb.selected_inferior()
def memory(address,size):return bytes(inferior.read_memory(address,size))
def value(expression):return gdb.parse_and_eval(expression)
def show(command):
    try:gdb.write(gdb.execute(command,to_string=True))
    except Exception as error:gdb.write('diagnostic: '+str(error)+'\n')
def linked(expression,fields,maximum=80):
    try:
        p=value(expression);seen=set()
        while int(p) and len(seen)<maximum and int(p) not in seen:
            seen.add(int(p));node=p.dereference();gdb.write(hex(int(p))+' ')
            for field in fields:
                try:gdb.write(field+'='+str(node[field])+' ')
                except Exception as error:gdb.write(str(error))
            gdb.write('\n');p=node['next']
    except Exception as error:gdb.write('diagnostic: '+str(error)+'\n')
header=memory(0xffffffffa1000000,128)
owners=struct.unpack_from('<Q',header,88)[0];count=struct.unpack_from('<I',header,100)[0];stride=struct.unpack_from('<I',header,108)[0]
for i in range(min(count,128)):
    record=memory(owners+i*stride,stride);name=record[:32].split(b'\0')[0].decode();image=struct.unpack_from('<Q',record,40)[0]
    file=build/'kmodules'/(name+'.elf')
    if name=='bootstrap' and os.environ.get('CB_DIAG_BOOTSTRAP'):file=pathlib.Path(os.environ['CB_DIAG_BOOTSTRAP'])
    if file.exists() and image:
        show('add-symbol-file '+str(file)+' -o '+hex(image))
for symbol in gdb.lookup_static_symbols('state'):
    if symbol.symtab:
        source=pathlib.Path(symbol.symtab.filename).name.removesuffix('.c').replace('-','_')
        gdb.set_convenience_variable('cb_'+source,symbol.value())
show("p 'cpu.c'::watchdog_failure");show("p 'cpu.c'::lock_owner");show("p 'cpu.c'::config");
show('p hpet_zero_probe')
try:
    probe=value('hpet_zero_probe')
    if int(probe['ready'])==2:
        address=int(probe['address']);table=int(probe['cr3'])&0x000ffffffffff000
        hhdm=int(value("'modules.c'::hub_pages->hhdm"))
        for shift in (39,30,21,12):
            entry=struct.unpack('<Q',memory(hhdm+table+((address>>shift)&511)*8,8))[0]
            gdb.write('HPET page walk shift='+str(shift)+' entry='+hex(entry)+'\n')
            if not entry&1:break
            if shift==12 or entry&128:
                mask=(1<<shift)-1;physical=(entry&0x000ffffffffff000&~mask)|(address&mask)
                gdb.write('HPET guest translation='+hex(physical)+'\n');break
            table=entry&0x000ffffffffff000
except gdb.error:pass
for i in range(int(value("'cpu.c'::config.count"))):
    c=value("'cpu.c'::config.records")[i]
    gdb.write('LOCK CPU '+str(i)+' depth='+str(c['depth'])+' locks='+str(c['lock_depth'])+' caller='+str(c['lock_caller'])+' method='+str(c['lock_method'])+'\n')

show('monitor xp /4wx 0xfed00000');show('monitor xp /2wx 0xfed00010');show('monitor xp /2wx 0xfed000f0');show('x/2wx 0xffffc000fed00010');show('x/2wx 0xffffc000fed000f0');
show('info threads');show('thread apply all bt 12');show('thread apply all info registers rip rsp eflags cr3');show('p *f');show('x/32gx f->rsp');show('info symbol f->rip');show('x/12i f->rip-24')
show("p 'bootstrap-apps.c'::apps")
linked("'bootstrap-apps.c'::apps.first",('process','state','error','bundle','ready'))
for source in ('threads.c','io.c','handles.c','process.c','timer.c','event.c','xhci.c','usb.c','irq.c','clock.c'):
    gdb.write('\nSTATE '+source+'\n');show("p $cb_"+source.removesuffix(".c"))
linked("$cb_threads.head",('info','wake_ns','wake_pending','joining','context'))
linked("$cb_process.head",('info','entry','saved_rsp'))
linked("$cb_handles.policies",('pid','receiver','standard'))
linked("$cb_handles.requests",('pid','provider','waiter','cap','id','dispatched','done','completed','failure'))
linked("$cb_handles.publications",('name','pid','completed','last_operation','dead'))
linked("$cb_handles.channels",('pid','token','ready','sequence'))
linked("$cb_io.first",('pid','thread','stop','waiting','ack','active','transfer','report'))
try:
    cpus=value("$cb_threads.cpus");count=int(value("$cb_threads.count"))
    for i in range(count):gdb.write('CPU '+str(i)+' '+str(cpus[i])+'\n')
    ctrl=value("$cb_xhci.head")
    if int(ctrl):
        c=ctrl.dereference();gdb.write('XHCI '+str(c)+'\n')
        slots=c['slots']
        for i in range(1,int(c['record']['info']['slots'])+1):
            s=slots[i]
            if not int(s['token']):continue
            gdb.write('SLOT '+str(i)+' '+str(s)+'\n')
            for j in range(32):
                ep=s['ep'][j]
                if int(ep):gdb.write('ENDPOINT '+str(j)+' '+str(ep.dereference())+'\n')
except Exception as error:gdb.write('diagnostic: '+str(error)+'\n')

# Small machine-readable companion for successful performance runs as well as
# failures. Read guest memory only; never call a guest surface from GDB.
if os.environ.get('CB_DIAG_REPORT'):
    import json
    report={}
    try:
        clock=value('$cb_clock');info=clock['info']
        report['clock']={name:int(info[name]) for name in ('source','flags','tsc_hz','calibrations')}
        report['clock']['smp_tsc']=int(clock['smp_tsc'])
        report['clock']['rejection_reason']=int(clock['tsc_reason'])
        report['profiling_hz']=int(value("'cpu.c'::profile_hz"))
        diagnostic=value('$cb_smp')['clock_status']
        report['smp_clock']={name:int(diagnostic[name]) for name in ('reason','cpu','source','hz','samples','calibrations')}
        config=value("'cpu.c'::config");report['cpus']=[]
        for i in range(int(config['count'])):
            cpu=config['records'][i];metrics=cpu['metrics']
            fields=('acquisitions','contended','max_wait_ns','max_hold_ns','max_irq_off_ns','tsc_ns_samples','hpet_ns_samples','unavailable_ns_samples','sampled_out_ns')
            entry=dict(cpu=i,timer_interrupts=int(cpu['timer_interrupts']),metrics={name:int(metrics[name]) for name in fields})
            report['cpus'].append(entry)
    except Exception as error:report['error']=str(error)
    pathlib.Path(os.environ['CB_DIAG_REPORT']).write_text(json.dumps(report,indent=2)+'\n')

"""Framed phase records and pixel expectations for paired-provider checks."""
import re

def complete_log(text):
    # Debugcon files can be read between bytes of a single logged number.
    # Never interpret a trailing prefix as a complete phase or PASS record.
    return text[:text.rfind('\n')+1]

def phase_records(text):
    return [tuple(map(int,m.groups())) for m in re.finditer(r'^GPU_PAIR PHASE (\d+) width=(\d+) height=(\d+) x=(\d+) y=(\d+)\n',complete_log(text),re.MULTILINE)]

def inspect(data,phase,state):
    head,dimensions,maximum,pixels=data.split(b'\n',3)
    w,h=map(int,dimensions.split())
    if head!=b'P6' or maximum!=b'255' or w!=state['width'] or h!=state['height'] or len(pixels)!=w*h*3:raise ValueError('invalid paired screen geometry/format')
    def pixel(x,y):
        if not 0<=x<w or not 0<=y<h:raise ValueError('sample outside screen')
        return int.from_bytes(pixels[(y*w+x)*3:(y*w+x)*3+3],'big')
    if phase==10:
        samples=[(8,8,0x304050),(32,24,0x55aa11),(110,70,0x55aa11),(112,72,0x304050)]
        result=[dict(x=x,y=y,expected=rgb,actual=pixel(x,y)) for x,y,rgb in samples]
        white=sum(pixel(x,y)>0x505050 and (pixel(x,y)>>16)==((pixel(x,y)>>8)&255)==(pixel(x,y)&255) for y in range(96,120) for x in range(16,112))
        return dict(state='matched' if all(s['actual']==s['expected'] for s in result) and white>=10 else 'mismatch',width=w,height=h,samples=result,white=white,orange=0)
    samples=[(8,8,0x102030)]
    if phase==1:samples += [(20,20,0xff0000),(48,20,0x00ff00),(20,48,0x0000ff),(48,48,0xffffff),(75,20,0x102030)]
    elif phase!=8:samples += [(44,28,0xff0000),(72,28,0x00ff00),(44,56,0x0000ff),(72,56,0xffffff),(20,20,0x102030)]
    else:samples += [(44,28,0x102030),(72,28,0x102030),(100,52,0x102030)]
    x,y=state['x'],state['y']
    if phase in (4,8):samples += [(x,y,0x101010),(x+1,y+2,0xfafafa),(x+10,y+2,0x102030)]
    if phase==5:samples += [(x,y,0x102030),(x+1,y+2,0x102030)]
    result=[dict(x=x,y=y,expected=rgb,actual=pixel(x,y)) for x,y,rgb in samples]
    white=orange=0
    if phase!=8:
        top=16 if phase==1 else 50
        for y in range(top,top+24):
            for x in range(96,288):
                p=pixel(x,y);r,g,b=p>>16,(p>>8)&255,p&255
                white+=r>50 and r==g==b;orange+=r>g>b and r>50
    matched=all(s['expected']==s['actual'] for s in result) and (phase==8 or white>=10 and orange>=5)
    return dict(state='matched' if matched else 'empty-ram-surface' if not any(pixels) else 'mismatch',width=w,height=h,samples=result,white=white,orange=orange)


def validate_capture(capture,phase,required=False):
    # Restoration must always be visible; GPU-resource readback cannot prove it.
    if (required or phase==10) and capture.get('state')!='matched':
        raise ValueError('Scanout phase '+str(phase)+' mismatched: '+capture.get('state','missing'))

def profile_records(text):
    """Accept complete profiling windows, never partial debugcon records."""
    import json
    text=complete_log(text)
    if 'GPU_PROFILE PASS\n' not in text:raise ValueError('missing profiling completion')
    rows=[json.loads(line.removeprefix('GPU_PROFILE ')) for line in text.splitlines() if line.startswith('GPU_PROFILE {')]
    windows=[r for r in rows if r['kind']=='window'];frames=[r for r in rows if r['kind']=='frame']
    if len(windows)!=1 or windows[0]['samples']!=32 or len(frames)!=32 or {r['sample'] for r in frames}!=set(range(32)):raise ValueError('incomplete profiling samples')
    if any(r['ns']<=0 for r in frames) or sum(r['ns'] for r in frames)!=windows[0]['total_ns'] or max(r['ns'] for r in frames)!=windows[0]['max_ns']:raise ValueError('inconsistent frame timings')
    if windows[0]['clock_source'] not in (1,2):raise ValueError('invalid profiling clock')
    if not any(r['kind']=='gpu_fence' and r['calls'] for r in rows) or not any(r['kind']=='broker_invoke' and r['count'] for r in rows):raise ValueError('missing GPU/broker measurements')
    if any((r['kind']=='gpu_fence' and r['id']==0x206 and r['calls']) or (r['kind']=='gpu_io' and r['id']==13 and r['calls']) for r in rows):raise ValueError('readback contaminated the measurement window')
    return rows

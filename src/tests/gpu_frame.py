"""Independent evidence rules for virtio RAM scanout and VirGL resource tests."""
import re
SAMPLES=((10,10,(255,0,0)),(310,10,(0,255,0)),(10,190,(0,0,255)),(310,190,(255,255,255)))
def inspect_ppm(data):
    try:
        header,dimensions,maximum,pixels=data.split(b'\n',3)
        width,height=map(int,dimensions.split())
    except (ValueError,TypeError):return dict(state='invalid',reason='invalid PPM header')
    if header!=b'P6' or maximum!=b'255' or width!=320 or height!=200 or len(pixels)!=320*200*3:
        return dict(state='invalid',reason='expected P6 320x200 RGB8',width=width,height=height,bytes=len(pixels))
    samples=[dict(x=x,y=y,expected=list(expected),actual=list(pixels[(y*width+x)*3:(y*width+x)*3+3])) for x,y,expected in SAMPLES]
    matches=all(sample['actual']==sample['expected'] for sample in samples)
    return dict(state='matched' if matches else 'empty-ram-surface' if not any(pixels) else 'mismatch',width=width,height=height,samples=samples)
def validate_gpu_result(text,gpu,capture):
    if 'GPU_TEST FAIL' in text:raise ValueError('guest GPU test failed')
    match=re.search(r'GPU_TEST PASS commands=(\d+) interrupts=(\d+) virgl=(\d+)',text)
    if not match:raise ValueError('guest completion record missing')
    commands,interrupts,virgl=map(int,match.groups())
    if commands<8 or interrupts<1:raise ValueError('insufficient fenced commands or IRQ delivery')
    if gpu=='virgl':
        if not virgl or 'GPU_TEST READBACK PASS pixels=64000' not in text or 'GPU_TEST TEXTURE READBACK PASS pixels=1024' not in text or 'GPU_TEST VIRGL CONTEXT/SUBMIT OK' not in text:
            raise ValueError('VirGL requires resource and texture readback, context and submit completion')
        if capture['state'] not in ('matched','empty-ram-surface','unavailable'):
            raise ValueError('nonempty or invalid scanout capture mismatch: '+str(capture))
    elif capture['state']!='matched':
        raise ValueError('2D screen pixels did not match: '+str(capture))
    return dict(commands=commands,interrupts=interrupts,virgl=virgl,resource_readback_verified=gpu=='virgl',readback_pixels=65024 if gpu=='virgl' else 0,
                screen_capture_verified=capture['state']=='matched',pixel_evidence='gpu-resource-readback' if gpu=='virgl' else 'qmp-screen-capture',
                presentation='fenced scanout/flush; visible GL output unverified' if capture['state']!='matched' else 'screen capture matched')

import unittest
from gpu_frame import inspect_ppm,validate_gpu_result
MATCH='GPU_TEST PASS commands=24 interrupts=22 virgl=1'
READBACK='GPU_TEST READBACK PASS pixels=64000\nGPU_TEST TEXTURE READBACK PASS pixels=1024\nGPU_TEST VIRGL CONTEXT/SUBMIT OK\n'
def ppm(black=False,wrong=False):
    pixels=bytearray()
    for y in range(200):
        for x in range(320):pixels+=bytes((0,0,0) if black else (1,2,3) if wrong else ((255,0,0) if x<160 else (0,255,0)) if y<100 else ((0,0,255) if x<160 else (255,255,255)))
    return b'P6\n320 200\n255\n'+pixels
class Evidence(unittest.TestCase):
    def test_2d_requires_screen_pixels(self):
        matched=inspect_ppm(ppm());result=validate_gpu_result('GPU_TEST PASS commands=8 interrupts=8 virgl=0','virtio',matched);self.assertTrue(result['screen_capture_verified'])
        for data in (ppm(black=True),ppm(wrong=True),b'bad'):
            with self.assertRaises(ValueError):validate_gpu_result(MATCH+READBACK,'virtio',inspect_ppm(data))
    def test_blank_gl_surface_requires_real_readback(self):
        capture=inspect_ppm(ppm(black=True));self.assertEqual(capture['state'],'empty-ram-surface')
        for evidence in ('','GPU_TEST READBACK PASS pixels=64000\n','GPU_TEST VIRGL CONTEXT/SUBMIT OK\n'):
            with self.assertRaises(ValueError):validate_gpu_result(MATCH+evidence,'virgl',capture)
        result=validate_gpu_result(MATCH+READBACK,'virgl',capture);self.assertTrue(result['resource_readback_verified']);self.assertFalse(result['screen_capture_verified']);self.assertEqual(result['readback_pixels'],65024)
    def test_real_gl_capture_is_additional_evidence(self):
        result=validate_gpu_result(MATCH+READBACK,'virgl',inspect_ppm(ppm()));self.assertTrue(result['screen_capture_verified'])
    def test_other_mismatches_do_not_get_ignored(self):
        for capture in (inspect_ppm(ppm(wrong=True)),dict(state='invalid'),dict(state='not-observed')):
            with self.assertRaises(ValueError):validate_gpu_result(MATCH+READBACK,'virgl',capture)
        for text in ('',MATCH+READBACK+'GPU_TEST FAIL',MATCH.replace('interrupts=22','interrupts=0')+READBACK,MATCH.replace('virgl=1','virgl=0')+READBACK):
            with self.assertRaises(ValueError):validate_gpu_result(text,'virgl',dict(state='unavailable'))
if __name__=='__main__':unittest.main()

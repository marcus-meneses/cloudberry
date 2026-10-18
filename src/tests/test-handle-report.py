import unittest
import importlib.util
import pathlib
import hashlib
from handle_report import parse_handles, native_timing_error


def fixture():
    lines = ['MEASURE handles clock 1 2000000000 1']
    lines += [f'PROFILE handles clock {phase} 1 0 3 2000000000 1 0 0 80 1 1 0 0 0 1' for phase in range(3)]
    lines += ['MEASURE handles samples '+str(i)+' '+' '.join(str(100+i+j) for j in range(48)) for i in range(8)]
    lines += ['MEASURE handles lock 0 100 0 0 1 1 2 0 0']
    lines += ['ATTR handles 0 '+str(i)+' 1 1 0 1 0 1234 1 1 0 handles.gate' for i in range(3)]
    return '\n'.join(lines+['PASS handle benchmark'])


class Reports(unittest.TestCase):
    def test_host_identity(self):
        path=pathlib.Path(__file__).with_name('bench-handles-kvm.py')
        spec=importlib.util.spec_from_file_location('handle_collector',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        a={'cpu':'vendor_id: AuthenticAMD\nmodel name: test\nflags: avx\nmicrocode: 1\ncpu MHz: 1000'}
        b={'cpu':a['cpu'].replace('1000','2500')}
        self.assertEqual(module.comparable_host(a),module.comparable_host(b))
        self.assertNotEqual(module.comparable_host(a),module.comparable_host({'cpu':a['cpu'].replace('AuthenticAMD','GenuineIntel')}))
        self.assertEqual(module.digest(path),hashlib.sha256(path.read_bytes()).hexdigest())

    def test_native_rejection_reasons(self):
        report=parse_handles(fixture(),1);self.assertIsNone(native_timing_error(report))
        report['clock']['source']=2;report['clock_diagnostics']['end']['source']=2;report['clock_diagnostics']['begin']['qualification_reason']=14
        self.assertIn('SMP qualification reason=uncertainty',native_timing_error(report));self.assertIn('Correctness passed',native_timing_error(report))
        report['clock_diagnostics']['end']['reason']=8;self.assertIn('runtime reason=regression',native_timing_error(report))
        report=parse_handles(fixture(),1);report['locks'][0]['unavailable_samples']=2;self.assertIn('CPU0=2',native_timing_error(report))
        report=parse_handles(fixture(),1);report['clock_diagnostics']['end']['calibrations']=2;self.assertIn('calibration changed',native_timing_error(report))

    def test_profile_framing_and_sign(self):
        row='PROFILE handles exchange 0 0 100 101 102 2000000000 1 18446744073709551615 2'
        result=parse_handles(fixture()+'\n'+row,1);self.assertEqual(result['clock_profile']['exchange'][0]['offset_ns'],-1)
        with self.assertRaises(ValueError):parse_handles(fixture()+'\n'+row+'\n'+row,1)
        with self.assertRaises(ValueError):parse_handles(fixture().replace('PROFILE handles clock 2','PROFILE handles clock 3'),1)
        with self.assertRaises(ValueError):parse_handles(fixture().replace('PROFILE handles clock 2','IGNORED handles clock 2'),1)

    def test_complete(self):
        result = parse_handles(fixture(), 1)
        self.assertEqual(result['measurements']['clock_bracket']['count'], 48)
        self.assertEqual(result['attribution'][0]['caller'], 1234)

    def test_reject_incomplete(self):
        with self.assertRaises(ValueError):parse_handles(fixture().replace('PASS handle benchmark',''),1)
        with self.assertRaises(ValueError):parse_handles(fixture(),4)

    def test_reject_duplicate(self):
        with self.assertRaises(ValueError):parse_handles(fixture()+'\nMEASURE handles clock 1 2000000000 1',1)
        with self.assertRaises(ValueError):parse_handles(fixture()+'\nATTR handles 0 0 1 1 0 1 0 1234 1 1 0 handles.gate',1)

    def test_reject_bad_samples(self):
        with self.assertRaises(ValueError):parse_handles(fixture().replace('100 101','-1 101'),1)
        with self.assertRaises(ValueError):parse_handles(fixture().replace('100 101','100'),1)


if __name__=='__main__':unittest.main()

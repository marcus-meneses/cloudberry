import unittest
import importlib.util
import pathlib
import hashlib
from handle_report import parse_handles


def fixture():
    lines = ['MEASURE handles clock 1 2000000000 1']
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

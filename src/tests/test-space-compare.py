"""Comparison and runner checks use synthetic native reports, never a VM."""
import copy
import importlib.util
import contextlib
import io
import tarfile
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from space_compare import CASES, compare, guest_fingerprint, markdown, validate_native, load_native_reference
from space_report import distribution

ROOT = Path(__file__).resolve().parents[2]
BASELINE = json.loads((ROOT/'src/tests/baselines/address-spaces-tcg.json').read_text())


def reports():
    result = {}
    for cpus, iommu in CASES:
        key = f'{cpus}cpu-{iommu}'
        data = copy.deepcopy(BASELINE['cases'][key])
        data.update(clock_source_stable=True, distributions={name: distribution([100, 200])
                    for name in data['distributions']})
        result[key] = [dict(status='passed', accelerator='kvm', spaces_only=True,
                            cpus=cpus, iommu=iommu, run_id=key, address_spaces=data)]
    return result


class Comparison(unittest.TestCase):
    def test_ratios_raw_samples_and_repetitions(self):
        inputs = reports()
        second = copy.deepcopy(inputs['1cpu-none'][0]);second['run_id'] = 'second'
        inputs['1cpu-none'].append(second)
        result = compare(BASELINE, inputs, BASELINE['guest_source_sha256'])
        metric = result['cases']['1cpu-none']['metrics']['root_roundtrip_ns']
        self.assertEqual(metric['kvm']['samples'], [100, 200, 100, 200])
        self.assertEqual(metric['tcg_over_kvm_median'], metric['tcg']['median']/150)
        self.assertIn('TCG/KVM', markdown(result))

    def test_native_reference_comparison_and_stale_rejection(self):
        inputs = reports()
        baseline = copy.deepcopy(BASELINE)
        baseline['cases']['1cpu-none']['run_ids'] = ['old-run']
        result = compare(baseline, inputs, 'new-source', reference_kind='previous_kvm')
        metric = result['cases']['1cpu-none']['metrics']['root_roundtrip_ns']
        self.assertEqual(metric['previous_kvm_over_kvm_median'], metric['previous_kvm']['median']/150)
        self.assertIn('Previous KVM/KVM', markdown(result))
        baseline['cases']['1cpu-none']['run_ids'] = ['1cpu-none']
        with self.assertRaises(ValueError):
            compare(baseline, inputs, 'new-source', reference_kind='previous_kvm')

    def test_native_reference_must_be_complete_and_passed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root/'session.json').write_text(json.dumps(dict(status='failed')))
            with self.assertRaises(ValueError):load_native_reference(root)
            (root/'session.json').write_text(json.dumps(dict(status='passed',runs=1,guest_source_sha256='test')))
            with self.assertRaises(ValueError):load_native_reference(root)

    def test_refuse_tcg_as_native(self):
        report = reports()['1cpu-none'][0];report['accelerator'] = 'tcg'
        with self.assertRaises(ValueError):
            validate_native(report, 1, 'none')

    def test_failed_wrong_identity_and_clock_transition(self):
        for field, value in [('status', 'failed'), ('cpus', 4), ('run_id', '')]:
            report = reports()['1cpu-none'][0];report[field] = value
            with self.assertRaises(ValueError):
                validate_native(report, 1, 'none')
        report = reports()['1cpu-none'][0];report['address_spaces']['clock_source_stable'] = False
        with self.assertRaises(ValueError):
            validate_native(report, 1, 'none')

    def test_duplicate_missing_and_missing_metric(self):
        inputs = reports();inputs['1cpu-none'] *= 2
        with self.assertRaises(ValueError):
            compare(BASELINE, inputs, BASELINE['guest_source_sha256'])
        inputs = reports();del inputs['4cpu-amd']
        with self.assertRaises(ValueError):
            compare(BASELINE, inputs, BASELINE['guest_source_sha256'])
        inputs = reports();del inputs['1cpu-none'][0]['address_spaces']['distributions']['inactive_protect_ns']
        with self.assertRaises(ValueError):
            compare(BASELINE, inputs, BASELINE['guest_source_sha256'])

    def test_zero_resolution_and_compatibility_warnings(self):
        inputs = reports();data = inputs['1cpu-none'][0]['address_spaces']
        data['clock_begin'][0] = 1;data['distributions']['root_roundtrip_ns'] = distribution([0, 0])
        result = compare(BASELINE, inputs, 'different-source')
        self.assertEqual(len(result['cases']['1cpu-none']['warnings']), 2)
        self.assertIsNone(result['cases']['1cpu-none']['metrics']['root_roundtrip_ns']['tcg_over_kvm_median'])

    def test_guest_identity_tracks_code_and_ignores_documentation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);self.prepare_root(root)
            before = guest_fingerprint(root)
            (root/'src/kernel/note.md').write_text('documentation')
            self.assertEqual(guest_fingerprint(root), before)
            (root/'src/kernel/example.c').write_text('changed code')
            self.assertNotEqual(guest_fingerprint(root), before)

    @staticmethod
    def prepare_root(root):
        for directory in ('kernel', 'boot', 'kmodules', 'tests', 'tools', 'assets'):
            (root/'src'/directory).mkdir(parents=True)
        (root/'src/Makefile').write_text('synthetic makefile')
        (root/'src/assets/cloudberry.png').write_bytes(b'synthetic asset')
        (root/'src/tests/baselines').mkdir()
        (root/'src/tests/baselines/address-spaces-tcg.json').write_text(json.dumps(BASELINE))

    def test_complete_script_archives_only_reports_and_logs(self):
        spec = importlib.util.spec_from_file_location('bench_complete', ROOT/'src/tests/bench-spaces-kvm.py')
        module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);self.prepare_root(root)
            boot_number = 0
            def logged(command, path):
                nonlocal boot_number
                path.write_text('synthetic command log')
                build = root/'bin/tests/kvm-spaces-build'
                if command[0] == 'make':
                    for name in ('tests/src-spaces-kernel.elf', 'kmodules/probe.kmod'):
                        binary = build/name;binary.parent.mkdir(parents=True, exist_ok=True);binary.write_bytes(b'synthetic guest')
                else:
                    cpus = int(next(v.split('=')[1] for v in command if v.startswith('--cpus=')))
                    iommu = next(v.split('=')[1] for v in command if v.startswith('--iommu='))
                    key = f'{cpus}cpu-{iommu}';live = build/'tests'/('spaces-'+key);live.mkdir(parents=True, exist_ok=True)
                    boot_number += 1
                    report = reports()[key][0];report['run_id'] += str(boot_number)
                    (live/'report.json').write_text(json.dumps(report))
                    (live/'boot.log').write_text('synthetic acceptance')
            with patch.object(module, 'ROOT', root), patch('sys.argv', ['bench']), patch.object(module.subprocess, 'run'), patch.object(module, 'host_info', return_value={'synthetic':True}), patch.object(module, 'logged', side_effect=logged), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(module.main(), 0)
                # A second invocation automatically snapshots the accepted prior
                # session and emits a distinct native-to-native comparison.
                self.assertEqual(module.main(), 0)
            latest = json.loads((root/'bin/tests/kvm-spaces/latest.json').read_text())
            self.assertEqual(latest['status'], 'passed')
            with tarfile.open(root/latest['archive']) as archive:
                members = archive.getnames()
                self.assertTrue(any(v.endswith('/comparison.json') for v in members))
                self.assertTrue(any(v.endswith('/native-comparison.json') for v in members))
                self.assertTrue(any(v.endswith('/native-reference.json') for v in members))
                self.assertEqual(sum(v.endswith('/report.json') for v in members), 3)
                self.assertFalse(any(v.endswith(('.img', '.iso', '.kmod', '.elf')) for v in members))

    def test_runner_fails_before_build_without_kvm(self):
        spec = importlib.util.spec_from_file_location('bench', ROOT/'src/tests/bench-spaces-kvm.py')
        module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with patch('sys.argv', ['bench']), patch.object(module.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'native-check')) as run, patch.object(module, 'logged') as logged:
            with self.assertRaises(subprocess.CalledProcessError):
                module.main()
            self.assertEqual(run.call_count, 1)
            logged.assert_not_called()


if __name__ == '__main__':
    unittest.main()

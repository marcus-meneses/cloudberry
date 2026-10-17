"""Fail-closed acceptance reporting for missing, corrupted and noisy samples."""
import unittest
from space_report import parse_spaces, distribution, SHAPES, SWITCH, INSTRUMENTATION


def fixture():
    rows = ['MEASURE spaces clock_begin 2 16 10000000 0',
            'MEASURE spaces clock_end 2 16 10000000 0',
            'MEASURE spaces root_mode 1 0', 'MEASURE spaces cycles 8',
            'MEASURE spaces fault_events 80']
    def row(key, count, value):
        rows.append('MEASURE spaces '+key+(' '+str(value))*count)
    rows.append('MEASURE spaces instrumentation 2 256 16 64')
    for key in INSTRUMENTATION:
        row(key, 64, 10 if key==INSTRUMENTATION[0] else 170)
    for worker in range(16):
        rows.append(f'MEASURE spaces worker 0 {worker+1} 101')
        for key in SWITCH:
            row(key, 32, 20 if key == 'root_roundtrip_ns' else 30)
    for key in ('protect_clock_bracket_ns', 'inactive_protect_ns', 'no_active_protect_ns'):
        row(key, 256, 10)
    for key in ('cycle_max_hold_ns', 'cycle_max_irq_off_ns'):
        row(key, 8, 10)
    for shape, pages in zip(SHAPES, (4, 4, 131)):
        for key in (f'teardown_{shape}_ns', f'populate_{shape}_ns',
                    f'teardown_{shape}_hold_ns', f'teardown_{shape}_irq_off_ns'):
            row(key, 10, 10)
        row(f'{shape}_table_pages', 10, pages)
    return '\n'.join(rows)


class Reports(unittest.TestCase):
    def test_raw_samples_and_negative_noise_survive(self):
        report = parse_spaces(fixture(), 1)
        self.assertEqual(report['fault_events'], 80)
        self.assertEqual(report['distributions']['root_roundtrip_ns']['count'], 512)
        self.assertEqual(report['derived']['estimated_extra_ns_per_root'], -5)
        self.assertTrue(all(v == -10 for v in report['derived']['paired_root_roundtrip_extra_ns']['samples']))

    def test_truncated_worker_rejected(self):
        text = fixture().replace('MEASURE spaces yield_roundtrip_ns', 'INCOMPLETE spaces yield_roundtrip_ns', 1)
        with self.assertRaises(ValueError):
            parse_spaces(text, 1)

    def test_malformed_sample_rejected(self):
        with self.assertRaises(ValueError):
            parse_spaces(fixture()+'\nMEASURE spaces root_roundtrip_ns oops', 1)

    def test_missing_fault_acceptance_rejected(self):
        with self.assertRaises(ValueError):
            parse_spaces(fixture().replace('MEASURE spaces fault_events 80', ''), 1)

    def test_wrong_geometry_rejected(self):
        with self.assertRaises(ValueError):
            parse_spaces(fixture().replace('sparse_128_table_pages 131', 'sparse_128_table_pages 130'), 1)

    def test_topology_mismatch_rejected(self):
        with self.assertRaises(ValueError):
            parse_spaces(fixture(), 4)

    def test_nearest_rank_and_input_preservation(self):
        raw = [7, 1, 5, 3]
        result = distribution(raw)
        self.assertEqual((result['min'], result['median'], result['p95'], result['max']), (1, 4, 7, 7))
        self.assertEqual(raw, [7, 1, 5, 3])

    def test_independent_instrumentation_and_incomplete_rejection(self):
        report = parse_spaces(fixture(), 1)['instrumentation']
        self.assertEqual(report['timestamp_ordering'], 'lfence')
        self.assertEqual(report['estimated_ns_per_operation']['instrument_ticks_16_ns']['median'], 10)
        self.assertEqual(parse_spaces(fixture().replace('instrumentation 2 256', 'instrumentation 2 0'), 1)['instrumentation']['timestamp_ordering'], 'cpuid')
        with self.assertRaises(ValueError):
            parse_spaces(fixture().replace('MEASURE spaces instrument_clock_16_ns', 'MISSING instrument_clock_16_ns'), 1)


if __name__ == '__main__':
    unittest.main()

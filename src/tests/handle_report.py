"""Strict process/handle measurements. Reports never influence guest builds."""
from space_report import distribution

CASES = ('clock_bracket', 'acquire_memory', 'reflect_memory', 'clock_handle',
         'echo_0', 'echo_128', 'echo_4096', 'allocate_free_4096')


def parse_handles(text, cpus):
    samples, locks, attribution = {}, {}, []
    clock = None
    for line in text.splitlines():
        if line.startswith('MEASURE handles '):
            fields = line.split()[2:]
            kind, values = fields[0], [int(v) for v in fields[1:]]
            if any(v < 0 for v in values):
                raise ValueError('negative handle measurement')
            if kind == 'clock':
                if clock is not None or len(values) != 3 or values[0] not in (1, 2):
                    raise ValueError('invalid/duplicate clock identity')
                clock = dict(source=values[0], tsc_hz=values[1], calibrations=values[2])
            elif kind == 'samples':
                if len(values) != 49 or values[0] >= len(CASES) or values[0] in samples:
                    raise ValueError('invalid/duplicate sample group')
                samples[values[0]] = distribution(values[1:])
            elif kind == 'lock':
                if len(values) != 9 or values[0] in locks:
                    raise ValueError('invalid/duplicate lock group')
                locks[values[0]] = dict(zip(('cpu', 'acquisitions', 'contended',
                                            'max_wait_ns', 'max_hold_ns', 'max_irq_off_ns',
                                            'tsc_samples', 'hpet_samples', 'unavailable_samples'), values))
            else:
                raise ValueError('unknown handle measurement')
        elif line.startswith('ATTR handles '):
            fields = line.split()[2:]
            if len(fields) != 12:
                raise ValueError('invalid attribution')
            values = [int(v) for v in fields[:-1]]
            attribution.append(dict(zip(('cpu', 'kind', 'ns', 'ticks', 'sample_cpu', 'method_id',
                                         'domain', 'caller', 'clock_epoch', 'source', 'address_space'), values),
                                    method=fields[-1]))
    expected = set(range(2 if cpus > 1 else 1))
    if clock is None or set(samples) != set(range(len(CASES))) or set(locks) != expected:
        raise ValueError('incomplete handle measurements')
    if len(attribution) != 3 * len(expected) or {(a['cpu'], a['kind']) for a in attribution} != {(c, k) for c in expected for k in range(3)}:
        raise ValueError('missing/duplicate attribution')
    if 'PASS handle benchmark' not in text:
        raise ValueError('missing handle benchmark acceptance')
    baseline = samples[0]['median']
    return dict(clock=clock, units='ns', samples_per_case=48, warmup_per_case=8,
                measurements={name: samples[i] for i, name in enumerate(CASES)},
                bracket_adjusted_median_estimates={name: max(0, samples[i]['median'] - baseline)
                                                  for i, name in enumerate(CASES) if i},
                locks=list(locks.values()), attribution=attribution,
                notes='Raw timings include endpoint clock and result validation/release. Adjusted medians are estimates, not pure transport costs. Lock peaks include process launch and owner waits.')

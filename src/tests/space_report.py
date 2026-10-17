"""Strict isolated address-space measurements; never feeds compilation/boot."""
import math
import re
import statistics

SWITCH = ('switch_clock_bracket_ns', 'same_root_pair_ns',
          'root_roundtrip_ns', 'yield_roundtrip_ns')
SHAPES = ('dense_32', 'dense_512', 'sparse_128')
INSTRUMENTATION = ('instrument_hpet_bracket_ns', 'instrument_ticks_16_ns',
                   'instrument_cpuid_ticks_16_ns', 'instrument_clock_16_ns',
                   'instrument_lock_16_ns', 'instrument_nested_lock_16_ns',
                   'instrument_irq_lock_16_ns')


def distribution(values):
    if not values:
        raise ValueError('empty measurement distribution')
    ordered = sorted(values)
    return dict(count=len(values), min=ordered[0], median=statistics.median(ordered),
                p95=ordered[math.ceil(.95 * len(ordered)) - 1], max=ordered[-1],
                samples=values)


def parse_spaces(text, cpus):
    groups, workers, current = {}, [], None
    for line in text.splitlines():
        if not line.startswith('MEASURE spaces '):
            continue
        match = re.fullmatch(r'MEASURE spaces ([a-z0-9_]+)((?: [0-9]+)+)', line)
        if not match:
            raise ValueError('malformed space measurement: ' + line)
        key, data = match.groups()
        values = [int(x) for x in data.split()]
        if key == 'worker':
            if len(values) != 3 or values[0] >= cpus:
                raise ValueError('invalid worker identity')
            current = dict(cpu=values[0], space=values[1], owner=values[2], timings={})
            workers.append(current)
            continue
        groups.setdefault(key, []).extend(values)
        if key in SWITCH:
            if current is None or key in current['timings'] or len(values) != 32:
                raise ValueError('missing/duplicate worker timing')
            current['timings'][key] = values
    def require(key, count):
        if len(groups.get(key, [])) != count:
            raise ValueError(f'{key}: expected {count} samples')
    count = 8 * (3 if cpus > 1 else 2)
    if len(workers) != count or any(set(w['timings']) != set(SWITCH) for w in workers):
        raise ValueError('incomplete worker measurements')
    for key in SWITCH:
        require(key, count * 32)
    protection = 'remote_protect_ns' if cpus > 1 else 'no_active_protect_ns'
    for key in ('protect_clock_bracket_ns', 'inactive_protect_ns', protection):
        require(key, 256)
    for key in ('cycle_max_hold_ns', 'cycle_max_irq_off_ns'):
        require(key, 8)
    for key in ('clock_begin', 'clock_end'):
        require(key, 4)
        if groups[key][0] not in (1, 2):
            raise ValueError('missing clock source')
    require('root_mode', 2)
    require('instrumentation', 4)
    vendor, flags, batch, samples = groups['instrumentation']
    if vendor not in (1, 2) or batch != 16 or samples != 64:
        raise ValueError('invalid instrumentation metadata')
    for key in INSTRUMENTATION:
        require(key, samples)
    if groups['root_mode'][0] not in (0, 1) or groups['root_mode'][1] != 0:
        raise ValueError('unexpected paging mode')
    if groups.get('cycles') != [8] or groups.get('fault_events') != [count * 5]:
        raise ValueError('missing fault/cycle acceptance')
    for shape, expected in zip(SHAPES, (4, 4, 131)):
        for key in (f'teardown_{shape}_ns', f'populate_{shape}_ns',
                    f'teardown_{shape}_hold_ns', f'teardown_{shape}_irq_off_ns'):
            require(key, 10)
        if groups.get(f'{shape}_table_pages') != [expected] * 10:
            raise ValueError('unexpected private table geometry: ' + shape)
    raw = {key: distribution(values) for key, values in groups.items()
           if key.endswith('_ns')}
    extra = [a-b for a, b in zip(groups['root_roundtrip_ns'], groups['same_root_pair_ns'])]
    derived = dict(paired_root_roundtrip_extra_ns=distribution(extra),
                   estimated_extra_ns_per_root=statistics.median(extra)/2)
    if cpus > 1:
        derived['remote_minus_inactive_protect_ns'] = distribution(
            [a-b for a, b in zip(groups[protection], groups['inactive_protect_ns'])])
    return dict(clock_begin=groups['clock_begin'], clock_end=groups['clock_end'],
                clock_source_stable=groups['clock_begin'][0] == groups['clock_end'][0],
                pge=bool(groups['root_mode'][0]), pcid=False,
                cycles=8, fault_events=count*5, workers=workers,
                private_table_pages={shape: groups[f'{shape}_table_pages'] for shape in SHAPES},
                instrumentation=dict(vendor=vendor, cpu_tsc_flags=flags,
                                     timestamp_ordering='lfence' if vendor == 1 or flags & 256 else 'rdtscp-cpuid' if flags & 128 else 'cpuid',
                                     operations_per_sample=batch, samples=samples,
                                     estimated_ns_per_operation={key: distribution([(a-b)/batch for a,b in zip(groups[key],groups[INSTRUMENTATION[0]])]) for key in INSTRUMENTATION[1:]}),
                distributions=raw, derived=derived,
                notes=['All raw samples retained; p95 uses nearest rank.',
                       'Nanoseconds are guest-clock elapsed time, including virtualization and waits.',
                       'Root roundtrip covers two mediated selections with IF=0; excludes FP/stack switching.',
                       'Paired differences are estimates; negative values are retained as noise.',
                       'Yield roundtrip includes scheduling/peer execution, not exclusive root cost.',
                       'Hold/IRQ-off values cover managed BSP instrumentation; they are observations, not bounds.'])

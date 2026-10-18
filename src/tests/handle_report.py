"""Strict process/handle measurements. Reports never influence guest builds."""
from space_report import distribution

CASES = ('clock_bracket', 'acquire_memory', 'reflect_memory', 'clock_handle',
         'echo_0', 'echo_128', 'echo_4096', 'allocate_free_4096')


PROFILE_FIELDS = {
    'clock': ('phase','source','reason','flags','hz','calibrations','qualification_reason','qualification_cpu','qualification_samples','qualification_status','qualification_cpus','max_offset_ns','max_drift_ppm','max_uncertainty_ns','qualification_calibration'),
    'sample': ('cpu','round','tsc_ns','hpet_ns','uncertainty_ns','flags','empty_ns','bracket_ns','call_ns','relay_ns','hz','epoch'),
    'exchange': ('cpu','round','before','remote','after','hz','epoch','offset_ns','uncertainty_ns'),
    'group': ('cpu','attempt','status','retained','bracket_mask','outlier_mask','target_ns','interval_median_ns','uncertainty_median_ns','drift_median_ppb','offset_median_ns','drift_mad_ppb','offset_mad_ns','max_offset_ns','max_drift_ppm','max_uncertainty_ns'),
    'offset': ('cpu','attempt','phase','status','retained','wide_mask','outlier_mask','median_ns','mad_ns','max_offset_ns','max_uncertainty_ns'),
}
REASONS = ('none','not-initialized','missing-tsc','missing-invariant','missing-hpet','uncalibrated','calibration-jitter','frequency-mismatch','regression','capability-mismatch','reference-failure','conversion','offset','drift','uncertainty','timeout','activation','source','setup','missing-method')

def native_timing_error(handles):
    phases=handles.get('clock_diagnostics',{})
    if set(phases)!={'origin','begin','end'}:return 'missing clock diagnostics; rebuild the guest before native acceptance'
    current=phases['end'];qualified=phases['begin']
    label=lambda value:REASONS[value] if 0<=value<len(REASONS) else str(value)
    if handles['clock']['source']!=1 or any(d['source']!=1 for d in phases.values()):
        return (f"HPET/fallback timing is not a TSC baseline: runtime reason={label(current['reason'])}; "
                f"SMP qualification reason={label(qualified['qualification_reason'])}, CPU={qualified['qualification_cpu']}, "
                f"status={qualified['qualification_status']}, samples={qualified['qualification_samples']}. "
                'Correctness passed; inspect handles.clock_diagnostics and handles.clock_profile in the saved report.')
    if len({(d['hz'],d['calibrations']) for d in phases.values()})!=1:return 'clock frequency/calibration changed before or during the measurement'
    unavailable=[f"CPU{m['cpu']}={m['unavailable_samples']}" for m in handles['locks'] if m['unavailable_samples']]
    if unavailable:return 'unavailable profiling samples: '+', '.join(unavailable)
    return None


def parse_handles(text, cpus):
    samples, locks, attribution = {}, {}, []
    clock = None
    profiles={kind:[] for kind in PROFILE_FIELDS}; seen=set()
    for line in text.splitlines():
        if line.startswith('PROFILE handles '):
            fields=line.split()[2:];kind=fields[0]
            if kind not in PROFILE_FIELDS or len(fields)-1!=len(PROFILE_FIELDS[kind]):raise ValueError('invalid clock profile framing')
            values=[int(v) for v in fields[1:]]
            if any(v<0 or v>2**64-1 for v in values):raise ValueError('invalid clock profile word')
            row=dict(zip(PROFILE_FIELDS[kind],values))
            if kind=='clock':
                key=(kind,row['phase'])
                if row['phase']>2 or row['source'] not in (1,2):raise ValueError('invalid clock phase/source')
            else:
                if row['cpu']>=cpus:raise ValueError('invalid clock profile CPU')
                key=(kind,row['cpu'],row.get('round',row.get('attempt')),row.get('phase'))
                if row.get('round',0)>=380 or row.get('attempt',0)>=19 or row.get('phase',0)>1:raise ValueError('invalid clock profile index')
                for name in ('offset_ns','drift_median_ppb','offset_median_ns','median_ns'):
                    if name in row and row[name]>=2**63:row[name]-=2**64
            if key in seen:raise ValueError('duplicate clock profile record')
            seen.add(key);profiles[kind].append(row)
        elif line.startswith('MEASURE handles '):
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
    if {d['phase'] for d in profiles['clock']}!={0,1,2}:raise ValueError('incomplete clock diagnostics')
    phases={('origin','begin','end')[d['phase']]:d for d in profiles.pop('clock')}
    measured=phases['end']
    if any(clock[k]!=measured[v] for k,v in (('source','source'),('tsc_hz','hz'),('calibrations','calibrations'))):raise ValueError('clock identity differs from final diagnostics')
    baseline = samples[0]['median']
    return dict(clock=clock, clock_diagnostics=phases, clock_profile=profiles, units='ns', samples_per_case=48, warmup_per_case=8,
                measurements={name: samples[i] for i, name in enumerate(CASES)},
                bracket_adjusted_median_estimates={name: max(0, samples[i]['median'] - baseline)
                                                  for i, name in enumerate(CASES) if i},
                locks=list(locks.values()), attribution=attribution,
                notes='Raw timings include endpoint clock and result validation/release. Adjusted medians are estimates, not pure transport costs. Lock peaks include process launch and owner waits.')

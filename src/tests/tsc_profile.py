"""Audit adaptive kernel clock groups and preserve every raw observation."""
import re

TRIALS = 10

def columns(measurements, prefix, names):
    values = [measurements.get(prefix + name, []) for name in names]
    if len({len(value) for value in values}) != 1:
        raise ValueError('Incomplete TSC profiling columns')
    return [dict(zip(names, row)) for row in zip(*values)]

def parse_exchanges(content):
    keys = ('cpu', 'round', 'before', 'remote', 'after', 'hz', 'epoch', 'offset_ns', 'uncertainty_ns')
    pattern = r'^TSCX ' + r' '.join([r'(\d+)'] * 7 + [r'(-?\d+)', r'(\d+)']) + '$'
    return [dict(zip(keys, map(int, row))) for row in re.findall(pattern, content, re.MULTILINE)]

def describe_attempts(groups):
    attempts = []
    for group in sorted((g for g in groups if g['cpu'] == 0), key=lambda g: g['attempt']):
        previous = attempts[-1] if attempts else None
        epoch = group.get('calibration_epoch')
        reason = ('calibration verification' if epoch != previous.get('epoch') else 'replacement') if previous and group['target_ns'] == previous['target_ns'] else 'longer window'
        attempts.append(dict(attempt=group['attempt'], target_ns=group['target_ns'], epoch=epoch,
                             reason=reason if previous else 'initial window'))
    return attempts

def summarize(measurements, exchanges=None):
    rows = columns(measurements, 'TSC sample ', ('CPU', 'round', 'ticks ns', 'HPET ns', 'uncertainty ns'))
    samples = [dict(cpu=r['CPU'], round=r['round'], tsc_ns=r['ticks ns'],
                    hpet_ns=r['HPET ns'], uncertainty_ns=r['uncertainty ns'],
                    attempt=r['round'] // (2 * TRIALS), trial=r['round'] % TRIALS,
                    edge='start' if r['round'] % (2 * TRIALS) < TRIALS else 'end') for r in rows]
    cost_names = ('empty ns', 'bracket ns', 'call ns', 'relay ns', 'Hz', 'epoch')
    costs = columns(measurements, 'TSC sample ', cost_names)
    if costs and len(costs) != len(samples):
        raise ValueError('Incomplete TSC cost samples')
    for sample, cost in zip(samples, costs):
        sample.update({key.replace(' ', '_').lower(): value for key, value in cost.items()})
    indexed = {(sample['cpu'], sample['round']): sample for sample in samples}
    if len(indexed) != len(samples):
        raise ValueError('Duplicate TSC sample')
    group_names = ('CPU', 'attempt', 'status', 'retained', 'bracket mask', 'outlier mask',
                   'target ns', 'interval median ns', 'uncertainty median ns', 'drift median ppb',
                   'offset median ns', 'drift MAD ppb', 'offset MAD ns', 'offset bound ns',
                   'drift bound ppm', 'uncertainty bound ns')
    groups = [{key.replace(' ', '_').lower(): value for key, value in r.items()}
              for r in columns(measurements, 'TSC group ', group_names)]
    for group in groups:
        mask = group['bracket_mask'] | group['outlier_mask']
        if mask >= (1 << TRIALS) or group['bracket_mask'] & group['outlier_mask']:
            raise ValueError('Invalid TSC rejection masks')
        group['estimates_available'] = group['interval_median_ns'] > 0
        if group['status'] == 1 and (group['retained'] < 7 or group['retained'] != TRIALS - mask.bit_count()
                or group['offset_bound_ns'] > 10000 or group['drift_bound_ppm'] > 500
                or group['uncertainty_bound_ns'] > 50000):
            raise ValueError('Invalid accepted TSC group')
    grouped = {(group['cpu'], group['attempt']): group for group in groups}
    if len(grouped) != len(groups):
        raise ValueError('Duplicate TSC group')
    for group in groups:
        first = indexed.get((group['cpu'], group['attempt'] * 2 * TRIALS))
        if first and 'hz' in first:
            group.update(input_hz=first['hz'], calibration_epoch=first['epoch'])
            group['estimated_hz'] = (first['hz'] * (1_000_000_000 + group['drift_median_ppb']) + 500_000_000) // 1_000_000_000 if group['estimates_available'] else None
    attempts = describe_attempts(groups)
    windows = []
    for cpu, start in sorted(indexed):
        if start % (2 * TRIALS) >= TRIALS:
            continue
        first = indexed[(cpu, start)]
        last = indexed.get((cpu, start + TRIALS))
        if last is None:
            continue
        if 'epoch' in first and (first['epoch'], first['hz']) != (last['epoch'], last['hz']):
            raise ValueError('Clock window crosses calibration epochs')
        hpet, tsc = last['hpet_ns'] - first['hpet_ns'], last['tsc_ns'] - first['tsc_ns']
        error = tsc - hpet
        uncertainty = first['uncertainty_ns'] + last['uncertainty_ns']
        base_first, base_last = indexed[(0, start)], indexed[(0, start + TRIALS)]
        offsets = [(s['tsc_ns'] - s['hpet_ns']) - (b['tsc_ns'] - b['hpet_ns'])
                   for s, b in ((first, base_first), (last, base_last))]
        bound = 0 if cpu == 0 else max(abs(offsets[0]) + first['uncertainty_ns'] + base_first['uncertainty_ns'],
                                       abs(offsets[1]) + last['uncertainty_ns'] + base_last['uncertainty_ns'])
        attempt, trial = first['attempt'], first['trial']
        group = grouped.get((cpu, attempt))
        rejected = ('wide bracket' if group['bracket_mask'] & (1 << trial) else
                    'MAD outlier' if group['outlier_mask'] & (1 << trial) else None) if group else None
        windows.append(dict(cpu=cpu, attempt=attempt, trial=trial, target_ns=group['target_ns'] if group else None,
            hpet_interval_ns=hpet, tsc_interval_ns=tsc, drift_ns=error, endpoint_uncertainty_ns=uncertainty,
            drift_ppm=error * 1_000_000 / hpet if hpet > 0 else None,
            drift_upper_bound_ppm=(abs(error) + uncertainty) * 1_000_000 / hpet if hpet > 0 else None,
            observed_bsp_offsets_ns=offsets, offset_upper_bound_ns=bound,
            bsp_self_comparison=cpu == 0, offset_diagnostic_only=True, rejection=rejected, group_status=group['status'] if group else None))
    calibrations = []
    overhead = []
    for cpu, epoch in sorted({(s['cpu'], s['epoch']) for s in samples if 'epoch' in s}):
        cohort = [s for s in samples if s['cpu'] == cpu and s.get('epoch') == epoch]
        record = dict(cpu=cpu, epoch=epoch, input_hz=cohort[0]['hz'])
        for key in ('empty_ns', 'bracket_ns', 'call_ns', 'relay_ns'):
            values = sorted(s[key] for s in cohort) if key != 'relay_ns' or cpu else []
            record[key] = dict(min=min(values), median=values[len(values)//2], max=max(values)) if values else None
        overhead.append(record)
    for sample in sorted(samples, key=lambda s: (s['round'], s['cpu'])):
        if 'epoch' not in sample:
            continue
        if not calibrations or calibrations[-1]['epoch'] != sample['epoch']:
            calibrations.append(dict(epoch=sample['epoch'], hz=sample['hz'], first_attempt=sample['attempt']))
        elif calibrations[-1]['hz'] != sample['hz']:
            raise ValueError('Frequency changed within calibration epoch')
    offset_names = ('CPU', 'attempt', 'phase', 'status', 'retained', 'wide mask', 'outlier mask',
                    'median ns', 'MAD ns', 'bound ns', 'uncertainty ns')
    offset_groups = [{key.replace(' ', '_').lower(): value for key, value in row.items()}
                     for row in columns(measurements, 'TSC offset ', offset_names)]
    offset_index = {(g['cpu'], g['attempt'], g['phase']): g for g in offset_groups}
    if len(offset_index) != len(offset_groups):
        raise ValueError('Duplicate TSC offset group')
    for group in offset_groups:
        mask = group['wide_mask'] | group['outlier_mask']
        if mask >= (1 << TRIALS) or group['wide_mask'] & group['outlier_mask']:
            raise ValueError('Invalid TSC offset masks')
        if group['status'] == 1 and (group['retained'] < 7 or group['retained'] != TRIALS - mask.bit_count()
                or group['bound_ns'] > 10000 or group['uncertainty_ns'] > 50000):
            raise ValueError('Invalid accepted TSC offset group')
    exchange_rows = []
    seen = set()
    for original in exchanges or []:
        row = dict(original);cpu, rnd = row['cpu'], row['round']
        if (cpu, rnd) in seen or not cpu or not row['hz'] or row['after'] < row['before']:
            raise ValueError('Invalid raw TSC exchange')
        seen.add((cpu, rnd))
        middle = row['before'] + (row['after'] - row['before']) // 2
        delta = row['remote'] - middle
        magnitude = (abs(delta) * 1_000_000_000 + row['hz'] - 1) // row['hz']
        offset = magnitude if delta >= 0 else -magnitude
        half = (row['after'] - row['before'] + 1) // 2
        uncertainty = (half * 1_000_000_000 + row['hz'] - 1) // row['hz'] + 1
        if offset != row['offset_ns'] or uncertainty != row['uncertainty_ns']:
            raise ValueError('TSC exchange bound mismatch')
        sample = indexed.get((cpu, rnd))
        if sample and 'epoch' in sample and (sample['hz'], sample['epoch']) != (row['hz'], row['epoch']):
            raise ValueError('TSC exchange epoch mismatch')
        attempt, phase, trial = rnd // 20, (rnd % 20) // 10, rnd % 10
        group = offset_index.get((cpu, attempt, phase))
        reason = ('wide round trip' if group['wide_mask'] & (1 << trial) else
                  'MAD outlier' if group['outlier_mask'] & (1 << trial) else None) if group else None
        row.update(attempt=attempt, phase=phase, trial=trial, rejection=reason,
                   offset_bound_ns=abs(offset) + uncertainty,
                   round_trip_ns=(row['after'] - row['before']) * 1_000_000_000 // row['hz'])
        exchange_rows.append(row)
    for group in offset_groups:
        if not group['cpu']:
            continue
        rows = [row for row in exchange_rows if (row['cpu'], row['attempt'], row['phase']) ==
                (group['cpu'], group['attempt'], group['phase'])]
        if len(rows) != TRIALS:
            raise ValueError('Incomplete TSC exchange evidence')
        if group['status'] == 1 and any(row['offset_bound_ns'] > 10000 or row['uncertainty_ns'] > 50000
                                      for row in rows if row['rejection'] is None):
            raise ValueError('Accepted TSC exchange exceeds its bounds')
    return dict(samples=samples, windows=windows, groups=groups, calibrations=calibrations, overhead=overhead, attempts=attempts,
                exchanges=exchange_rows, offset_groups=offset_groups,
                exchange_wide_discarded=sum(g['wide_mask'].bit_count() for g in offset_groups),
                exchange_outliers_discarded=sum(g['outlier_mask'].bit_count() for g in offset_groups),
                group_purpose="frequency; TSC-only offset groups qualify synchronization",
                method='per-CPU upper medians and uncertainty-aware MAD filtering; minimum 7/10 retained',
                analysis_only=True, outliers_discarded=sum(g['outlier_mask'].bit_count() for g in groups),
                brackets_discarded=sum(g['bracket_mask'].bit_count() for g in groups))

if __name__ == '__main__':
    schedule = describe_attempts([
        dict(cpu=0, attempt=i, target_ns=target, calibration_epoch=epoch)
        for i, (target, epoch) in enumerate(((10, 1), (10, 1), (100, 1), (100, 2)))])
    assert [a['reason'] for a in schedule] == ['initial window', 'replacement', 'longer window', 'calibration verification']

    exchange = parse_exchanges('TSCX 1 0 1000 1300 1200 1000000000 1 200 101\n')
    audit = summarize({}, exchange)
    assert audit['exchanges'][0]['offset_bound_ns'] == 301 and audit['exchanges'][0]['round_trip_ns'] == 200
    bad = dict(exchange[0], uncertainty_ns=100)
    try:
        summarize({}, [bad])
    except ValueError:
        pass
    else:
        raise AssertionError('Understated RTT uncertainty accepted')

    fixture = {key: [] for key in ('TSC sample CPU', 'TSC sample round', 'TSC sample ticks ns',
                                  'TSC sample HPET ns', 'TSC sample uncertainty ns')}
    for cpu in range(2):
        for rnd in range(20):
            elapsed = (rnd % TRIALS) * 1000 + (10_000_000 if rnd >= TRIALS else 0)
            for key, value in zip(fixture, (cpu, rnd, 1_000_000 + elapsed + elapsed // 10_000 + cpu * 2000,
                                          1_000_000 + elapsed, 100)):
                fixture[key].append(value)
    result = summarize(fixture)
    own, remote = result['windows'][0], result['windows'][10]
    assert own['drift_ppm'] == 100 and own['offset_upper_bound_ns'] == 0
    assert remote['observed_bsp_offsets_ns'] == [2000, 2000] and remote['offset_upper_bound_ns'] == 2200
    assert own['drift_upper_bound_ppm'] == 120 and len(result['samples']) == 40
    group = {'CPU': 0, 'attempt': 0, 'status': 1, 'retained': 9, 'bracket mask': 0,
             'outlier mask': 1, 'target ns': 10_000_000, 'interval median ns': 10_000_000,
             'uncertainty median ns': 100, 'drift median ppb': 100000, 'offset median ns': 0,
             'drift MAD ppb': 0, 'offset MAD ns': 0, 'offset bound ns': 0,
             'drift bound ppm': 120, 'uncertainty bound ns': 100}
    fixture.update({'TSC group ' + key: [value] for key, value in group.items()})
    audited = summarize(fixture)
    assert audited['outliers_discarded'] == 1 and audited['windows'][0]['rejection'] == 'MAD outlier'
    fixture['TSC group retained'] = [6]
    try:
        summarize(fixture)
    except ValueError:
        pass
    else:
        raise AssertionError('Accepted too-small group')
    fixture['TSC group retained'] = [9]
    fixture.update({'TSC sample ' + key: [value] * 40 for key, value in
        (('empty ns', 50), ('bracket ns', 100), ('call ns', 200), ('relay ns', 1000), ('Hz', 1_000_000_000), ('epoch', 1))})
    audited = summarize(fixture)
    assert audited['groups'][0]['estimated_hz'] == 1_000_100_000
    assert audited['overhead'][0]['empty_ns']['median'] == 50 and audited['overhead'][0]['relay_ns'] is None
    assert audited['overhead'][1]['relay_ns']['median'] == 1000
    fixture['TSC sample epoch'][10] = 2
    try:
        summarize(fixture)
    except ValueError:
        pass
    else:
        raise AssertionError('Cross-epoch proof accepted')
    fixture['TSC sample epoch'][10] = 1
    # A seventh attempt repeats a duration with a fresh calibration epoch.
    for rnd in range(120, 140):
        elapsed = (rnd % TRIALS) * 1000 + (500_000_000 if rnd % 20 >= TRIALS else 0)
        values = (0, rnd, 1_000_000 + elapsed + elapsed // 10_000, 1_000_000 + elapsed, 100, 50, 100, 200, 0, 999_800_000, 2)
        keys = ('CPU', 'round', 'ticks ns', 'HPET ns', 'uncertainty ns', 'empty ns', 'bracket ns', 'call ns', 'relay ns', 'Hz', 'epoch')
        for key, value in zip(keys, values):
            fixture['TSC sample ' + key].append(value)
    repeated = dict(group, attempt=6, retained=10)
    repeated.update({'target ns': 500_000_000, 'interval median ns': 500_000_000, 'outlier mask': 0})
    for key, value in repeated.items():
        fixture['TSC group ' + key].append(value)
    audited = summarize(fixture)
    assert audited['calibrations'][-1] == dict(epoch=2, hz=999_800_000, first_attempt=6)
    assert any(w['attempt'] == 6 and w['target_ns'] == 500_000_000 for w in audited['windows'])
    assert summarize({})['samples'] == []
    fixture['TSC sample round'].pop()
    try:
        summarize(fixture)
    except ValueError:
        pass
    else:
        raise AssertionError('Incomplete sample accepted')
    print('PASS adaptive report: paired samples, signed drift, CPU offsets, correlated self-comparison and incomplete data')

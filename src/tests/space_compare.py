"""Reproducible native/TCG comparison; inputs are evidence, never build config."""
import hashlib
import json
from pathlib import Path
from space_report import distribution

CASES = ((1, 'none'), (4, 'amd'), (4, 'intel'))


def guest_fingerprint(root):
    src = Path(root) / 'src'
    suffixes = {'.c', '.h', '.S', '.ld', '.json', '.cnf', '.conf'}
    paths = {src / 'Makefile', src / 'assets/cloudberry.png'}
    for directory in ('kernel', 'boot', 'kmodules'):
        paths.update(p for p in (src / directory).rglob('*') if p.suffix in suffixes)
    paths.update(p for p in (src / 'tests').glob('*') if p.suffix in suffixes)
    paths.update((src / 'tools').glob('*.py'))
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(root).as_posix().encode() + b'\0')
        digest.update(path.read_bytes() + b'\0')
    return digest.hexdigest()


def validate_native(report, cpus, iommu):
    if (report.get('status') != 'passed' or report.get('accelerator') != 'kvm'
            or not report.get('spaces_only') or report.get('cpus') != cpus
            or report.get('iommu') != iommu or not report.get('run_id')):
        raise ValueError('missing, failed or mismatched native acceptance report')
    data = report.get('address_spaces', {})
    if data.get('cycles') != 8 or data.get('fault_events') != (80 if cpus == 1 else 120):
        raise ValueError('incomplete native fault acceptance')
    if not data.get('clock_source_stable'):
        raise ValueError('guest clock source changed during measurement; inspect the log')
    for stats in data.get('distributions', {}).values():
        samples = stats.get('samples', [])
        if not samples or stats.get('count') != len(samples) or any(not isinstance(x, int) or x < 0 for x in samples):
            raise ValueError('missing or invalid raw native timings')
    if 'root_roundtrip_ns' not in data.get('distributions', {}):
        raise ValueError('missing native root measurements')


def load_native_reference(session):
    session = Path(session)
    identity = json.loads((session/'session.json').read_text())
    if identity.get('status') != 'passed' or not identity.get('guest_source_sha256'):
        raise ValueError('reference must be a completed, accepted KVM session')
    cases = {}
    for cpus, iommu in CASES:
        key = f'{cpus}cpu-{iommu}'
        series = [json.loads(p.read_text()) for p in sorted((session/'cases'/key).glob('run-*/report.json'))]
        if len(series) != identity.get('runs'):
            raise ValueError('incomplete native reference: '+key)
        for report in series:
            validate_native(report, cpus, iommu)
        modes = {(r['address_spaces']['clock_begin'][0], r['address_spaces']['pge'], r['address_spaces']['pcid']) for r in series}
        if len(modes) != 1 or len({r['run_id'] for r in series}) != len(series):
            raise ValueError('mixed modes or duplicate reference runs: '+key)
        first = series[0]['address_spaces']
        cases[key] = dict(clock_begin=first['clock_begin'], pge=first['pge'], pcid=first['pcid'],
                          run_ids=[r['run_id'] for r in series],
                          distributions={name: distribution([v for r in series for v in r['address_spaces']['distributions'][name]['samples']]) for name in first['distributions']})
    return dict(guest_source_sha256=identity['guest_source_sha256'], cases=cases,
                session=str(session), host=json.loads((session/'host.json').read_text()))


def compare(baseline, reports, fingerprint, reference_kind='tcg'):
    if reference_kind not in ('tcg', 'previous_kvm'):
        raise ValueError('unknown reference kind')
    label = 'TCG' if reference_kind == 'tcg' else 'previous KVM'
    cases = {}
    for cpus, iommu in CASES:
        key = f'{cpus}cpu-{iommu}'
        series = reports.get(key, [])
        if not series:
            raise ValueError('missing native case: ' + key)
        for report in series:
            validate_native(report, cpus, iommu)
        if len({r['run_id'] for r in series}) != len(series):
            raise ValueError('duplicate run identity: ' + key)
        reference = baseline['cases'][key]
        if reference_kind == 'previous_kvm' and set(reference.get('run_ids', ())) & {r['run_id'] for r in series}:
            raise ValueError('native reference reuses a current run identity: '+key)
        modes = {(r['address_spaces']['clock_begin'][0], r['address_spaces']['pge'],
                  r['address_spaces']['pcid']) for r in series}
        if len(modes) != 1:
            raise ValueError('clock/paging mode differs between native repetitions: ' + key)
        mode = next(iter(modes))
        expected = (reference['clock_begin'][0], reference['pge'], reference['pcid'])
        warnings = []
        if mode != expected:
            warnings.append(f'Clock source or paging mode differs from {label}; ratios include that difference.')
        if fingerprint != baseline['guest_source_sha256']:
            warnings.append(f'Guest sources differ from the historical {label} baseline.')
        metrics = {}
        for metric, old in reference['distributions'].items():
            raw = []
            for report in series:
                stats = report['address_spaces']['distributions'].get(metric)
                if stats is None:
                    raise ValueError('missing comparable metric: ' + metric)
                raw.extend(stats['samples'])
            new = distribution(raw)
            metrics[metric] = {reference_kind:old, 'kvm':new,
                              reference_kind+'_over_kvm_median':old['median']/new['median'] if new['median'] else None,
                              'per_boot_kvm_medians':[r['address_spaces']['distributions'][metric]['median'] for r in series]}
        cases[key] = dict(run_ids=[r['run_id'] for r in series], native_clock=series[0]['address_spaces']['clock_begin'],
                          pge=mode[1], pcid=mode[2], warnings=warnings, metrics=metrics)
    return dict(schema=1, reference_kind=reference_kind, guest_source_sha256=fingerprint,
                baseline_guest_source_sha256=baseline['guest_source_sha256'],
                cases=cases, notes=['Ratios compare observed elapsed guest time, not code optimization speedup.',
                                    'Environment and instrumentation differences can affect comparisons.',
                                    'All native samples are retained; medians/p95 are pooled across equal-sized boots.'])


def markdown(result):
    kind = result.get('reference_kind', 'tcg')
    label = 'TCG' if kind == 'tcg' else 'Previous KVM'
    lines = ['# KVM address-space comparison', '',
             f'Times are microseconds. A ratio above 1 means {label} took longer.',
             'Clock, CPU exposure, hardware and virtualization differences contribute.', '']
    for case, data in result['cases'].items():
        lines += [f'## {case}', '', f'Native clock source: {data["native_clock"][0]} (1=TSC, 2=HPET); PGE={data["pge"]}; PCID={data["pcid"]}.', '']
        lines += [f'- {warning}' for warning in data['warnings']]
        if data['warnings']:
            lines.append('')
        lines += [f'| Measurement | {label} median | KVM median | KVM p95 | {label}/KVM |',
                  '| --- | ---: | ---: | ---: | ---: |']
        for name, metric in data['metrics'].items():
            ratio = metric[kind+'_over_kvm_median']
            ratio_text = f'{ratio:.2f}×' if ratio is not None else '—'
            lines.append(f'| {name} | {metric[kind]["median"]/1000:.2f} | {metric["kvm"]["median"]/1000:.2f} | {metric["kvm"]["p95"]/1000:.2f} | {ratio_text} |')
        lines += ['', 'Run IDs: '+', '.join(f'`{v}`' for v in data['run_ids']), '']
    return '\n'.join(lines)

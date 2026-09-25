"""Run alternating fixed-tick native benchmarks; compare medians and repeatability.

Both executables must include TexasPathfindingInvestigation.BenchmarkLocalPark.
Pass a working directory containing testdata and an isolated user-data directory.
"""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--baseline', required=True)
parser.add_argument('--candidate', required=True)
parser.add_argument('--cwd', required=True)
parser.add_argument('--user-data', required=True)
parser.add_argument('--rct2-data', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--runs', type=int, default=5)
parser.add_argument('--park', action='append', nargs=3, metavar=('LABEL', 'PATH', 'TICKS'), required=True)
args = parser.parse_args()
output = Path(args.output).resolve()
output.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['OPENRCT2_TEST_USER_DATA_PATH'] = str(Path(args.user_data).resolve())
env['OPENRCT2_TEST_RCT2_PATH'] = args.rct2_data
executables = {v: str(Path(getattr(args, v)).resolve()) for v in ('baseline', 'candidate')}
records = []
for label, park, ticks in args.park:
    env['OPENRCT2_INVESTIGATION_PARK'] = str(Path(park).resolve())
    env['OPENRCT2_INVESTIGATION_BENCHMARK_TICKS'] = ticks
    for repeat in range(args.runs):
        order = ('baseline', 'candidate') if repeat % 2 == 0 else ('candidate', 'baseline')
        for variant in order:
            run = subprocess.run([executables[variant], '--gtest_filter=TexasPathfindingInvestigation.BenchmarkLocalPark'],
                                 cwd=args.cwd, env=env, capture_output=True, text=True)
            log = run.stdout + run.stderr
            (output / f'{label}-{repeat}-{variant}.log').write_text(log)
            if run.returncode:
                raise RuntimeError(log[-4000:])
            line = next(line for line in log.splitlines() if line.startswith('BENCHMARK '))
            record = dict(re.findall(r'(\w+)=([^ ]+)', line))
            record.update(park=label, repeat=repeat, variant=variant)
            records.append(record)
            print(label, repeat, variant, record['us_per_tick'], 'us/tick', flush=True)
            (output / 'runs.json').write_text(json.dumps(records, indent=2))
summary = []
for label, _, _ in args.park:
    versions = {}
    for variant in executables:
        selected = [r for r in records if r['park'] == label and r['variant'] == variant]
        versions[variant] = dict(median_us=statistics.median(float(r['us_per_tick']) for r in selected),
                                 min_us=min(float(r['us_per_tick']) for r in selected),
                                 max_us=max(float(r['us_per_tick']) for r in selected),
                                 checksums=sorted({r['checksum'] for r in selected}),
                                 route_entries=sorted({r['route_directions'] for r in selected}))
        assert len(versions[variant]['checksums']) == 1, (label, variant, 'nondeterministic replay')
    change = (versions['candidate']['median_us'] / versions['baseline']['median_us'] - 1) * 100
    summary.append(dict(park=label, change_percent=change, **versions))
(output / 'summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))

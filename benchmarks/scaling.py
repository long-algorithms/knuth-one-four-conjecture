"""Repeated local scaling measurements; reports medians, not a single best run."""
import argparse
import csv
import re
import statistics
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('--bound', type=int, default=10**9)
    parser.add_argument('--seed', type=int, default=4)
    parser.add_argument('--threads', type=int, nargs='+', default=[1, 4])
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--bitmap-log2', type=int, default=24)
    parser.add_argument('--batch-size', type=int, default=64)
    parser.add_argument('--shards', type=int, default=64)
    parser.add_argument('--inline-depth', type=int, default=8)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    writer = csv.writer(sys.stdout)
    writer.writerow(['threads', 'median_wall_seconds', 'median_search_seconds',
                     'discovered', 'first_missing', 'large_states', 'large_capacity_bytes'])
    expected = None
    for threads in args.threads:
        wall_times, search_times = [], []
        for _ in range(args.repeats):
            command = [args.executable, '--threads', str(threads)]
            for option in ('bound', 'seed', 'bitmap_log2', 'batch_size', 'shards', 'inline_depth'):
                command += ['--' + option.replace('_', '-'), str(getattr(args, option))]
            start = time.perf_counter()
            result = subprocess.run(command, text=True, capture_output=True, check=True)
            wall_times.append(time.perf_counter() - start)
            fields = dict(re.findall(r'(\w+)=([^\s]+)', result.stdout))
            search_times.append(float(fields['search_seconds']))
            signature = (fields['discovered'], fields['first_missing'])
            if expected is not None and signature != expected:
                raise RuntimeError('search counts changed across benchmark runs')
            expected = signature
        writer.writerow([threads, f'{statistics.median(wall_times):.6f}',
                         f'{statistics.median(search_times):.6f}', fields['discovered'],
                         fields['first_missing'], fields['large_states'], fields['large_capacity_bytes']])
        sys.stdout.flush()


if __name__ == '__main__':
    main()

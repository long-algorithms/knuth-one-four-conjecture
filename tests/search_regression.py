"""Compare complete visited sets with an independent exact-integer graph oracle."""
import math
import random
import re
import subprocess
import sys
import tempfile
from collections import deque
from pathlib import Path

EXE = sys.argv[1]


def exact_candidate(m, bound):
    n, roots = math.factorial(m), 0
    while n > bound:
        n, roots = math.isqrt(n), roots + 1
    return n, roots


def exact_closure(seed, bound):
    # Unlike the optimized solver, expand two outgoing edges separately. This
    # independently checks its duplicate-pruned square-root-chain invariant.
    seen, queue = {seed}, deque([seed])
    while queue:
        m = queue.popleft()
        for n in (math.isqrt(m), exact_candidate(m, bound)[0]):
            if n not in seen:
                seen.add(n)
                queue.append(n)
    return seen


def fields(output):
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', output)}


def run(seed, bound, config, path):
    args = [EXE, '--bound', str(bound), '--threads', str(config[0]),
            '--batch-size', str(config[1]), '--bitmap-log2', str(config[2]),
            '--shards', str(config[3]), '--inline-depth', str(config[4]),
            '--dump-reached', str(path)]
    if seed is not None:
        args += ['--seed', str(seed)]
    output = subprocess.check_output(args, text=True, timeout=30)
    values = fields(output)
    result = list(map(int, path.read_text().splitlines()))
    assert result == sorted(set(result)), 'dump is not sorted and unique'
    assert len(result) == values['discovered'] == values['processed'], output
    assert values['seed'] == (4 if seed is None else seed), output
    seen = set(result)
    first = next((i for i in range(1, values['bitmap_limit']) if i not in seen),
                 values['bitmap_limit'])
    assert first == values['first_missing'], output
    assert 'status=uncertified_numerical_search' in output
    return set(result)


def main():
    configs = [(1, 1, 6, 1, 0), (4, 64, 6, 64, 8), (8, 4096, 12, 4, 64),
               (4, 3, 7, 16, 1), (8, 16, 6, 1, 1024)]
    cases = [(1, 1), (2, 2), (3, 6), (4, 4), (4, 6), (4, 24),
             (3, 63), (4, 64), (4, 100), (3, 1000), (4, 1000), (4, 4096)]
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'reached.txt'
        for seed, bound in cases:
            expected = exact_closure(seed, bound)
            for config in configs:
                actual = run(seed, bound, config, path)
                assert actual == expected, (seed, bound, config, actual ^ expected)
        assert run(None, 6, configs[0], path) == {1, 2, 4}, 'default seed or sqrt closure'
        # A larger differential check compares full sets, not merely counts.
        reference = run(4, 1000000, configs[0], path)
        for config in configs[1:]:
            assert run(4, 1000000, config, path) == reference

    rng = random.Random(20261031)
    candidates = [(m, b) for m in range(1, 22) for b in (1, 4, 24, 720, 2**64 - 1)]
    candidates += [(rng.randrange(21, 2049), rng.randrange(1, 2**64)) for _ in range(80)]
    for m, bound in candidates:
        output = subprocess.check_output([EXE, '--candidate', str(m), '--bound', str(bound)],
                                         text=True, timeout=10)
        actual = fields(output)
        assert (actual['candidate'], actual['roots']) == exact_candidate(m, bound), (m, bound, output)
    for args in (['--seed', '0'], ['--seed', '7', '--bound', '6'], ['--bound', '-1'],
                 ['--bound', '+4'], ['--bound', '18446744073709551616'], ['--bound', '0'],
                 ['--threads', '0'], ['--threads', '1025'], ['--bitmap-log2', '41'],
                 ['--batch-size', '0'], ['--shards', '3'], ['--candidate', '0'],
                 ['--inline-depth', '1025'],
                 ['--bound'], ['--unknown', '1']):
        result = subprocess.run([EXE, *args], text=True, capture_output=True, timeout=10)
        assert result.returncode != 0 and 'error:' in result.stderr, args
    print(f'{len(cases)} exact closures x {len(configs)} configurations; '
          f'{len(candidates)} exact candidate edges; full-set and CLI checks passed')


if __name__ == '__main__':
    main()

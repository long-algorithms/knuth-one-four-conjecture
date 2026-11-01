"""Check that concurrent workers enumerate the same visited set on a small instance."""
import re
import subprocess
import sys


def run(executable: str, workers: int) -> dict[str, int]:
    output = subprocess.check_output([
        executable, '--threads', str(workers), '--bound', '1000000',
        '--bitmap-log2', '20'], text=True, timeout=30)
    values = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', output)}
    assert 'status=uncertified_numerical_search' in output
    assert values['discovered'] == values['processed']
    assert values['first_missing'] < values['bitmap_limit']
    return values


def main() -> None:
    comparisons = [run(sys.argv[1], n) for n in (1, 4, 8)]
    actual = {(v['first_missing'], v['discovered']) for v in comparisons}
    assert len(actual) == 1, comparisons
    print('Consistent across 1, 4 and 8 workers:', actual.pop())


if __name__ == '__main__':
    main()

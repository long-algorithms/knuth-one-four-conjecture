"""Verify explicit equivalence of the starting integers 3 and 4 exactly.

T_k(n) = floor((n!)^(1/2^k)). Integer powers avoid floating-point error.
"""
from math import factorial


def verify(n: int, k: int, expected: int) -> None:
    f = factorial(n)
    power = 1 << k
    assert expected**power <= f < (expected + 1) ** power, (n, k, expected)


def main() -> None:
    # 3 -> 6 -> 720 -> 50 -> 103 -> 4
    for a, k, b in [(3, 0, 6), (6, 0, 720), (720, 10, 50),
                    (50, 5, 103), (103, 8, 4)]:
        verify(a, k, b)
    # 4 -> 24 -> 942 -> 3
    for a, k, b in [(4, 0, 24), (24, 3, 942), (942, 12, 3)]:
        verify(a, k, b)
    print("Exact 3 <-> 4 constructions verified")


if __name__ == "__main__":
    main()

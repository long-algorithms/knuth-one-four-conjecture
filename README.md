# Knuth's one-four conjecture

Computational investigation of Donald Knuth's conjecture that **every positive
integer can be expressed using a single 4**, factorials, square roots, and the
floor (integer-part) operation. This repository explores the equivalent variant
starting from **3**.

## Historical source

D. E. Knuth, **"Representing Numbers Using Only One 4,"** *Mathematics
Magazine* **37** (1964), no. 5, 308–310.
[DOI: 10.2307/2689238](https://doi.org/10.2307/2689238).

The name refers to this specific problem, not to Knuth's other conjectures.

## Why starting with 3 is equivalent

Write `T_k(n) = floor((n!)^(1/2^k))`. Exact transformations give

- `3 -> 6 -> 720 -> 50 -> 103 -> 4`, with `k = 0, 0, 10, 5, 8`;
- `4 -> 24 -> 942 -> 3`, with `k = 0, 3, 12`.

Consequently, either starting integer can generate the other using the allowed
operations. `tests/exact_representations.py` checks these assertions using only
integer arithmetic.

## Build

Requires a C++17 compiler supporting GNU `__float128` and `libquadmath`, CMake
(3.18+), Git (for the initial dependency download), and Python 3 for the tests.
Tested targets are intended for GNU/Linux with GCC; other toolchains have not
been validated.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/knuth_search --threads 4 --bound 1000 --bitmap-log2 12
```

The build uses CMake `FetchContent` to obtain
[Moodycamel's concurrentqueue](https://github.com/cameron314/concurrentqueue),
pinned to release **v1.0.5** at commit
`9afb99746f0f5fc94ac8aef737053ae0481ba8d1`. The first configure requires
network access; no library source is copied into this repository.

## Current search

`src/knuth_search.cpp` is an exploratory, concurrent reachability search. It
retains the original factorial-logarithm/quad-precision approach, but fixes the
historical `vector<bool>` data races and the premature worker-exit problem:

- atomic bitset for small nonnegative integers;
- mutex-protected hash set for larger visited integers;
- outstanding-work accounting for termination, independent of transient queue
  emptiness;
- exact integer square root for the descent chain.

Options: `--threads N`, `--bound N` (candidate ceiling, at most 10^15), and
`--bitmap-log2 N` (visited bitmap covers `[0, 2^N)`, with N from 6 through 32).
The default bitmap uses 2 MiB; `--bitmap-log2 32` uses 512 MiB. The search
prints the first **undiscovered** integer in the bitmap's range and processing
statistics. It does not prove the first missing number is unreachable.

**Important limitation:** Candidate generation uses `lgammaq` and `expq`.
Quad precision is not interval arithmetic: the program does not certify that
all floor operations were correctly rounded, and it does not emit independently
verifiable expression witnesses. Its coverage output is **numerical evidence,
not a proof**. Rigorous verification is future work.

## Historical implementation

`historical/concurrent-quad/quad.cpp` preserves the source from
[`octonion/knuth/cpp/concurrent/quad`](https://github.com/octonion/knuth/tree/main/cpp/concurrent/quad)
without algorithmic changes. The original recorded output files are retained
beside it. The historical program has a data race in `vector<bool>` and workers
can terminate while work is still outstanding; it is **not** built by default.
For reproduction only, opt in with `-DKNUTH_BUILD_HISTORICAL=ON`.

See [docs/implementation-notes.md](docs/implementation-notes.md).

## License

MIT License, copyright (c) 2022 Christopher D. Long. Moodycamel's separately
fetched library is distributed under its own BSD/Boost license.

# Knuth's one-four conjecture

Computational investigation of Donald Knuth's conjecture that every positive
integer can be expressed using **one 4**, factorials, square roots, and the floor
(integer-part) operation. The C++ search starts from **4** by default.

The program is an exploratory bounded search, **not a proof or a certified
reachability computation**. Quad-precision factorial logarithms and exponentials
can still give incorrect floors near integer boundaries. An undiscovered integer
is not a counterexample to the conjecture.

## Build and run

Requires GCC with `__float128` and `libquadmath`, CMake 3.18+, and Git.
Python 3 is required when building tests. GNU/Linux is the supported baseline.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/knuth_search
```

The default command runs a short exploratory search from 4, with an inclusive
ceiling of `10^8` and one worker. Increase `--bound` and `--threads` explicitly
for a longer search, for example:

```sh
./build/knuth_search --bound 10000000000 --threads 16
```

For a machine-specific build, add `-DKNUTH_ARCH=native` at configuration time.
Explicit GCC architecture targets such as `znver3` or `knl` are also accepted;
choose a compiler that supports the target. Do not use `-ffast-math`.

CMake `FetchContent` downloads [Moodycamel's concurrentqueue](https://github.com/cameron314/concurrentqueue),
pinned to **v1.0.5**, commit `9afb99746f0f5fc94ac8aef737053ae0481ba8d1`.
The first configuration needs network access. To use an already available source
checkout offline, pass `-DFETCHCONTENT_SOURCE_DIR_CONCURRENTQUEUE=/path/to/checkout`.
The dependency is not vendored. The project is MIT-licensed; the dependency
retains its upstream BSD-2-Clause/Boost licensing.

## Options

| Option | Default | Purpose |
| --- | --- | --- |
| `--seed N` | `4` | Starting integer; `3` supports historical comparisons |
| `--bound N` | `100000000` | Inclusive ceiling for generated integer states; accepts positive `uint64_t` values |
| `--threads N` | `1` | Worker count, 1–1024 |
| `--bitmap-log2 N` | `24` | Address bits for the dense visited bitmap, 6–40 |
| `--batch-size N` | `64` | Maximum dequeued jobs per batch, 1–4096 |
| `--shards N` | `64` | Power-of-two shard count for the sparse visited set, 1–4096 |
| `--inline-depth N` | `8` | Maximum successive large states processed without queueing; 0 disables this |
| `--large-reserve N` | `0` | Initial total reservation for sparse states; otherwise grows on demand |
| `--dump-reached FILE` | none | Sorted visited-set export for comparison; requires extra memory |
| `--candidate N` | none | Evaluate one factorial/root edge instead of running a search |

The seed must not exceed the bound. The bound is **not** a desired coverage
limit or a promise that the full search will fit in memory. Arithmetic supports
up to `18446744073709551615`; practical searches remain time- and memory-limited.

A bitmap with 24 address bits uses 2 MiB; 32 uses 512 MiB; 36 uses 8 GiB.
Allocation is capped at `bound + 1` states when that is smaller. The sparse set
and work queues require additional memory. Large requested bitmaps can exhaust
RAM; the 40-bit maximum alone would require 128 GiB.

Output includes the first missing integer **within the bitmap**, unique states,
processed states, inlined states, memory-capacity statistics, and search time.
`note=bitmap_range_exhausted` means `first_missing` is only a sentinel/lower
bound, not a tested missing integer. `large_capacity_bytes` excludes queues,
metadata, and temporary allocations. The visited-set export is neither a
resumable checkpoint nor a construction certificate.

## Algorithm and validation

The solver retains the original logarithmic factorial/repeated-square-root
approach. It adds exact small-factorial edges, exact integer square roots,
atomic bitmap ownership, sharded flat hash sets, tokenized bulk queue operations,
and bounded inline following of large-state chains. Numerical calls use a
reentrant log-gamma routine where available, with a protected fallback.

Tests compare **complete visited sets** against an independent exact-integer
oracle, vary worker/batch/shard/inline settings, test integer and hash-table
boundaries, and verify the equivalence of the two historical seeds. They are
regression checks, not certification of arbitrary large numerical edges.

See [implementation notes](docs/implementation-notes.md) for invariants,
limitations, and further algorithmic work, and [benchmarks](benchmarks/README.md)
for measurements and a repeatable scaling script. The original implementation
and output files are preserved in `historical/concurrent-quad/`.

## Largest recorded historical run

The original [`maxout.txt`](historical/concurrent-quad/maxout.txt) records
**419,916,105** as the **first integer not discovered** by the historical
concurrent quad-precision search, starting from 3. **This run was performed
on an Intel Xeon Phi system.** Its reported wall time was `921m44.615s`
(15 h 21 min 44.615 s). The run used the original, uncertified and
unformalized implementation and reported discovering all positive integers
below 419,916,105.

## Origin and the starting integer

D. E. Knuth, **“Representing Numbers Using Only One 4,”** *Mathematics Magazine*
**37** (1964), no. 5, 308–310.
[DOI: 10.2307/2689238](https://doi.org/10.2307/2689238).

Writing `T_k(n) = floor((n!)^(1/2^k))`, exact transformations give

- `3 -> 6 -> 720 -> 50 -> 103 -> 4`, with `k = 0, 0, 10, 5, 8`;
- `4 -> 24 -> 942 -> 3`, with `k = 0, 3, 12`.

Thus the unrestricted conjectures are equivalent. Very small bounded searches
need not agree: with ceiling 6, seed 4 reaches `{1,2,4}`, whereas seed 3 reaches
`{1,2,3,6}`. `tests/exact_representations.py` checks the displayed constructions
using integer arithmetic.

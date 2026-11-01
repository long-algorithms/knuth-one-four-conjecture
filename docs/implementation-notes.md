# Implementation notes

## The bounded graph

Let `B` be the inclusive integer ceiling. For each discovered positive `m`, the
principal successor is `floor((m!)^(1/2^p))`, where `p >= 0` is the smallest
integer for which that result is at most `B`. Subsequent integer square roots
give the remaining descendants. The exact identity

`floor(sqrt(floor(x))) = floor(sqrt(x))`, for `x >= 0`,

justifies descending through integers after the first root. Every new state is
expanded once. This finite graph is a useful subsearch, not all unrestricted
expressions: for example, it does not explore every possible repeated factorial
above the ceiling before descending again.

The seed's square-root chain must also be initialized. Merely marking the seed
and stopping descent at already marked states is wrong: seed 4 and ceiling 6
would omit both 2 and 1. Both are valid immediate descendants of 4.

## Numerical kernel

For `m <= 20`, the factorial fits in `uint64_t` and repeated integer square
roots give an exact principal successor. This avoids a rounded `exp(log(m!))`.
For larger `m`, compute `x = log(m!)`. The integer-inclusive criterion is

`x / 2^p < log(B + 1)`.

The older `x / 2^p <= log(B)` criterion can discard a root whose floor is still
`B`. Binary-exponent extraction (`ilogbq`) and exact power-of-two scaling
(`ldexpq`) select `p` without the earlier division and `log2q`. The finite/range
check is performed before converting to `uint64_t`, including at its maximum.
A hardware double square root only seeds an estimate; 128-bit integer products
correct it to the exact integer square root without integer divisions.

On glibc, CMake selects the reentrant `lgammaf128_r` when available. The older
`lgammaq` can write global `signgam`, creating a data race even when every call
stores +1. Other toolchains use a mutex-protected `lgammaq` fallback, which is
correctly synchronized but limits parallel numerical throughput. Select
`-DKNUTH_FORCE_QUADMATH=ON` to test that fallback.

None of this certifies the floating-point logarithm/exponential. Values near an
integer boundary remain a numerical risk. Tests against exact arithmetic or
higher precision do not substitute for an error bound. Do not use fast-math.

References: [GNU libquadmath routines](https://gcc.gnu.org/onlinedocs/libquadmath/Math-Library-Routines.html),
[GNU libc special functions and reentrant log-gamma](https://sourceware.org/glibc/manual/2.41/html_node/Special-Functions.html).

## Visited storage

Small states use a compact atomic bitmap. A relaxed read rejects known bits
without an exclusive cache-line operation; `fetch_or` decides which worker owns
a genuinely new state. A set bit records discovery, not completion. The owning
worker must finish or abort the whole search.

Large states use independently locked, cache-line-aligned shards. Each shard is
an append-only open-addressed table of 64-bit values. Zero is an empty slot,
which is safe because states are strictly positive; no deletions or tombstones
are needed. A table doubles before exceeding 75% occupancy. This removes
per-state node allocation and the old single global mutex. Hash bits used to
select the shard are not reused to select the initial bucket within that shard.
The [SplitMix64 finalizer](https://prng.di.unimi.it/splitmix64.c) mixes integer keys.

Capacity is not a fixed cost per key: power-of-two growth leaves slack, and
rehashing temporarily needs both old and new arrays. Runtime statistics report
steady table capacity, not total resident process memory. The bitmap's first
missing value is found a word at a time, rather than bit by bit.

## Scheduling and termination

Each worker owns Moodycamel producer/consumer tokens and reusable batch buffers.
Queue and outstanding-work operations are amortized over batches; statistics are
thread-local and cache-line-separated until workers join. The library's token
and bulk APIs are documented in its [upstream README](https://github.com/cameron314/concurrentqueue#readme).

There is additional structure above `floor(sqrt(B))`: only the principal
successor can lie above that threshold. Thus the large-state subgraph is a
functional graph if the bitmap covers all those square-root descendants.
A 32-bit-address bitmap (512 MiB) suffices for every 64-bit bound.
The worker follows up to `--inline-depth` newly discovered large successors
immediately and queues the other children. This reduces queue traffic without
changing the visited graph. With a smaller bitmap it still follows only one
large child and queues the others. A depth cap prevents one long chain from
monopolizing a worker. Use 0 to compare against pure batched scheduling.

`pending` counts queued tasks plus tasks held by workers. A held task covers its
inline chain as well. Output tasks are counted before publication; the input
batch is retired only after all its inline work and publications finish.
Therefore `pending == 0`, not an unsuccessful dequeue or approximate queue size,
establishes completion. Brief starvation yields, then backs off with short
sleeps. Final counters require `discovered == processed` and `pending == 0`.
Worker and allocation exceptions stop all workers and propagate to main; they
must never be reported as a completed search.

## Tests and reproducibility

`tests/search_regression.py` constructs exact small graphs using Python
factorials and integer square roots, expanding root and factorial edges
separately. It compares the complete sets with concurrent runs, including
very small ceilings and bitmap boundaries. It also checks 185 exact principal
edges and command-line rejection cases. `tests/core.cpp` checks integer roots,
flat-table growth/duplicates, and contested atomic/sharded insertion ownership.
Assertions in the C++ tests stay active in Release builds.

CI runs release, serialized-fallback, and address/undefined-behavior sanitizer
configurations. These tests do not establish absence of every possible race or
numerical floor error. The historical source is preserved without edits and is
not the correctness oracle.

## Further work, in priority order

1. **Certified fast numerical filter.** Use a positive-argument Stirling
   enclosure, then accept a candidate only when both endpoints have the same
   floor. Escalate ambiguous cases to MPFR/ball arithmetic. The asymptotic
   remainder alone is insufficient: floating evaluation and transcendental
   rounding errors must also be enclosed. This is more promising than simply
   replacing quad precision with unguarded double precision.
2. **Larger-machine measurements.** Tune batches, shards, inline depth, and the
   bitmap on the 5950X and Xeon Phi separately. Test NUMA placement and parallel
   first-touch initialization before assuming a different queue library helps.
3. **Checkpointing and witnesses.** A checkpoint needs unfinished work as well
   as discovered states. Increasing the ceiling changes principal successors,
   so previously expanded states need reconsideration; loading a visited bitmap
   alone is not sufficient. Construction witnesses are a separate requirement.
4. **Transition-range compression.** On a fixed root-depth band the principal
   successor is monotone in `m`. Certified intervals on which it is constant
   could share numerical work, particularly for large inputs. This is a research
   direction, not an implemented or benchmarked speedup.

For Stirling expansions and remainder bounds, see [NIST DLMF §5.11](https://dlmf.nist.gov/5.11).

## Historical measurements

The original recorded first undiscovered values were 378,835,118 in
659m 12.449s (`10Q.txt`) and 419,916,105 in 921m 44.615s (`maxout.txt`).
The checked-in source specifies four threads, but recorded user/real time ratios
suggest other configurations. These logs are historical evidence, not certified
coverage or a reproducible performance comparison with this revision.

# Performance measurements

Initial measurements on an AMD EPYC 9V74 virtualized container with five exposed
CPUs and a four-CPU quota, using GCC 14.2.0, `-O3`, and no architecture tuning.
These are not measurements on the 5950X or Xeon Phi and should not be extrapolated
linearly to those machines. Each entry is the median of three runs. Wall time
and peak RSS are measured by GNU `time`; the table compares the combined changes,
not isolated contributions from each optimization. Small timings are noisy.

Baseline: repository commit `5e0aa8279a718b9721360c01126f966c76ac2448`, not the
historical `vector<bool>` implementation. Both versions use seed 3 for this
comparison and a 24-bit bitmap. New defaults: 64-item batches, 64 shards, inline
depth 8. Baseline concurrency includes the subsequently identified `signgam` issue.

| Ceiling | Workers | Baseline wall (s) | New wall (s) | Speedup | Baseline RSS (MiB) | New RSS (MiB) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 100000000 | 1 | 0.41 | 0.29 | 1.41x | 4.38 | 4.14 |
| 100000000 | 4 | 0.11 | 0.08 | 1.38x | 4.38 | 4.26 |
| 1000000000 | 1 | 1.35 | 0.96 | 1.41x | 8.85 | 6.39 |
| 1000000000 | 4 | 0.46 | 0.27 | 1.70x | 8.78 | 6.39 |
| 10000000000 | 1 | 4.76 | 3.50 | 1.36x | 28.56 | 13.77 |
| 10000000000 | 4 | 1.82 | 0.89 | 2.04x | 28.50 | 14.48 |

[Machine-readable results](initial-results.json) include the new per-run samples.

At ceiling `10^10`, both implementations reached 1,347,820 states and reported
24,754 as the first missing value. A separate export compared the entire sorted
visited set from the baseline (one worker, seed 3) with the new solver (four
workers, default seed 4). The files matched byte for byte; SHA-256:
`e22a1d6987df610173e85aa8e5e334898674383666a4dee7466d3800ef12dbfa`.

Inline following eliminated approximately 633,000 queue round trips on this case.
Its small incremental timing gain is less conclusive than the structural reduction
in queued work. Use `--inline-depth 0` for an ablation. Batching, sharding, and
numerical-kernel changes remain independently tunable or testable.

## Reproduce scaling measurements

```sh
python3 benchmarks/scaling.py ./build/knuth_search --bound 10000000000 \
  --threads 1 4 8 16 32 --repeats 3 > scaling.csv
```

Change `--batch-size`, `--shards`, `--inline-depth`, and `--bitmap-log2` one at a
time. The script checks counts across runs; the regression suite separately
compares complete sets. Search-only time excludes initialization and reporting;
wall time includes them. Full 64-bit ceilings have arithmetic regression coverage,
not a completed large-limit search or a feasibility guarantee.

## Validation recorded for this revision

- Release, forced serialized fallback, and address/undefined-behavior sanitizer tests passed locally.
- 12 exact small graph cases were checked under five worker/batch/bitmap/shard/inline configurations.
- 185 principal edges were compared with exact factorial/integer-root arithmetic.
- 105 additional large-input edges, including `UINT64_MAX`, agreed with independent 100-digit arithmetic.
  That last comparison used local `mpmath`; it is supplementary evidence, not a runtime dependency or a proof.

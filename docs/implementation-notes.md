# Implementation notes

## Search strategy

For every discovered `m`, evaluate the logarithm of `m!` using `lgammaq(m+1)`.
Select a power of two `2^p` that keeps the resulting candidate
approximately below the user-specified ceiling, then evaluate
`floor(exp(log(m!)/2^p))`, with `round` for `p=0` as in the original program.
Repeated integer square roots generate additional candidates. A work queue
expands each newly encountered candidate at most once.

This is a **heuristic numerical graph search**. Floating-point values extremely
close to integers can produce incorrect floors. Such boundaries require
additional precision and ultimately certified interval/exact evaluation.
The search does not claim mathematical exhaustiveness for the conjecture.

## Threading

The historical implementation accessed `vector<bool>` concurrently and treated
`try_dequeue` failure as termination. In the new implementation, setting a
visited bit is atomic; large values are guarded by a mutex. The pending-work
counter includes both queued and actively processed jobs. It is incremented
before a new item is enqueued and decremented after all descendants of the
current item have been scheduled. A temporarily unsuccessful dequeue does not
cause early worker termination.

Future performance work should benchmark sharded/worker-local queues and work
stealing, especially on NUMA machines, against this baseline.

## Dependency management

CMake `FetchContent` pins the exact upstream revision of Moodycamel's
`concurrentqueue`. It downloads source at configure time and can use CMake's
normal dependency cache for later/offline builds. The original third-party
header was *not* copied into the new source tree; see the historical repository
for the exact original snapshot.

## Historical measurements

The original source repository includes these files, whose first line is the
first undiscovered integer reported by a run (not an impossibility claim):

| File | First undiscovered integer | Wall-clock time |
| --- | ---: | ---: |
| `10Q.txt` | 378,835,118 | 659m 12.449s |
| `maxout.txt` | 419,916,105 | 921m 44.615s |

The historical source checked in with `nthreads=4`, whereas the recorded
user/real time ratios suggest these runs used different runtime or source
configuration. Reproducibility is therefore not established.

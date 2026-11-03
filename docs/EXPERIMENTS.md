# Initial measured demonstration

This is the complete default synthetic demonstration produced during the upgrade.
It is a reproducibility example, not evidence of a native hardware speedup or
generalization to arbitrary real applications. The model was trained for real;
these results are not hard-coded into the simulator or report generator.

## Configuration

- Seven synthetic workload families, each with distinct training, validation and test runs.
- 12,000 references per non-matrix trace; matrix dimensions 20/22/24 for train/validation/test.
- 64-byte lines, four ways, 4 KiB and 16 KiB caches.
- 256 warmup block accesses, write-back and write allocation.
- LRU, IGBI, LIP, SRRIP, ML and OPT: 84 completed simulation runs.
- A 256-access reuse horizon; final 256 samples per trace censored in training export.
- Deterministic 60,000-row training and validation reservoirs.
- Balanced logistic regression, 35 epochs, learning rate 0.2, L2 strength 0.001, seed 42.
- Threshold 0.45 selected by validation F1, without consulting test cache results.

## Validation classification

| Metric | Value |
| --- | ---: |
| Accuracy | 96.22% |
| Precision | 97.06% |
| Recall | 97.23% |
| F1 | 97.14% |

These are classification metrics on validation traces. Test cache results below
come from closed-loop simulation with frozen model weights.

## All policy miss counts at 4 KiB

| Test trace | LRU | IGBI | LIP | SRRIP | ML | OPT |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Hot working set + scan | 8,944 | 7,552 | 7,552 | 8,496 | 7,552 | 7,552 |
| Matrix, ordinary | 1,335 | 1,335 | 8,813 | 1,397 | 1,349 | 644 |
| Matrix, tiled | 438 | 438 | 480 | 435 | 440 | 304 |
| Changing phases | 5,715 | 5,725 | 5,690 | 5,713 | 5,685 | 4,681 |
| Pointer-like walk | 11,744 | 10,906 | 10,640 | 11,744 | 10,640 | 10,598 |
| Random references | 11,407 | 11,380 | 11,361 | 11,403 | 11,361 | 9,814 |
| Streaming | 11,744 | 11,744 | 11,744 | 11,744 | 11,744 | 11,744 |

## ML versus LRU, both capacities

Positive percentages mean fewer misses; negative percentages mean more misses.
Relative reduction is `(LRU misses - ML misses) / LRU misses`.

| Test trace | 4 KiB | 16 KiB |
| --- | ---: | ---: |
| Hot working set + scan | +15.56% | 0.00% |
| Matrix, ordinary | -1.05% | 0.00% |
| Matrix, tiled | -0.46% | 0.00% |
| Changing phases | +0.52% | +0.26% |
| Pointer-like walk | +9.40% | +37.41% |
| Random references | +0.40% | +0.24% |
| Streaming | 0.00% | 0.00% |

The unweighted arithmetic mean across these 14 trace/configuration pairs is **4.45%**.
This summary is specific to this workload mixture and must be accompanied by the
individual results. ML does not uniformly outperform IGBI, LIP or SRRIP. For example,
LIP matches several of its improvements, while always-low insertion severely hurts
the ordinary matrix trace. The learned policy largely preserves ordinary insertion
behavior on those matrix traces but still has small regressions against LRU.

The sampled model features generalize across different runs of the same synthetic
families here. A stronger generalization claim requires held-out workload families
and independent real Pin traces. The pointer-like synthetic trace models only
logical node references; real pointer code also executes loop, index and stack accesses.

## Reproduce and inspect

Run `make demo` in Linux, then inspect:

- `results/demo/report.html`: filterable offline tables and paired improvement bars.
- `results/demo/results.csv` and `results.json`: every measured run, including regressions.
- `results/demo/manifest.json`: exact commands, trace hashes, model hash and training provenance.
- `models/reuse.metrics.json`: classification metrics, feature weights and training parameters.

The supplied legacy `pinatrace.out` was separately checked for backward compatibility.
At its original 64 KiB / 128-byte / 16-way LRU configuration, the upgraded simulator
reproduces 223,546 hits, 2,591 misses, 2,079 evictions and 435 write-backs (98.85% hits).
The original target source is missing, so that trace is a compatibility diagnostic,
not a reproducible real-workload training benchmark.

## Costs and remaining work

The exported model has eight weights and a bias, but its bounded software histories
occupy 2,883,672 bytes including the model object in this build. This is substantial
relative to the simulated caches. Future work should evaluate smaller tables, collision
rates and the resulting miss/overhead tradeoff. Single-run simulator durations appear
in the report; repeated measurements are required for reliable timing conclusions.

Next practical experiments: capture ordinary/tiled matrix and pointer workloads in
Linux with Pin; train on separate runs; evaluate a workload family excluded from
training; measure model-table ablations and paired simulator timing distributions.

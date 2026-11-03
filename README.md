# CacheLab

**Trace-Driven Data Cache Simulation with Machine-Learned Reuse Prediction**

CacheLab studies a practical systems question: can a lightweight learned reuse
predictor reduce cache pollution compared with LRU and an instruction-based heuristic?
It combines a C++17 simulator, real-program tracing with Intel Pin, reproducible
synthetic workloads, a Python logistic-regression trainer, and offline experiment reports.

The original project has been upgraded in this same folder. The old source, traces,
reports and executable were copied to `backups/before-ml-cea4f4fd/` before editing.
The two original course PDFs are historical submissions; the updated proposal is
`PROJECT_PROPOSAL.md`.

## Run on Linux or WSL

Requirements: Linux x86-64, GCC 9+ or Clang with C++17 support, Python 3.10+, and Make.
The simulator, training and reporting need **no third-party Python packages**.
Intel Pin is optional and is needed only to capture real-program traces.

```bash
cd /mnt/e/Trace-Driven-Memory-Cache-Simulator-and-Performance-Analyzer
# Install missing build tools on Ubuntu/WSL if necessary:
# sudo apt update && sudo apt install build-essential python3 cmake
make
make test
make demo
```

Open `results/demo/report.html` in a browser. The report is self-contained and works
offline. `make demo` generates three trace splits, trains a real model, and evaluates
the test split across six policies and two cache sizes. It takes approximately a minute
on a typical development machine; the exact duration depends on hardware.

For a shorter pipeline check: `python3 run_demo.py --quick`.
The quick run overwrites the same generated model/results and is a smoke experiment.
Run `make demo` again to restore the full demonstration.

Alternative build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
python3 run_demo.py --simulator ./build/cache_simulator
```

## What changed

- Ten comparison policies: LRU, FIFO, Random, LFU, MRU, Belady OPT, IGBI, LIP, SRRIP and ML.
- A real, trained logistic model controls insertion priority using eight causal features.
- The model is trained in Python and evaluated directly in C++; no Python process is
  launched on a simulated access.
- Strict, backward-compatible trace parsing, sized accesses and cache-line crossings.
- Read/write misses, write-back traffic, byte counts, warmup, limits, JSON and estimated AMAT.
- Online policies stream traces; only OPT requires future-trace storage.
- Separate training/validation/test runs, exact-file leakage checks and SHA-256 provenance.
- Automated configuration sweeps, CSV/JSON results and an HTML comparison report.
- Actual C++ workloads, an optional ROI-aware Pin tool, and regression tests.

## Simulator examples

```bash
./cache_simulator --trace pinatrace.out --policy lru --size 32768 --block 64 --assoc 8
./cache_simulator --trace traces/test/hot_scan.trace --policy ml \
  --model models/reuse.model --size 4096 --block 64 --assoc 4 --json
./cache_simulator --trace traces/test/random.trace --policy lru \
  --write wt --alloc nwa --warmup 256 --limit 10000 --json
./cache_simulator --help
```

Cache size, line size and associativity must be positive powers of two with an
integral number of sets. Models record their training line size; inference rejects
a different size. Re-train for another line size.

Trace formats (addresses and PCs may be decimal or `0x` hexadecimal):

```text
# Legacy traces remain supported; omitted byte sizes mean one byte.
R 0x1000
W 0x400120 0x1000
# Sized data accesses: type, instruction PC, data address, bytes.
R 0x400100 0x103c 16
```

The last example touches two 64-byte cache lines. Statistics count **block accesses**,
so one operand may contribute multiple accesses. The instruction PC is predictor
context; it does not represent an instruction-cache fetch. Only data caches are modeled.

## Train and evaluate

```bash
python3 generate_workloads.py --output traces
python3 train_model.py --simulator ./cache_simulator \
  --train traces/train --validation traces/validation \
  --output models/reuse.model --horizon 256 --epochs 35
python3 benchmark.py ./cache_simulator traces/test --model models/reuse.model \
  --sizes 4096 16384 65536 --assocs 2 4 8 --output results/sweep
```

The trainer exports features from C++, creates binary reuse labels, fits balanced
logistic regression using minibatch gradient descent, and selects a classification
threshold using validation F1. A label is 1 if the block reappears within the next K
block accesses. The final K accesses are censored because a complete horizon is missing.
Future accesses are used for training labels only. They never enter inference features.

Features capture historical instruction reuse, instruction frequency, block recency,
block frequency, same-block accesses, repeated strides, stride magnitude and read/write type.
Fixed-size hashed histories bound predictor memory; collisions forget past history.
Weights and the threshold are frozen during test simulation, while causal histories
continue updating. See `docs/DESIGN.md` for exact semantics and limitations.

Training outputs:

- `models/reuse.model`: portable, versioned text weights for the C++ predictor.
- `models/reuse.metrics.json`: model hash, feature names, parameters, training trace hashes
  and classification metrics. Preserve it beside the model for benchmark provenance.

The benchmark rejects exact copies of training/validation traces, verifies the model
hash and records every simulator command. Use genuinely separate input runs;
hash checks alone cannot detect overlapping segments or correlated workloads.
Report cache miss reduction and overhead alongside classification metrics.

## Capture real traces with Intel Pin

The bundled kit targets Linux; execute it from Linux/WSL, not PowerShell.

```bash
export PIN_ROOT="$PWD/pin-3.28-98749-g6643ecee5-gcc-linux"
make target_workload
bash scripts/capture_trace.sh matrix 32 traces/real/matrix.trace
bash scripts/capture_trace.sh tiled 32 traces/real/tiled.trace
bash scripts/capture_trace.sh pointer 64 traces/real/pointer.trace
python3 benchmark.py ./cache_simulator traces/real --model models/reuse.model \
  --output results/real
```

The helper builds `pin_trace.so` using the kit's own Makefiles and traces between
the workload's `cachelab_roi_begin`/`cachelab_roi_end` markers. Initialization is excluded.
The Pin tool also supports whole-program tracing by omitting `-roi 1`.
Ordinary read/write memory operands are recorded with their sizes. Specialized
gather/scatter operations and REP behavior require separate validation before using
this tracer for arbitrary instruction sets. Pin's compatibility depends on the
installed Linux kernel/glibc; consult the kit documentation if it fails to start.

For real training experiments, capture distinct runs and input sizes into separate
train, validation and test folders, then pass those folders to the same trainer.
Do not describe the supplied synthetic demonstration as a real-program performance result.

For native timing, run the target **without Pin**:

```bash
./target_workload matrix 128
./target_workload tiled 128
```

Both versions compute the same result and print a checksum and elapsed seconds.
Use repeated runs when making native runtime claims.

## Metrics and interpretation

- `hit_rate` and `miss_rate` in JSON are fractions; the text interface prints percentages.
- `AMAT = hit_cycles + miss_rate * miss_penalty` is a configurable first-order estimate.
  It does not model out-of-order overlap, write buffers, TLBs, prefetchers or contention.
- Write-through and no-write-allocate transactions count the accessed bytes. Fills and
  dirty write-backs transfer full lines. No final dirty flush occurs unless `--flush` is used;
  `flush_write_backs` is reported separately from eviction write-backs.
- Warmup changes cache and predictor state without counting its accesses or traffic.
  Evictions during measurement can write back lines dirtied during warmup.
- `--warmup` and `--limit` count split block accesses, including partial operands.
- OPT is a future-aware reference, restricted to write allocation. Its cold-start
  full-trace miss count is optimal under mandatory admission and identical geometry.
  Policy-specific warmup states can change comparisons over the measured suffix.
- Simulation time includes parsing and policy work, plus OPT preprocessing. Cache/model
  initialization is excluded. Single-run simulator timing does not establish native speedup.
- The feature histories currently occupy about 2.75 MiB of software state; the weights
  are small. This is a research simulator, not a demonstrated hardware implementation.

## Repository guide

```text
cache_simulator.cpp      Command line, validation and metrics
include/cachelab/        Trace parsing, cache policies, causal features, model and labels
train_model.py           Dependency-free logistic training and provenance
generate_workloads.py    Explicitly synthetic logical-reference trace generator
benchmark.py            Experiment sweeps and offline report
run_demo.py              End-to-end reproducible demonstration
target_workload.cpp     Real computations with tracing markers
pin_trace.cpp           Linux Intel Pin tracer
pin/                    Pin build integration
scripts/capture_trace.sh Real-trace capture helper
test_driver.py          Semantic, causal-label and ML integration regression tests
PROJECT_PROPOSAL.md      Updated project idea and evaluation plan
docs/                   Design, measured demonstration and verification notes
```

## Research context

IGBI and the learned insertion policy relate to instruction-signature reuse prediction.
The project is an educational implementation and experimental comparison of these ideas.

- [SHiP: Signature-Based Hit Predictor](https://mrmgroup.cs.princeton.edu/papers/MICRO11_SHiP_Wu_Final.pdf)
- [Perceptron Learning for Reuse Prediction](https://par.nsf.gov/servlets/purl/10042123)
- [Applying Deep Learning to the Cache Replacement Problem](https://www.cs.utexas.edu/~lin/papers/micro19c.pdf)
- [Cachegrind simulation details and limitations](https://valgrind.org/docs/manual/cg-manual.html)

Original project members: Aryan Sandal (2024CSB1103) and Moinak Goswami (2024CSB1225).

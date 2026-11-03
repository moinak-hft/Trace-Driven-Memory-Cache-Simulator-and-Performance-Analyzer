# Trace-Driven Data Cache Simulator with Machine-Learned Reuse Prediction

## Problem and motivation

Cache replacement policies make decisions from partial information about future reuse.
LRU works well for some locality patterns but can discard useful blocks during streaming
scans or working-set changes. A trace-driven simulator makes these effects observable
and enables controlled comparisons under identical cache configurations.

This project investigates whether a compact learned predictor can identify useful
incoming blocks and improve their insertion priority compared with LRU and the
existing instruction-guided insertion heuristic (IGBI).

## Research question

Can a logistic-regression model trained on past-run traces reduce cache misses and
estimated memory access cost on separate runs, and what software overhead does it add?

## Objectives

1. Implement configurable data-cache simulation in C++17 from scratch.
2. Compare standard policies, scan-resistant insertion policies, IGBI and Belady OPT.
3. Extract causal memory-history features and generate finite-horizon reuse labels.
4. Train a model in Python, export its weights, and evaluate it directly in C++.
5. Measure closed-loop cache behavior on separate training, validation and test runs.
6. Support real-program trace capture through Linux Intel Pin and explicit computation markers.
7. Produce reproducible experiment tables, graphs, provenance and regression tests.

## Proposed learned policy

The predictor estimates whether an accessed block will be used again within K block
accesses. Its eight features summarize past instruction and block behavior. A block
with predicted probability below a validation-selected threshold is inserted at the
least-recently-used position; other blocks enter at the most-recently-used position.
Hits promote blocks normally. Every allocating miss still admits its incoming block.

This isolates the contribution of learned insertion while preserving an understandable
replacement mechanism. The model's weights remain fixed during evaluation; historical
feature state evolves causally as the test trace is consumed.

## Experimental method

- Workloads: streaming scans, reused working sets mixed with scans, pointer-like walks,
  random accesses, changing phases, ordinary matrix multiplication and tiled multiplication.
- Data sources: reproducible synthetic logical-reference traces for controlled experiments,
  plus separately captured real executable traces for practical validation.
- Comparisons: LRU, FIFO, Random, LFU, MRU, OPT, IGBI, LIP, SRRIP and ML.
- Configurations: cache capacity and associativity sweeps, with separately trained models
  when changing the line size.
- Splits: distinct runs/input sizes for training, validation and testing. Related windows
  from one run remain together; held-out workload families are a further generalization study.
- Metrics: misses, relative miss reduction, read/write traffic, estimated AMAT, classification
  precision/recall/F1, simulation time and predictor software memory.
- Fairness: identical trace and geometry for paired comparisons, recorded seeds for Random,
  no future information at test-time inference, and explicit reporting of regressions.

## Deliverables

The simulator, trainer, versioned model format, workload generator, Pin integration,
automated tests, build files, experiment CSV/JSON, self-contained HTML report and
technical documentation are implemented in this repository.

## Scope and limits

The project remains a single-level trace-driven data-cache study. It does not model
multicore coherence, physical address translation, speculative execution, realistic
hardware timing or neural prefetching. Simulated AMAT is an estimate. A trained model
is an experimental policy with measurable tradeoffs, not a promise of universal improvement.

## Contribution and related work

The contribution is the reproducible integration and evaluation of learned insertion
against explicit heuristic baselines. Instruction-signature reuse prediction and
learned cache management have prior literature, including SHiP and perceptron reuse
prediction. The documentation cites these sources and distinguishes this implementation
from a claim of a newly invented cache-replacement principle.

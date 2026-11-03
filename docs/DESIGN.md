# Implementation and experimental semantics

## Data path

`trace -> strict parser -> sized block accesses -> causal history -> probability -> cache -> counters`

The online policies retain cache state and fixed-size metadata, not the input trace.
OPT additionally retains the selected trace interval and future occurrence queues.
Dataset export uses a queue of at most K+1 samples and an active-block map bounded
by the horizon. It can label arbitrarily long traces without loading them in full.

## Cache representation

Each set contains valid/dirty bits, the full block number, insertion time, access
frequency, a reuse signature, an LRU rank and a two-bit SRRIP value. The set is
`(address / line_size) % number_of_sets`. Unique ranks run from LRU=0 to MRU=n-1.
Insertion at LRU shifts surviving ranks; hits promote the accessed line to MRU.
Every allocating miss selects an existing victim before admitting its incoming line.

IGBI uses 16,384 saturating counters keyed by a hash of the insertion instruction PC.
Hits train the insertion signature upward; unused evictions train it downward.
Counter zero gives low insertion priority. The ML policy replaces this insertion
decision with a logistic probability, leaving the LRU victim and hit mechanisms intact.
LIP is the always-low insertion ablation; ordinary LRU is the always-high ablation.
SRRIP provides a separate established scan-resistant comparison.

## Causal feature schema: causal-v1

All features are computed before incorporating the current access into the histories.
PC identity is hashed to select past-state entries; raw address values are not numeric
inputs to the model.

| Feature | Definition |
| --- | --- |
| pc_repeat_rate | Laplace-smoothed fraction of this PC's prior accesses whose block was seen in the preceding K accesses |
| pc_frequency | min(1, log2(1 + prior PC accesses) / 16) |
| block_recency | max(0, 1 - distance_since_previous_block_access / K), zero when unseen |
| block_frequency | min(1, log2(1 + prior block accesses) / 8) |
| same_block | Current block equals the last block accessed by this PC |
| stable_stride | Current signed stride matches the previous signed stride for this PC |
| stride_magnitude | min(1, log2(1 + absolute block stride) / 16), zero for unseen PC |
| is_write | One for a write, zero for a read |

The histories use 65,536 direct-mapped block entries and 16,384 direct-mapped PC entries.
Collisions replace an entry and forget its history. The simulator reports their actual
software allocation size, around 2.75 MiB on this build, separately from cache capacity.
This overhead is part of the tradeoff and requires optimization before hardware claims.

## Labels, fitting and inference

For access i with block b, `label_i = 1` if b occurs at any position i+1 through i+K.
Features are retained in a bounded queue and labels are resolved after exactly K future
accesses have been observed. The last K samples are excluded to avoid false negative
labels from trace truncation. A label horizon is measured in split block accesses,
not CPU instructions, bytes, cache misses or cycles.

The Python trainer samples rows with deterministic reservoir sampling, applies
balanced class weighting, and fits logistic regression with L2 regularization and
minibatch gradient descent. No preprocessing is fitted on validation/test data.
Threshold selection maximizes F1 on validation runs. F1 is a classification criterion;
the policy must be separately evaluated for miss reduction and traffic.

The exported version-1 model contains a schema identifier, feature count, line size,
horizon, threshold, bias and eight weights. C++ validates these values and computes
the same numerically stable sigmoid used by Python. Version/schema/line mismatches
and non-finite weights are errors. The companion metrics file records hashes and parameters.

In test simulation, weights and the threshold remain frozen. Past-only feature state
continues updating. Classification predictions are made for all accesses; insertion
priority matters only on allocating misses. No-write-allocate write misses bypass admission.

## Traffic and timing

An allocating miss reads a full line. Write-through accesses issue byte-sized writes;
write-back accesses mark a line dirty and write a full line when it is evicted.
No-write-allocate write misses issue only a write. A read-modify-write instruction
may be represented by separate R and W records. A cross-line operand is split into
multiple block accesses; this counting convention differs from tools that count an
entire operand as one possible miss.

Warmup changes replacement state, histories, random-generator state and dirty bits.
Its counters are excluded. Optional final flushing reports flush write-backs separately.
The simple AMAT equation does not include extra dirty-victim latency or overlap.
Measured simulator seconds include parsing and policy execution, plus OPT preprocessing,
and exclude construction/model-file loading. Use repeated timings for overhead studies.

## Validation

Regression tests check byte ranges, writes, warmup/limits, invalid input, deterministic
random replacement, IGBI scan behavior, ML insertion extremes, causal features, exact
reuse labels, C++/Python inference parity and overlap rejection. A separate dynamic
program independently checks OPT miss counts for small random traces. Matrix workload
checksums verify the optimized and ordinary computations agree.

The Linux CI workflow builds and runs the regression suite and a short ML experiment.
The included Pin source uses the local kit's documented APIs; executing Pin is a
separate environment-dependent verification step.

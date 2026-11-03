# Cache Simulator Project

A cache-simulation toolkit with:

- a configurable cache simulator,
- an Intel Pin-based memory tracer,
- a synthetic workload generator,
- a test driver for validation, and
- a benchmark script for comparing replacement policies.

---

## Overview

This project traces memory accesses from a target program and feeds them into a cache simulator. The simulator supports multiple replacement, write, and allocation policies, making it useful for studying cache behavior under different configurations.

The main components are:

- `cache_simulator.cpp` ΓÇö the cache simulator core
- `pin_trace.cpp` ΓÇö Intel Pin tool that records memory reads/writes
- `target_workload.cpp` ΓÇö sample program used to generate memory activity
- `test_driver.py` ΓÇö automated correctness checks
- `benchmark.py` ΓÇö runs the simulator across several policies and summarizes the results

---

## Features

- Replacement policies: `lru`, `fifo`, `random`, `lfu`, `mru`, `optimal`, `igbi`
- Write policies: `wb`, `wt`
- Allocation policies: `wa`, `nwa`
- Trace parsing from Intel Pin output
- Statistics reporting:
  - hit rate
  - miss rate
  - evictions
  - write backs
- Benchmarking across multiple policies using the same trace

---

## How It Works

1. `target_workload` runs under Intel Pin.
2. `pin_trace.so` records each memory read/write into a trace file.
3. `cache_simulator` consumes that trace and simulates cache behavior.
4. `test_driver.py` verifies expected results on small controlled cases.
5. `benchmark.py` compares policies on a larger trace and prints a summary table.

---

## Repository Structure

```text
.
Γö£ΓöÇΓöÇ benchmark.py
Γö£ΓöÇΓöÇ cache_simulator.cpp
Γö£ΓöÇΓöÇ pin_trace.cpp
Γö£ΓöÇΓöÇ target_workload.cpp
Γö£ΓöÇΓöÇ test_driver.py
ΓööΓöÇΓöÇ README.md
```

---

## Simulator Options

The simulator accepts the following command-line options:

- `--size <bytes>`: total cache size
- `--block <bytes>`: block size
- `--assoc <ways>`: associativity
- `--policy <name>`: cache replacement policy
- `--write <wb|wt>`: write-back or write-through
- `--alloc <wa|nwa>`: write-allocate or no-write-allocate
- `--trace <file>`: trace file path

Example:

```bash
./cache_simulator --size 65536 --block 128 --assoc 16 --policy igbi --write wb --trace pinatrace.out
```

---

## Output Format

The simulator prints:

- `Hit Rate`
- `Miss Rate`
- `Evictions`
- `Write Backs`

This format is used directly by both `benchmark.py` and `test_driver.py`.

---

## Automated Testing

Run the test driver to validate the simulator on a few known cases:

```bash
python3 test_driver.py ./cache_simulator
```

The test driver checks:

- basic hit/miss behavior,
- thrashing behavior,
- write policy behavior,
- and the IGBI path.

---

## Benchmarking

Run the benchmark script to compare policies on a trace:

```bash
python3 benchmark.py ./cache_simulator pinatrace.out
```

The script evaluates:

- `LRU`
- `FIFO`
- `RANDOM`
- `LFU`
- `MRU`
- `OPTIMAL`
- `IGBI`

and prints the best-performing realistic policy based on hit rate.

---

## Compilation Guide

Follow these steps to compile and run the project successfully:

```bash
g++ -O3 target_workload.cpp -o target_workload
g++ -O3 -std=c++17 cache_simulator.cpp -o cache_simulator

cd pin-3.28-98749-g6643ecee5-gcc-linux/source/tools/MyPinTool
make obj-intel64/pin_trace.so TARGET=intel64
cd ../../../../

cd /workspaces/CS-204_Project
cp pin_trace.cpp pin-3.28-98749-g6643ecee5-gcc-linux/source/tools/MyPinTool/
cd pin-3.28-98749-g6643ecee5-gcc-linux/source/tools/MyPinTool
make obj-intel64/pin_trace.so TARGET=intel64
cd ../../../../

./pin-3.28-98749-g6643ecee5-gcc-linux/pin -t pin-3.28-98749-g6643ecee5-gcc-linux/source/tools/MyPinTool/obj-intel64/pin_trace.so -- ./target_workload

python3 test_driver.py ./cache_simulator

python3 benchmark.py ./cache_simulator pinatrace.out

./cache_simulator --size 65536 --block 128 --assoc 16 --policy igbi --write wb --trace pinatrace.out
```

### Notes

- Make sure the Intel Pin directory name matches your local installation.
- If your project folder is different from `/workspaces/CS-204_Project`, update that path before copying `pin_trace.cpp`.
- The trace file produced by the Pin tool is typically `pinatrace.out`.

---

## Example Workflow

```bash
# 1) Build the target workload and simulator
./target_workload

# 2) Generate a trace using Pin
./pin-3.28-98749-g6643ecee5-gcc-linux/pin -t pin-3.28-98749-g6643ecee5-gcc-linux/source/tools/MyPinTool/obj-intel64/pin_trace.so -- ./target_workload

# 3) Validate the simulator
python3 test_driver.py ./cache_simulator

# 4) Compare policies
python3 benchmark.py ./cache_simulator pinatrace.out
```

---

## Acknowledgements

This project uses Intel Pin for dynamic binary instrumentation and trace generation.

## Project Member's.
   1) Aryan Sandal 2024CSB1103  
   2) Moinak Goswami 2024CSB1225

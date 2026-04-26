# ⚡ Trace-Driven Memory Cache Simulator & Performance Analyzer

A high-performance, deterministic memory cache simulation engine built for deep architectural analysis and quantitative workload evaluation. 

This project leverages Intel Pin for dynamic binary instrumentation (DBI) to extract granular memory access patterns at runtime, and feeds those traces into a highly optimized, dynamically configurable C++17 simulator. The engine is designed from first principles to evaluate memory replacement policies, ranging from standard deterministic logic to omniscient oracles and custom thrash-resistant algorithms.

**Developed for CS204 by:**
* Aryan Sandal (2024CSB1103) 
* Moinak Goswami (2024CSB1225)

---

## 🏗️ Core Architectural Components

The system architecture is strictly decoupled into three highly specialized pipelines, isolating the overhead of trace generation from the speed of simulation.

### 1. Dynamic Binary Instrumentation (Intel Pin)
The trace generator (`pin_trace.cpp`) utilizes Intel Pin's Just-In-Time (JIT) compiler infrastructure. 
* **Mechanism:** It intercepts instructions at the basic block level before CPU execution. Using `INS_MemoryOperandIsRead` and `INS_MemoryOperandIsWritten`, it identifies memory boundaries.
* **Overhead Mitigation:** Instead of analyzing every instruction, it injects predicated calls (`INS_InsertPredicatedCall`) strictly on memory operands, capturing the Effective Address (EA) and Instruction Pointer (IP).
* **I/O Streaming:** Raw access traces are buffered and streamed sequentially to disk (`pinatrace.out`) to prevent I/O blocking during target application execution.

### 2. High-Frequency Simulation Engine
The `cache_simulator` is a native C++ binary modeling an N-way set-associative hardware cache. 
* **Bitwise Address Translation:** Floating-point division is entirely eliminated from the hot path. The engine calculates bit-shifts during initialization ($Offset Bits = \log_2(BlockSize)$) and utilizes heavily optimized bitwise masks to isolate Tags and Indexes in $O(1)$ time.
* **Zero-Allocation Hot Path:** The cache grid is pre-allocated as a continuous block of memory via `std::vector<vector<Line>>`. During simulation, no dynamic memory is allocated or freed, ensuring strictly deterministic execution times even across multi-gigabyte trace files.

### 3. Quantitative Validation Harness
A Python test driver (`benchmark.py`) automates the execution of the compiled C++ binary, piping various geometric configurations and replacement policies into the engine. It parses the standard output via regex to generate a comparative, tabular matrix of hit/miss ratios, write-backs, and eviction counts.

---

## 🧮 Configurable Cache Geometries

The simulator allows for command-line manipulation of the physical cache geometry, enabling sensitivity analysis across different hardware constraints:

* **`--size` (Capacity):** Total bytes allocated to the simulated cache.
* **`--block` (Line Size):** The spatial locality fetch size (typically 64 bytes).
* **`--assoc` (Associativity):** The number of ways per set (e.g., 1 for Direct Mapped, 8 for 8-way, or N for Fully Associative).
* **`--write` (Write Policy):** * `wb` (Write-Back): Defers memory writes until eviction, utilizing a `dirty` bit.
  * `wt` (Write-Through): Commits writes to memory immediately.
* **`--alloc` (Allocation Policy):** * `wa` (Write-Allocate): Fetches the block on a write miss.
  * `nwa` (No-Write-Allocate): Bypasses the cache on a write miss.

---

## 🧠 Algorithmic Implementations & Time Complexity

The engine implements multiple replacement policies, managing victim selection when a cache set reaches capacity.

### Standard Deterministic Policies
* **LRU (Least Recently Used):** Evaluated using a monotonically increasing 64-bit clock tick. On a hit, the block's `last_used` timestamp is updated. *Time Complexity: $O(Associativity)$ for victim search.*
* **FIFO (First-In, First-Out):** Utilizes an immutable `inserted` timestamp captured when the block enters the cache. *Time Complexity: $O(Associativity)$.*
* **MRU (Most Recently Used):** Evicts the block with the highest `last_used` tick. Designed for specific cyclic access patterns. *Time Complexity: $O(Associativity)$.*
* **LFU (Least Frequently Used):** Tracks raw access counts via a `freq` variable. To prevent cache pollution from stale ties (blocks used heavily once and never again), LFU strictly falls back to LRU logic as a secondary tie-breaker. *Time Complexity: $O(Associativity)$.*
* **RANDOM:** Utilizes `<random>` with a deterministic seed (`mt19937_64`) to uniformly select a victim index. Serves as a low-overhead baseline. *Time Complexity: $O(1)$.*

### Advanced & Omniscient Policies
* **OPTIMAL (Belady's MIN Algorithm):** Establishes the absolute theoretical mathematical ceiling for cache hit rates on a given trace.   
  * **Mechanism:** It operates via a two-pass oracle mechanism. The `preprocess_optimal` pass maps all future accesses into a dynamic hash map of deques (`unordered_map<uint64_t, deque<uint64_t>>`).   
  * **Victim Selection:** During the simulation pass, it scans the cache set and evicts the block whose next required access is furthest in the absolute future (or `ULLONG_MAX` if it will never be accessed again).  
  * **Space Complexity:** $O(N)$ where N is the total number of memory accesses in the trace.
* **CUSTOM (Frequency-Shielded LRU):** A proprietary, thrash-resistant algorithm built specifically to defeat cache pollution caused by massive linear disk scans (e.g., directory traversals in `/bin/ls` or matrix streaming).   
  * **Mechanism:** It establishes a **Frequency Shield**. Any cache line with a frequency $> 1$ is mathematically protected.   
  * **Victim Selection:** When the set is full, the algorithm performs a dual-scan. It aggressively targets unshielded blocks ($frequency = 1$) for eviction, using LRU to break ties among them. Shielded blocks are only vulnerable if the entire set is shielded. This completely isolates valuable temporal data (stack frames, active pointers) from being overwritten by one-hit-wonder array streams.

---

## 📊 Anatomy of a Simulated Cache Line

To minimize memory overhead and replicate hardware structures, the internal `Line` struct is heavily compressed:

```cpp
struct Line {
    bool valid;          
    bool dirty;          
    uint64_t block;      
    uint64_t inserted;   
    uint64_t last_used;  
    uint64_t freq;       
};
```
---

## ⚙ Compilation & Execution Guide

## Prerequisites
- Intel Pin 3.28 (Linux)
- GCC/G++ compiler with C++17 standard support
- Python 3.x for benchmarking

## Step 1: Trace Generation
Move the instrumentation code into the Intel Pin hierarchy, compile the `.so` shared object, and execute it against a target workload.

```bash
cp pin_trace.cpp pin-3.28-98749-g6643ceee5-gcc-linux/source/tools/MyPinTool/MyPinTool.cpp
cd pin-3.28-98749-g6643ceee5-gcc-linux/source/tools/MyPinTool
make obj-intel64/MyPinTool.so TARGET=intel64
cd ../../../../
./pin-3.28-98749-g6643ceee5-gcc-linux/pin -t pin-3.28-98749-g6643ceee5-gcc-linux/source/tools/MyPinTool/obj-intel64/MyPinTool.so -- pinatrace.out
```

## Step 2: Engine Compilation
Compile the simulation engine natively. Maximum level-3 optimization (-O3) is mandatory for traversing datasets with millions of access vectors efficiently.

```bash
g++ -O3 -std=c++17 cache_simulator.cpp -o cache_simulator
```

## Step 3: Automated Validation Benchmarking
Execute the Python wrapper to run sensitivity analysis across all algorithms. The script automatically pipes the generated pinatrace.out through the compiled binary.

```bash
python3 benchmark.py
```

### Manual Execution Example
To bypass the benchmark wrapper and test a highly specific cache geometry:
```bash
./cache_simulator --size 65536 --block 128 --assoc 16 --policy custom --write wb --alloc wa pinatrace.out
```

## Notes:-
*  pinatrace.out is the trace file generated by Intel Pin instrumentation.
*  The simulator binary is built from cache_simulator.cpp.
*  The benchmark script automates multiple runs and validation checks.

# Trace-Driven Memory Cache Simulator

A high-performance, offline memory cache simulator built for deep architectural analysis. This project leverages dynamic binary instrumentation (DBI) to extract granular memory access patterns and evaluates them against a highly configurable C++17 simulation engine.

Developed for CS204 by Aryan Sandal (2024CSB1103) and Moinak Goswami (2024CSB1225).

---

## 🚀 Project Implementation Phases

This repository contains the complete implementation of the project milestones:

* **Phase 1: Environment Setup & Trace Generation:** A custom Intel Pin Tool (`pin_trace.cpp`) utilizing a Just-In-Time (JIT) compiler to intercept memory read/write operands and generate raw execution traces.
* **Phases 2 & 3: Base Simulator & Optimal Policies:** A native C++ engine (`cache_simulator.cpp`) modeling an N-way set-associative architecture, including standard replacement policies and a two-pass Belady's MIN optimal baseline.
* **Phase 4/5: Custom Policy Innovation:** The implementation of a proprietary "Frequency-Shielded LRU" algorithm designed to outperform standard temporal policies on scan-heavy workloads.

---

## 🧠 Cache Replacement Policies

The simulator evaluates memory traces against the following deterministic and omniscient replacement algorithms. 

### Standard Policies
* **LRU (Least Recently Used):** Evaluated using dynamic temporal timestamps. Every cache hit updates the block's internal clock tick. During eviction, the block with the absolute lowest clock tick is removed, operating on the assumption of temporal locality.
* **FIFO (First-In, First-Out):** Utilizes an immutable `inserted` timestamp. Evicts the oldest block residing in the cache set, entirely ignoring intermediate access frequency or recent hits.
* **LFU (Least Frequently Used):** Tracks raw access counts via a `freq` variable. Evicts the block with the lowest access frequency. To prevent cache pollution from stale ties, LFU utilizes LRU as a secondary tie-breaker.
* **MRU (Most Recently Used):** The inverse of LRU. Evicts the block with the highest `last_used` tick. Designed for specific cyclic access patterns where the oldest data is statistically the most likely to be needed next.
* **RANDOM:** Utilizes the `<random>` library's `mt19937_64` generator (seeded for determinism) to uniformly select a victim index. Serves as a performance baseline.

### Advanced Policies
* **OPTIMAL (Belady's MIN):** Establishes the theoretical mathematical ceiling for hit rates. It operates via a two-pass mechanism: an initial pass maps all future accesses into a dynamic hash map of queues. During the simulation pass, it scans the cache set and evicts the block whose next required access is furthest in the absolute future.
* **CUSTOM (Frequency-Shielded LRU):** A proprietary algorithm built to defeat cache pollution caused by massive linear disk scans (e.g., directory traversals). It establishes a **Frequency Shield** for any block with a hit count $> 1$. During eviction, the algorithm prioritizes unshielded, one-hit-wonder blocks, exclusively falling back to standard LRU logic to break ties.

---

## ⚙️ Build & Execution Guide

### 1. Generate the Memory Trace
Move the trace script into your local Pin directory structure, compile the shared object library, and execute it against a target binary.

```bash
cp pin_trace.cpp pin-3.28-98749-g6643ecee5-gcc-linux/source/tools/MyPinTool/MyPinTool.cpp
cd pin-3.28-98749-g6643ecee5-gcc-linux/source/tools/MyPinTool
make obj-intel64/MyPinTool.so TARGET=intel64
cd ../../../../
./pin-3.28-98749-g6643ecee5-gcc-linux/pin -t pin-3.28-98749-g6643ecee5-gcc-linux/source/tools/MyPinTool/obj-intel64/MyPinTool.so -- /bin/ls

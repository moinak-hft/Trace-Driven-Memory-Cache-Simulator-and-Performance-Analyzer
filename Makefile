CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic
PYTHON ?= python3
HEADERS := $(wildcard include/cachelab/*.hpp)
.PHONY: all test demo traces train clean
all: cache_simulator target_workload
cache_simulator: cache_simulator.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@
target_workload: target_workload.cpp
	$(CXX) $(CXXFLAGS) $< -o $@
test: all
	$(PYTHON) test_driver.py ./cache_simulator --workload ./target_workload
traces:
	$(PYTHON) generate_workloads.py --output traces
train: cache_simulator traces
	$(PYTHON) train_model.py --simulator ./cache_simulator --train traces/train --validation traces/validation --output models/reuse.model
demo: all
	$(PYTHON) run_demo.py --simulator ./cache_simulator
clean:
	$(RM) cache_simulator target_workload

#pragma once
#include "model.hpp"
#include <memory>
#include <random>
#include <unordered_map>
#include <deque>

namespace cachelab {
struct Config {
    u64 size = 32768, block = 64, assoc = 8, warmup = 0, limit = 0, seed = 1337;
    double hit_cycles = 1, miss_penalty = 100;
    std::string policy = "lru", write = "wb", alloc = "wa", model_path;
    bool flush = false;
    double threshold = -1;
};
struct Stats {
    u64 total_accesses = 0, hits = 0, misses = 0, read_accesses = 0, write_accesses = 0;
    u64 read_misses = 0, write_misses = 0, evictions = 0, write_backs = 0, flush_write_backs = 0;
    u64 memory_reads = 0, memory_writes = 0, memory_read_bytes = 0, memory_write_bytes = 0;
    u64 ml_predictions = 0, ml_low_priority_insertions = 0, warmup_accesses = 0;
};
struct Line {
    bool valid = false, dirty = false;
    u64 block = 0, pc = 0, inserted = 0, frequency = 0, rank = 0;
    unsigned rrpv = 3;
};

class Cache {
    Config c;
    std::vector<std::vector<Line>> sets;
    std::vector<unsigned char> predictor;
    std::unique_ptr<FeatureTracker> history;
    Model model;
    std::mt19937_64 rng;
    std::unordered_map<u64, std::deque<u64>> future;
    u64 tick = 0;

    std::size_t victim(std::vector<Line>& set) {
        if (c.policy == "random") return std::uniform_int_distribution<std::size_t>(0, set.size()-1)(rng);
        if (c.policy == "srrip") {
            while (true) {
                for (std::size_t i = 0; i < set.size(); ++i) if (set[i].rrpv == 3) return i;
                for (auto& line : set) ++line.rrpv;
            }
        }
        std::size_t result = 0;
        for (std::size_t i = 1; i < set.size(); ++i) {
            bool choose = false;
            if (c.policy == "fifo") choose = set[i].inserted < set[result].inserted;
            else if (c.policy == "mru") choose = set[i].rank > set[result].rank;
            else if (c.policy == "lfu") choose = set[i].frequency < set[result].frequency ||
                (set[i].frequency == set[result].frequency && set[i].rank < set[result].rank);
            else if (c.policy == "optimal") {
                const auto next = [&](u64 b) { const auto& q = future.at(b); return q.empty() ? std::numeric_limits<u64>::max() : q.front(); };
                choose = next(set[i].block) > next(set[result].block);
            } else choose = set[i].rank < set[result].rank;
            if (choose) result = i;
        }
        return result;
    }
    void touch(std::vector<Line>& set, Line& line) {
        const u64 old_rank = line.rank;
        u64 count = 0;
        for (auto& entry : set) if (entry.valid) {
            ++count;
            if (entry.rank > old_rank) --entry.rank;
        }
        line.rank = count-1;
    }
    void memory_write(u64 bytes, bool measured) {
        if (measured) { ++stats.memory_writes; stats.memory_write_bytes += bytes; }
    }
public:
    Stats stats;
    explicit Cache(Config config) : c(std::move(config)), rng(c.seed) {
        sets.assign(c.size/(c.block*c.assoc), std::vector<Line>(c.assoc));
        if (c.policy == "igbi") predictor.assign(16384, 2);
        if (c.policy == "ml") {
            model = Model::load(c.model_path);
            if (model.block_size != c.block) throw std::runtime_error("model block size differs from cache; retrain for this block size");
            if (c.threshold >= 0) model.threshold = c.threshold;
            history = std::make_unique<FeatureTracker>(model.horizon);
        }
    }
    std::size_t predictor_bytes() const {
        return history ? history->bytes()+sizeof(Model) : predictor.size();
    }
    void prepare_optimal(const std::vector<Access>& trace) {
        for (std::size_t i = 0; i < trace.size(); ++i) future[trace[i].address/c.block].push_back(i);
    }
    void access(const Access& a, u64 position, bool measured) {
        ++tick;
        if (!measured) ++stats.warmup_accesses;
        const bool write = a.type == 'W';
        const u64 block = a.address/c.block;
        auto& set = sets[block % sets.size()];
        double probability = 1;
        if (history) {
            probability = model.predict(history->observe(a, c.block, position));
            if (measured) ++stats.ml_predictions;
        }
        if (c.policy == "optimal") {
            auto& queue = future.at(block);
            if (!queue.empty() && queue.front() == position) queue.pop_front();
        }
        if (measured) {
            ++stats.total_accesses;
            write ? ++stats.write_accesses : ++stats.read_accesses;
        }
        std::size_t found = set.size(), empty = set.size();
        for (std::size_t i = 0; i < set.size(); ++i) {
            if (set[i].valid && set[i].block == block) { found = i; break; }
            if (!set[i].valid && empty == set.size()) empty = i;
        }
        if (found != set.size()) {
            auto& line = set[found];
            if (measured) ++stats.hits;
            ++line.frequency; touch(set, line); line.rrpv = 0;
            if (c.policy == "igbi") {
                auto& counter = predictor[signature_hash(line.pc) % predictor.size()];
                if (counter < 3) ++counter;
            }
            if (write) {
                if (c.write == "wb") line.dirty = true;
                else memory_write(a.bytes, measured);
            }
            return;
        }
        if (measured) {
            ++stats.misses;
            write ? ++stats.write_misses : ++stats.read_misses;
        }
        if (write && c.alloc == "nwa") { memory_write(a.bytes, measured); return; }
        if (measured) { ++stats.memory_reads; stats.memory_read_bytes += c.block; }
        const std::size_t index = empty == set.size() ? victim(set) : empty;
        auto& line = set[index];
        if (line.valid) {
            if (measured) ++stats.evictions;
            if (line.dirty) {
                if (measured) ++stats.write_backs;
                memory_write(c.block, measured);
            }
            if (c.policy == "igbi") {
                auto& counter = predictor[signature_hash(line.pc) % predictor.size()];
                if (!line.frequency && counter) --counter;
                else if (line.frequency && counter < 3) ++counter;
            }
            for (auto& entry : set) if (entry.valid && entry.rank > line.rank) --entry.rank;
            line.valid = false;
        }
        bool low = c.policy == "lip" || (c.policy == "ml" && probability < model.threshold);
        if (c.policy == "igbi") low = predictor[signature_hash(a.pc)%predictor.size()] == 0;
        u64 count = 0;
        for (auto& entry : set) if (entry.valid) { ++count; if (low) ++entry.rank; }
        line = Line{true, write && c.write == "wb", block, a.pc, tick,
                    c.policy == "igbi" ? 0ULL : 1ULL, low ? 0 : count, 2};
        if (measured && low && c.policy == "ml") ++stats.ml_low_priority_insertions;
        if (write && c.write == "wt") memory_write(a.bytes, measured);
    }
    void finish() {
        if (c.flush) for (auto& set : sets) for (auto& line : set) if (line.valid && line.dirty) {
            ++stats.flush_write_backs; memory_write(c.block, true); line.dirty = false;
        }
    }
};
} // namespace cachelab

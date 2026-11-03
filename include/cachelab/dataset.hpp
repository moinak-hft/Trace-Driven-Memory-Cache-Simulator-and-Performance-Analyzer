#pragma once
#include "features.hpp"
#include <deque>
#include <iomanip>
#include <unordered_map>

namespace cachelab {
inline u64 export_dataset(const std::string& trace, const std::string& output, u64 block_size, u64 horizon) {
    if (output == trace) throw std::runtime_error("dataset output must differ from trace");
    std::ofstream out(output);
    if (!out) throw std::runtime_error("cannot create dataset: " + output);
    out << "position,block,label";
    for (const auto* name : FEATURE_NAMES) out << ',' << name;
    out << '\n' << std::setprecision(17);
    struct Sample { u64 position, block; Features features; };
    std::deque<Sample> pending;
    std::unordered_map<u64, u64> latest;
    FeatureTracker tracker(horizon);
    u64 position = 0, rows = 0;
    visit_trace(trace, block_size, [&](const Access& a) {
        const u64 block = a.address/block_size;
        auto f = tracker.observe(a, block_size, position);
        latest[block] = position;
        pending.push_back(Sample{position++, block, f});
        // Resolve labels only once the entire look-ahead horizon is known.
        if (pending.size() > horizon) {
            const auto& s = pending.front();
            const auto last = latest.at(s.block);
            out << s.position << ',' << s.block << ',' << (last > s.position ? 1 : 0);
            for (double value : s.features) out << ',' << value;
            out << '\n'; ++rows;
            if (last == s.position) latest.erase(s.block);
            pending.pop_front();
        }
        return true;
    });
    // Final K samples are censored, not incorrectly labelled "never reused".
    if (!out) throw std::runtime_error("error writing dataset");
    return rows;
}
} // namespace cachelab

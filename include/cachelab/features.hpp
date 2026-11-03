#pragma once
#include "trace.hpp"
#include <array>
#include <cmath>

namespace cachelab {
constexpr std::size_t FEATURE_COUNT = 8;
constexpr const char* FEATURE_SCHEMA = "causal-v1";
inline const std::array<const char*, FEATURE_COUNT> FEATURE_NAMES = {
    "pc_repeat_rate", "pc_frequency", "block_recency", "block_frequency",
    "same_block", "stable_stride", "stride_magnitude", "is_write"
};
using Features = std::array<double, FEATURE_COUNT>;

inline u64 signature_hash(u64 x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Fixed-size, direct-mapped histories bound memory for arbitrarily long traces.
// Collisions conservatively forget history. No future outcomes enter features.
class FeatureTracker {
    struct Block { bool valid = false; u64 tag = 0, last = 0, count = 0; };
    struct PC {
        bool valid = false, has_stride = false, stride_negative = false;
        u64 tag = 0, count = 0, repeats = 0, last_block = 0, stride = 0;
    };
    std::vector<Block> blocks = std::vector<Block>(65536);
    std::vector<PC> pcs = std::vector<PC>(16384);
    u64 horizon;
public:
    explicit FeatureTracker(u64 h) : horizon(h) {}
    std::size_t bytes() const { return blocks.size()*sizeof(Block) + pcs.size()*sizeof(PC); }
    Features observe(const Access& access, u64 block_size, u64 position) {
        const u64 block = access.address / block_size;
        auto& b = blocks[signature_hash(block) % blocks.size()];
        auto& p = pcs[signature_hash(access.pc) % pcs.size()];
        const bool seen = b.valid && b.tag == block;
        const bool known_pc = p.valid && p.tag == access.pc;
        const bool recent = seen && position - b.last <= horizon;
        const u64 distance = known_pc ? (block > p.last_block ? block-p.last_block : p.last_block-block) : 0;
        const bool negative = known_pc && block < p.last_block;
        Features f = {
            known_pc ? double(p.repeats + 1) / double(p.count + 2) : 0.5,
            known_pc ? std::min(1.0, std::log2(1.0+p.count)/16.0) : 0.0,
            seen ? std::max(0.0, 1.0-double(position-b.last)/double(horizon)) : 0.0,
            seen ? std::min(1.0, std::log2(1.0+b.count)/8.0) : 0.0,
            known_pc && block == p.last_block ? 1.0 : 0.0,
            known_pc && p.has_stride && distance == p.stride && negative == p.stride_negative ? 1.0 : 0.0,
            known_pc ? std::min(1.0, std::log2(1.0+double(distance))/16.0) : 0.0,
            access.type == 'W' ? 1.0 : 0.0
        };
        if (!known_pc) p = PC{};
        if (known_pc) { p.has_stride = true; p.stride = distance; p.stride_negative = negative; }
        p.valid = true; p.tag = access.pc; ++p.count;
        p.repeats += recent ? 1 : 0; p.last_block = block;
        if (!seen) b = Block{};
        b.valid = true; b.tag = block; b.last = position; ++b.count;
        return f;
    }
};
} // namespace cachelab

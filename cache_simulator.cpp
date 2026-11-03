#include "include/cachelab/cache.hpp"
#include "include/cachelab/dataset.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>

using namespace cachelab;

static void help() {
    std::cout << "CacheLab: trace-driven data cache simulation with learned reuse prediction\n"
        "Usage: cache_simulator --trace FILE [options]\n"
        "  --size BYTES --block BYTES --assoc WAYS\n"
        "  --policy lru|fifo|random|lfu|mru|optimal|igbi|lip|srrip|ml\n"
        "  --write wb|wt --alloc wa|nwa --seed N\n"
        "  --model FILE [--threshold 0..1]    Required by ml\n"
        "  --warmup N --limit N              Block accesses; limit 0 = all\n"
        "  --hit-cycles N --miss-penalty N    First-order AMAT assumptions\n"
        "  --json --flush                    JSON output; optional final dirty flush\n"
        "  --export-dataset FILE --horizon K Export causal features and reuse labels\n"
        "Trace: R|W address OR R|W pc address [byte_count]; # comments allowed.\n";
}

static double real_number(const std::string& value) {
    std::size_t used = 0; double result = std::stod(value, &used);
    if (used != value.size() || !std::isfinite(result)) throw std::runtime_error("invalid number: " + value);
    return result;
}
static bool power_of_two(u64 n) { return n && !(n & (n-1)); }

int main(int argc, char** argv) {
    try {
        Config c;
        std::string trace, dataset;
        u64 horizon = 256;
        bool json = false, threshold_supplied = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto value = [&]() -> std::string {
                if (i+1 == argc) throw std::runtime_error("missing value for " + arg);
                return argv[++i];
            };
            if (arg == "--help" || arg == "-h") { help(); return 0; }
            else if (arg == "--size") c.size = integer(value());
            else if (arg == "--block") c.block = integer(value());
            else if (arg == "--assoc") c.assoc = integer(value());
            else if (arg == "--policy") c.policy = value();
            else if (arg == "--write") c.write = value();
            else if (arg == "--alloc") c.alloc = value();
            else if (arg == "--trace") trace = value();
            else if (arg == "--model") c.model_path = value();
            else if (arg == "--threshold") { c.threshold = real_number(value()); threshold_supplied = true; }
            else if (arg == "--seed") c.seed = integer(value());
            else if (arg == "--warmup") c.warmup = integer(value());
            else if (arg == "--limit") c.limit = integer(value());
            else if (arg == "--hit-cycles") c.hit_cycles = real_number(value());
            else if (arg == "--miss-penalty") c.miss_penalty = real_number(value());
            else if (arg == "--horizon") horizon = integer(value());
            else if (arg == "--export-dataset") dataset = value();
            else if (arg == "--json") json = true;
            else if (arg == "--flush") c.flush = true;
            else if (!arg.empty() && arg[0] != '-' && trace.empty()) trace = arg;
            else throw std::runtime_error("unknown argument: " + arg);
        }
        const std::set<std::string> policies{"lru","fifo","random","lfu","mru","optimal","igbi","lip","srrip","ml"};
        if (!policies.count(c.policy)) throw std::runtime_error("unknown policy: " + c.policy);
        if (trace.empty()) throw std::runtime_error("--trace is required; use --help for usage");
        if (!power_of_two(c.block) || !power_of_two(c.size) || !power_of_two(c.assoc) ||
            c.assoc > c.size/c.block || c.size % (c.block*c.assoc))
            throw std::runtime_error("size, block and associativity must be positive powers of two with size >= block*assoc");
        if (c.write != "wb" && c.write != "wt") throw std::runtime_error("--write must be wb or wt");
        if (c.alloc != "wa" && c.alloc != "nwa") throw std::runtime_error("--alloc must be wa or nwa");
        if (c.policy == "optimal" && c.alloc != "wa") throw std::runtime_error("optimal baseline requires write allocation (--alloc wa)");
        if (c.hit_cycles < 0 || c.miss_penalty < 0) throw std::runtime_error("latencies must be nonnegative");
        if (threshold_supplied && (c.threshold < 0 || c.threshold > 1)) throw std::runtime_error("threshold must be between 0 and 1");
        if (!horizon || horizon > 10000000) throw std::runtime_error("horizon must be 1..10000000");
        if (!dataset.empty()) {
            if (std::filesystem::weakly_canonical(trace) == std::filesystem::weakly_canonical(dataset))
                throw std::runtime_error("dataset output must differ from trace");
            if (c.warmup || c.limit) throw std::runtime_error("dataset export uses full traces; omit --warmup and --limit");
            const auto rows = export_dataset(trace, dataset, c.block, horizon);
            std::cout << "Dataset rows: " << rows << " (last " << horizon << " accesses censored)\n";
            return 0;
        }
        if (c.policy == "ml" && c.model_path.empty()) throw std::runtime_error("ml requires --model FILE");
        Cache cache(c);
        const auto start = std::chrono::steady_clock::now();
        u64 count = 0;
        const auto keep = [&]() { return !c.limit || count < c.warmup || count-c.warmup < c.limit; };
        if (c.policy == "optimal") {
            std::vector<Access> accesses;
            visit_trace(trace, c.block, [&](const Access& a) {
                if (!keep()) return false;
                accesses.push_back(a); ++count; return keep();
            });
            cache.prepare_optimal(accesses);
            for (std::size_t i = 0; i < accesses.size(); ++i) cache.access(accesses[i], i, i >= c.warmup);
        } else {
            visit_trace(trace, c.block, [&](const Access& a) {
                if (!keep()) return false;
                cache.access(a, count, count >= c.warmup); ++count; return keep();
            });
        }
        cache.finish();
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        const auto& s = cache.stats;
        const double hit = s.total_accesses ? double(s.hits)/s.total_accesses : 0;
        const double miss = s.total_accesses ? double(s.misses)/s.total_accesses : 0;
        const double amat = s.total_accesses ? c.hit_cycles + miss*c.miss_penalty : 0;
        std::cout << std::setprecision(12);
        if (json) {
            std::cout << "{\"schema_version\":1,\"policy\":\"" << c.policy << "\"";
            const auto field = [&](const char* key, auto value) { std::cout << ",\"" << key << "\":" << value; };
            field("cache_size",c.size); field("block_size",c.block); field("associativity",c.assoc); field("seed",c.seed);
            field("total_accesses",s.total_accesses); field("hits",s.hits); field("misses",s.misses);
            field("read_accesses",s.read_accesses); field("write_accesses",s.write_accesses);
            field("read_misses",s.read_misses); field("write_misses",s.write_misses);
            field("hit_rate",hit); field("miss_rate",miss); field("evictions",s.evictions);
            field("write_backs",s.write_backs); field("flush_write_backs",s.flush_write_backs);
            field("memory_reads",s.memory_reads); field("memory_writes",s.memory_writes);
            field("memory_read_bytes",s.memory_read_bytes); field("memory_write_bytes",s.memory_write_bytes);
            field("memory_traffic_bytes",s.memory_read_bytes+s.memory_write_bytes);
            field("warmup_accesses",s.warmup_accesses); field("amat_cycles",amat);
            field("hit_cycles",c.hit_cycles); field("miss_penalty",c.miss_penalty);
            field("ml_predictions",s.ml_predictions); field("ml_low_priority_insertions",s.ml_low_priority_insertions);
            field("predictor_software_bytes",cache.predictor_bytes()); field("simulation_seconds",elapsed);
            field("accesses_per_second",elapsed > 0 ? count/elapsed : 0);
            std::cout << ",\"write_policy\":\"" << c.write << "\",\"allocation_policy\":\"" << c.alloc << "\"}\n";
        } else {
            std::cout << "Policy: " << c.policy << "\nTotal Accesses: " << s.total_accesses
                << "\nHits: " << s.hits << "\nMisses: " << s.misses << std::fixed << std::setprecision(2)
                << "\nHit Rate: " << hit*100 << "%\nMiss Rate: " << miss*100 << "%\nEvictions: " << s.evictions
                << "\nWrite Backs: " << s.write_backs << "\nMemory Traffic Bytes: " << s.memory_read_bytes+s.memory_write_bytes
                << "\nEstimated AMAT (cycles): " << amat << "\nSimulation Seconds: " << elapsed << '\n';
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 1; }
}

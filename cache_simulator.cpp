#include <bits/stdc++.h>
using namespace std;
#define pb push_back
#define F first
#define S second
using u64 = uint64_t;

enum class Policy
{
    LRU,
    FIFO,
    RANDOM,
    LFU,
    MRU,
    OPTIMAL,
    CUSTOM
};
enum class WritePolicy
{
    WRITE_BACK,
    WRITE_THROUGH
};
enum class AllocatePolicy
{
    WRITE_ALLOCATE,
    NO_WRITE_ALLOCATE
};

struct Access
{
    char type;
    u64 ip;
    u64 addr;
};

struct Line
{
    bool valid = false, dirty = false;
    u64 block = 0, inserted = 0, last_used = 0, freq = 0;
};

static string lower_copy(string s)
{
    for (char &c : s)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

static Policy parse_policy(const string &s)
{
    string x = lower_copy(s);
    if (x == "lru")
        return Policy::LRU;
    if (x == "fifo")
        return Policy::FIFO;
    if (x == "random")
        return Policy::RANDOM;
    if (x == "lfu")
        return Policy::LFU;
    if (x == "mru")
        return Policy::MRU;
    if (x == "optimal" || x == "belady" || x == "min")
        return Policy::OPTIMAL;
    if (x == "custom")
        return Policy::CUSTOM;
    cerr << "Unknown policy: " << s << "\n";
    exit(1);
}

static WritePolicy parse_write_policy(const string &s)
{
    string x = lower_copy(s);
    if (x == "wb" || x == "writeback" || x == "write-back")
        return WritePolicy::WRITE_BACK;
    if (x == "wt" || x == "writethrough" || x == "write-through")
        return WritePolicy::WRITE_THROUGH;
    cerr << "Unknown write policy: " << s << "\n";
    exit(1);
}

static AllocatePolicy parse_alloc_policy(const string &s)
{
    string x = lower_copy(s);
    if (x == "wa" || x == "writeallocate" || x == "write-allocate")
        return AllocatePolicy::WRITE_ALLOCATE;
    if (x == "nwa" || x == "no-write-allocate" || x == "nowriteallocate")
        return AllocatePolicy::NO_WRITE_ALLOCATE;
    cerr << "Unknown allocation policy: " << s << "\n";
    exit(1);
}

static u64 parse_u64(const string &s)
{
    return stoull(s, nullptr, 0);
}

static bool is_write(char c)
{
    return c == 'W' || c == 'w';
}

class CacheSimulator
{
    size_t cache_size, block_size, associativity, num_sets;
    Policy policy;
    WritePolicy write_policy;
    AllocatePolicy alloc_policy;
    vector<vector<Line>> sets;
    unordered_map<u64, deque<u64>> future;
    mt19937_64 rng;
    u64 clock_tick = 0;
    bool optimal_ready = false;

    int choose_victim(const vector<Line> &set)
    {
        if (policy == Policy::RANDOM)
        {
            uniform_int_distribution<int> dist(0, static_cast<int>(associativity - 1));
            return dist(rng);
        }

        int victim = 0;

        if (policy == Policy::FIFO)
        {
            u64 best = set[0].inserted;
            for (size_t i = 1; i < associativity; ++i)
            {
                if (set[i].inserted < best)
                {
                    best = set[i].inserted;
                    victim = static_cast<int>(i);
                }
            }
            return victim;
        }

        if (policy == Policy::LRU)
        {
            u64 best = set[0].last_used;
            for (size_t i = 1; i < associativity; ++i)
            {
                if (set[i].last_used < best)
                {
                    best = set[i].last_used;
                    victim = static_cast<int>(i);
                }
            }
            return victim;
        }

        if (policy == Policy::CUSTOM)
        {
            int unshielded_victim = -1;
            u64 oldest_unshielded = ULLONG_MAX;
            
            int global_victim = 0;
            u64 oldest_global = ULLONG_MAX;

            for (size_t i = 0; i < associativity; ++i)
            {
                if (set[i].last_used < oldest_global)
                {
                    oldest_global = set[i].last_used;
                    global_victim = static_cast<int>(i);
                }

                if (set[i].freq <= 1 && set[i].last_used < oldest_unshielded)
                {
                    oldest_unshielded = set[i].last_used;
                    unshielded_victim = static_cast<int>(i);
                }
            }
            
            return (unshielded_victim != -1) ? unshielded_victim : global_victim;
        }

        if (policy == Policy::MRU)
        {
            u64 best = set[0].last_used;
            for (size_t i = 1; i < associativity; ++i)
            {
                if (set[i].last_used > best)
                {
                    best = set[i].last_used;
                    victim = static_cast<int>(i);
                }
            }
            return victim;
        }

        if (policy == Policy::LFU)
        {
            u64 best_freq = set[0].freq;
            u64 best_time = set[0].last_used;
            for (size_t i = 1; i < associativity; ++i)
            {
                if (set[i].freq < best_freq || (set[i].freq == best_freq && set[i].last_used < best_time))
                {
                    best_freq = set[i].freq;
                    best_time = set[i].last_used;
                    victim = static_cast<int>(i);
                }
            }
            return victim;
        }

        if (policy == Policy::OPTIMAL)
        {
            u64 farthest = 0;
            for (size_t i = 0; i < associativity; ++i)
            {
                auto it = future.find(set[i].block);
                u64 next_use = ULLONG_MAX;
                if (it != future.end() && !it->second.empty())
                    next_use = it->second.front();
                if (next_use == ULLONG_MAX)
                    return static_cast<int>(i);
                if (next_use > farthest)
                {
                    farthest = next_use;
                    victim = static_cast<int>(i);
                }
            }
            return victim;
        }

        return 0; 
    }

public:
    u64 total_accesses = 0, reads = 0, writes = 0, hits = 0, misses = 0, evictions = 0, write_backs = 0, mem_writes = 0;

    CacheSimulator(size_t cache_size_,
                   size_t block_size_,
                   size_t associativity_,
                   Policy policy_,
                   WritePolicy write_policy_,
                   AllocatePolicy alloc_policy_)
        : cache_size(cache_size_),
          block_size(block_size_),
          associativity(associativity_),
          policy(policy_),
          write_policy(write_policy_),
          alloc_policy(alloc_policy_),
          rng(1337)
    {
        if (block_size == 0 || associativity == 0 || cache_size == 0)
        {
            cerr << "Cache parameters must be non-zero\n";
            exit(1);
        }
        if (cache_size < block_size * associativity)
        {
            cerr << "Cache size must be at least block_size * associativity\n";
            exit(1);
        }
        if (cache_size % (block_size * associativity) != 0)
        {
            cerr << "Cache size must be divisible by block_size * associativity\n";
            exit(1);
        }
        num_sets = cache_size / (block_size * associativity);
        if (num_sets == 0)
        {
            cerr << "Number of sets became zero\n";
            exit(1);
        }
        sets.assign(num_sets, vector<Line>(associativity));
    }

    void preprocess_optimal(const vector<Access> &trace)
    {
        future.clear();
        for (u64 i = 0; i < trace.size(); ++i)
        {
            u64 block = trace[i].addr / block_size;
            future[block].push_back(i);
        }
        optimal_ready = true;
    }

    void access(u64 addr, char type, u64 pos, u64 ip)
    {
        total_accesses++;
        is_write(type) ? writes++ : reads++;

        u64 block = addr / block_size;
        size_t set_idx = static_cast<size_t>(block % num_sets);
        auto &set = sets[set_idx];

        if (policy == Policy::OPTIMAL && optimal_ready)
        {
            auto &dq = future[block];
            if (!dq.empty() && dq.front() == pos)
                dq.pop_front();
        }

        int hit_idx = -1;
        int empty_idx = -1;

        for (size_t i = 0; i < associativity; ++i)
        {
            if (set[i].valid && set[i].block == block)
            {
                hit_idx = static_cast<int>(i);
                break;
            }
            if (!set[i].valid && empty_idx == -1)
                empty_idx = static_cast<int>(i);
        }

        if (hit_idx != -1)
        {
            hits++;
            auto &line = set[hit_idx];
            line.last_used = ++clock_tick;
            line.freq++;

            if (is_write(type))
            {
                write_policy == WritePolicy::WRITE_BACK ? line.dirty = true : mem_writes++;
            }
            return;
        }

        misses++;

        if (is_write(type) && alloc_policy == AllocatePolicy::NO_WRITE_ALLOCATE)
        {
            mem_writes++;
            return;
        }

        int victim = empty_idx;
        if (victim == -1)
        {
            victim = choose_victim(set);
            if (set[victim].valid)
            {
                evictions++;
                if (set[victim].dirty && write_policy == WritePolicy::WRITE_BACK)
                {
                    write_backs++;
                    mem_writes++;
                }
            }
        }

        auto &line = set[victim];
        line.valid = true;
        line.dirty = is_write(type) && write_policy == WritePolicy::WRITE_BACK;
        line.block = block;
        line.inserted = ++clock_tick;
        line.last_used = clock_tick;
        line.freq = 1;

        if (is_write(type) && write_policy == WritePolicy::WRITE_THROUGH)
            mem_writes++;

        if (policy == Policy::OPTIMAL && optimal_ready)
        {
            auto &dq = future[block];
            if (!dq.empty() && dq.front() == pos)
                dq.pop_front();
        }
    }

    void print_report() const
    {
        cout << fixed << setprecision(2);
        double total = total_accesses ? static_cast<double>(total_accesses) : 1.0;
        std::cout << "Hit Rate: " << (100.0 * hits / total) << "%\n"
                  << "Miss Rate: " << (100.0 * misses / total) << "%\n"
                  << "Evictions: " << evictions << "\n"
                  << "Write Backs: " << write_backs << "\n";
    }
};

static bool parse_trace_line(const string &line, Access &a)
{
    if (line.empty() || line[0] == '#')
        return false;

    stringstream ss(line);
    vector<string> tok;
    string x;
    while (ss >> x)
        tok.push_back(x);
    if (tok.size() < 2)
        return false;

    char t = static_cast<char>(toupper(static_cast<unsigned char>(tok[0][0])));
    if (t != 'R' && t != 'W')
        return false;

    a.type = t;
    if (tok.size() >= 3)
    {
        a.ip = parse_u64(tok[1]);
        a.addr = parse_u64(tok.back());
    }
    else
    {
        a.ip = 0;
        a.addr = parse_u64(tok.back());
    }
    return true;
}

static vector<Access> load_trace(const string &path)
{
    ifstream in(path);
    if (!in.is_open())
    {
        cerr << "Cannot open trace file: " << path << "\n";
        exit(1);
    }

    vector<Access> trace;
    string line;
    while (getline(in, line))
    {
        Access a{};
        if (parse_trace_line(line, a))
            trace.push_back(a);
    }
    return trace;
}

static void print_help()
{
    cout << "Usage:\n";
    cout << "  cache_simulator --size N --block N --assoc N --policy lru|fifo|random|lfu|mru|optimal|custom --write wb|wt --alloc wa|nwa trace.txt\n";
}

int main(int argc, char *argv[])
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    size_t cache_size = 32768;
    size_t block_size = 64;
    size_t assoc = 8;
    Policy policy = Policy::LRU;
    WritePolicy write_policy = WritePolicy::WRITE_BACK;
    AllocatePolicy alloc_policy = AllocatePolicy::WRITE_ALLOCATE;
    string trace_path;

    for (int i = 1; i < argc; ++i)
    {
        string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            print_help();
            return 0;
        }
        else if (arg == "--size" && i + 1 < argc)
            cache_size = static_cast<size_t>(parse_u64(argv[++i]));

        else if (arg == "--block" && i + 1 < argc)
            block_size = static_cast<size_t>(parse_u64(argv[++i]));

        else if (arg == "--assoc" && i + 1 < argc)
            assoc = static_cast<size_t>(parse_u64(argv[++i]));

        else if (arg == "--policy" && i + 1 < argc)
            policy = parse_policy(argv[++i]);

        else if (arg == "--write" && i + 1 < argc)
            write_policy = parse_write_policy(argv[++i]);

        else if (arg == "--alloc" && i + 1 < argc)
            alloc_policy = parse_alloc_policy(argv[++i]);

        else if (arg == "--trace" && i + 1 < argc)
            trace_path = argv[++i];
            
        else if (!arg.empty() && arg[0] == '-')
        {
            cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
        else
            trace_path = arg;
    }

    if (trace_path.empty())
    {
        print_help();
        return 1;
    }

    vector<Access> trace = load_trace(trace_path);
    CacheSimulator sim(cache_size, block_size, assoc, policy, write_policy, alloc_policy);
    if (policy == Policy::OPTIMAL)
        sim.preprocess_optimal(trace);

    for (u64 i = 0; i < trace.size(); ++i)
    {
        sim.access(trace[i].addr, trace[i].type, i, trace[i].ip);
    }

    sim.print_report();
    return 0;
}
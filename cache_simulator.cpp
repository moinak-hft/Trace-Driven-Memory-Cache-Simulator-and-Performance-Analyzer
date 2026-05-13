#include <bits/stdc++.h>
using namespace std;
#define pb push_back
#define F first
#define S second
using u64 = uint64_t;
using st = size_t;
#define rep(i, a, b) for (st i = a; i < b; i++)

enum Policy { P_LRU, P_FIFO, P_RAND, P_LFU, P_MRU, P_OPT, P_IGBI };
enum WritePolicy { WB, WT };
enum AllocPolicy { WA, NWA };

struct Access
{
    char type;
    u64 ip, addr;
};

struct Line
{
    bool valid = false, dirty = false;
    u64 block = 0, inserted = 0, lastUse = 0, freq = 0, ip = 0;
};

Policy parse_pol(string s)
{
    for (char &c : s)
        c = tolower(c);
    if (s == "lru")
        return P_LRU;
    if (s == "fifo")
        return P_FIFO;
    if (s == "random")
        return P_RAND;
    if (s == "lfu")
        return P_LFU;
    if (s == "mru")
        return P_MRU;
    if (s == "optimal" || s == "belady" || s == "min")
        return P_OPT;
    if (s == "custom" || s == "igbi")
        return P_IGBI;
    exit(1);
}

WritePolicy parse_wp(string s)
{
    for (char &c : s)
        c = tolower(c);
    if (s == "wb" || s == "writeback")
        return WB;
    if (s == "wt" || s == "writethrough")
        return WT;
    exit(1);
}

AllocPolicy parse_ap(string s)
{
    for (char &c : s)
        c = tolower(c);
    if (s == "wa" || s == "writeallocate")
        return WA;
    if (s == "nwa" || s == "nowriteallocate")
        return NWA;
    exit(1);
}

class CacheSim
{
    st csz, bsz, assoc, nsets;
    Policy pol;
    WritePolicy wp;
    AllocPolicy ap;
    vector<vector<Line>> sets;
    unordered_map<u64, deque<u64>> fut; // Beleady's OPT
    vector<int> ipTable; // hardware predictor table for IGBI.
    mt19937_64 rng;
    u64 clk = 0;
    bool optRun = false;

    int blockForEviction(const vector<Line> &set)
    {
        if (pol == P_RAND)
            return rng() % assoc;
        int targ = 0;

        if (pol == P_FIFO)
        {
            u64 bst = set[0].inserted;
            rep(i, 1, assoc) if (set[i].inserted < bst) bst = set[i].inserted, targ = i;
            return targ;
        }
        if (pol == P_LRU || pol == P_IGBI)
        {
            u64 bst = set[0].lastUse;
            rep(i, 1, assoc) if (set[i].lastUse < bst) bst = set[i].lastUse, targ = i;
            return targ;
        }
        if (pol == P_MRU)
        {
            u64 bst = set[0].lastUse;
            rep(i, 1, assoc) if (set[i].lastUse > bst) bst = set[i].lastUse, targ = i;
            return targ;
        }
        if (pol == P_LFU)
        {
            u64 tf = set[0].freq, tt = set[0].lastUse;
            rep(i, 1, assoc)
            {
                if (set[i].freq < tf || (set[i].freq == tf && set[i].lastUse < tt))
                {
                    tf = set[i].freq;
                    tt = set[i].lastUse;
                    targ = i;
                }
            }
            return targ;
        }
        if (pol == P_OPT)
        {
            u64 far = 0;
            rep(i, 0, assoc)
            {
                auto it = fut.find(set[i].block);
                u64 nxt = (it != fut.end() && !it->second.empty()) ? it->second.front() : ULLONG_MAX;
                if (nxt == ULLONG_MAX)
                    return i;
                if (nxt > far)
                    far = nxt, targ = i;
            }
            return targ;
        }
        return 0;
    }

public:
    u64 tot = 0, rds = 0, wrs = 0, hts = 0, mis = 0, evs = 0, wbs = 0, mws = 0;

    CacheSim(st cs, st bs, st a, Policy p, WritePolicy w, AllocPolicy al)
        : csz(cs), bsz(bs), assoc(a), pol(p), wp(w), ap(al), rng(1337)
    {
        if (!bsz || !assoc || !csz || csz <= bsz * assoc - 1 || csz % (bsz * assoc))
            exit(1);
        nsets = csz / (bsz * assoc);
        if (!nsets)
            exit(1);
        sets.assign(nsets, vector<Line>(assoc));
        ipTable.assign(16384, 2);
    }

    void precomp(const vector<Access> &tr)
    {
        fut.clear();
        rep(i, 0, tr.size()) fut[tr[i].addr / bsz].pb(i);
        optRun = true;
    }

    void access(u64 addr, char typ, u64 pos, u64 ip)
    {
        tot++;
        bool is_wr = (typ == 'W' || typ == 'w');
        is_wr ? wrs++ : rds++;

        u64 blk = addr / bsz;
        auto &set = sets[blk % nsets];

        if (pol == P_OPT && optRun)
        {
            auto &dq = fut[blk];
            if (!dq.empty() && dq.front() == pos)
                dq.pop_front();
        }

        int hit = -1, emp = -1;
        rep(i, 0, assoc)
        {
            if (set[i].valid && set[i].block == blk)
            {
                hit = i;
                break;
            }
            if (!set[i].valid && emp == -1)
                emp = i;
        }

        if (hit != -1)
        {
            hts++;
            set[hit].freq++;
            if (pol == P_IGBI)
            {
                u64 hashIP = (set[hit].ip ^ (set[hit].ip >> 4)) % 16384;
                if (ipTable[hashIP] < 3)
                    ipTable[hashIP]++;
            }
            set[hit].lastUse = ++clk;
            if (is_wr)
                wp == WB ? set[hit].dirty = true : mws++;
            return;
        } 

        mis++;
        if (is_wr && ap == NWA)
        {
            mws++;
            return;
        }

        int targ = emp;
        if (targ == -1)
        {
            targ = blockForEviction(set);
            if (set[targ].valid)
            {
                evs++;
                if (pol == P_IGBI)
                {
                    u64 hashedIP = (set[targ].ip ^ (set[targ].ip >> 4)) % 16384;
                    if (set[targ].freq == 0){
                        if (ipTable[hashedIP] > 0)
                            ipTable[hashedIP]--;
                    }
                    else{
                        if (ipTable[hashedIP] < 3)
                            ipTable[hashedIP]++;
                    }
                }
                if (set[targ].dirty && wp == WB)
                    wbs++, mws++;
            }
        }

        set[targ].valid = true;
        set[targ].dirty = (is_wr && wp == WB);
        set[targ].block = blk;
        set[targ].inserted = ++clk;
        set[targ].freq = (pol == P_IGBI) ? 0 : 1;

        if (pol == P_IGBI)
        {
            set[targ].ip = ip;
            u64 new_ip_hash = (ip ^ (ip >> 4)) % 16384;
            if (ipTable[new_ip_hash] == 0)
                set[targ].lastUse = 0;
            else
                set[targ].lastUse = clk;
        }
        else
        {
            set[targ].lastUse = clk;
        }

        if (is_wr && wp == WT)
            mws++;
    }

    void out() const
    {
        cout << fixed << setprecision(2);
        double dtot = tot ? (double)tot : 1.0;
        cout << "Hit Rate: " << (100.0 * hts / dtot) << "%\n"
             << "Miss Rate: " << (100.0 * mis / dtot) << "%\n"
             << "Evictions: " << evs << "\n"
             << "Write Backs: " << wbs << "\n";
    }
};

bool parse_tr(const string &line, Access &a)
{
    if (line.empty() || line[0] == '#')
        return false;
    stringstream ss(line);
    vector<string> tok;
    string x;
    while (ss >> x)
        tok.pb(x);
    if (tok.size() < 2)
        return false;
    char t = toupper(tok[0][0]);
    if (t != 'R' && t != 'W')
        return false;
    a.type = t;
    a.ip = tok.size() >= 3 ? stoull(tok[1], 0, 0) : 0;
    a.addr = stoull(tok.back(), 0, 0);
    return true;
}

int main(int argc, char *argv[])
{
    ios::sync_with_stdio(0);
    cin.tie(0);
    st csz = 32768, bsz = 64, assoc = 8;
    Policy pol = P_LRU;
    WritePolicy wp = WB;
    AllocPolicy ap = WA;
    string tr_path;

    rep(i, 1, argc)
    {
        string arg = argv[i];
        if (arg == "--size" && i + 1 < (st)argc)
            csz = stoull(argv[++i]);
        else if (arg == "--block" && i + 1 < (st)argc)
            bsz = stoull(argv[++i]);
        else if (arg == "--assoc" && i + 1 < (st)argc)
            assoc = stoull(argv[++i]);
        else if (arg == "--policy" && i + 1 < (st)argc)
            pol = parse_pol(argv[++i]);
        else if (arg == "--write" && i + 1 < (st)argc)
            wp = parse_wp(argv[++i]);
        else if (arg == "--alloc" && i + 1 < (st)argc)
            ap = parse_ap(argv[++i]);
        else if (arg == "--trace" && i + 1 < (st)argc)
            tr_path = argv[++i];
        else if (arg[0] != '-')
            tr_path = arg;
    }

    if (tr_path.empty())
        exit(1);

    ifstream in(tr_path);
    if (!in.is_open())
        exit(1);
    vector<Access> tr;
    string ln;

    while (getline(in, ln))
    {
        Access a{};
        if (parse_tr(ln, a))
            tr.pb(a);
    }

    CacheSim sim(csz, bsz, assoc, pol, wp, ap);
    if (pol == P_OPT)
        sim.precomp(tr);
    rep(i, 0, tr.size()) sim.access(tr[i].addr, tr[i].type, i, tr[i].ip);
    sim.out();
    return 0;
}

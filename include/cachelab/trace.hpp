#pragma once
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cachelab {
using u64 = std::uint64_t;
struct Access { char type; u64 pc, address, bytes; };

inline u64 integer(const std::string& s) {
    if (s.empty() || s.front() == '-') throw std::runtime_error("invalid unsigned integer: " + s);
    std::size_t used = 0;
    const int base = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? 16 : 10;
    u64 n = std::stoull(s, &used, base);
    if (used != s.size()) throw std::runtime_error("invalid integer: " + s);
    return n;
}

// Legacy: R address / R pc address. Sized: R pc address bytes.
// The instruction PC is context for a DATA access, not an instruction fetch.
inline bool parse_access(std::string line, Access& a) {
    line = line.substr(0, line.find('#'));
    std::istringstream in(line);
    std::vector<std::string> t;
    for (std::string word; in >> word;) t.push_back(word);
    if (t.empty()) return false;
    if (t.size() < 2 || t.size() > 4 || (t[0] != "R" && t[0] != "W"))
        throw std::runtime_error("expected R|W [pc] address [bytes]");
    a.type = t[0][0];
    a.pc = t.size() >= 3 ? integer(t[1]) : 0;
    a.address = integer(t[t.size() == 2 ? 1 : 2]);
    a.bytes = t.size() == 4 ? integer(t[3]) : 1;
    if (!a.bytes || a.bytes - 1 > std::numeric_limits<u64>::max() - a.address)
        throw std::runtime_error("zero-sized or overflowing address range");
    return true;
}

// Streams trace records and splits every range at cache-line boundaries.
// Returning false from visit stops without reading the rest of the file.
inline u64 visit_trace(const std::string& path, u64 block_size,
                      const std::function<bool(const Access&)>& visit) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open trace: " + path);
    std::string line;
    u64 line_no = 0, records = 0;
    while (std::getline(input, line)) {
        ++line_no;
        Access a{};
        try { if (!parse_access(line, a)) continue; }
        catch (const std::exception& e) {
            throw std::runtime_error(path + ":" + std::to_string(line_no) + ": " + e.what());
        }
        ++records;
        u64 remaining = a.bytes;
        while (remaining) {
            const u64 part = std::min(remaining, block_size - a.address % block_size);
            if (!visit(Access{a.type, a.pc, a.address, part})) return records;
            remaining -= part;
            if (remaining) a.address += part;
        }
    }
    if (input.bad()) throw std::runtime_error("error reading trace: " + path);
    return records;
}
} // namespace cachelab

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace kvlite {

// One mutating operation, as written to the append-only file.
struct AofRecord {
    enum class Op : char { Set = 'S', Del = 'D', Expire = 'E' };

    Op op;
    std::int64_t expires_at_ms;  // Unix epoch milliseconds; 0 means "no expiry"
    std::string key;
    std::string value;           // empty for Del and Expire
};

// Append-only file (AOF).
//
// Every record is a text header line followed by the raw key and value bytes:
//
//     <op> <expires_at_ms> <key_length> <value_length>\n<key><value>\n
//
// Lengths make keys and values binary-safe: they may contain spaces or
// newlines. If the process dies halfway through a write, the last record is
// incomplete. load() detects that, keeps every complete record before it, and
// cuts the file back to the last complete record.
class Aof {
public:
    struct LoadResult {
        std::vector<AofRecord> records;
        bool repaired_tail = false;  // true if a partial or corrupt tail was cut off
    };

    explicit Aof(std::string path);

    // Reads every complete record. A missing file counts as empty.
    LoadResult load();

    // Opens the file for appending. Call after load().
    void open_for_append();

    // Writes one record and flushes it to the operating system.
    void append(const AofRecord& record);

    // Replaces the whole file with `records`, atomically: writes a temporary
    // file, then renames it over the old one.
    void rewrite(const std::vector<AofRecord>& records);

    const std::string& path() const { return path_; }

private:
    static void write_record(std::ostream& out, const AofRecord& record);

    std::string path_;
    std::ofstream out_;
};

}  // namespace kvlite

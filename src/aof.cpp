#include "kvlite/aof.hpp"

#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace kvlite {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kMaxFieldBytes = 512ULL * 1024 * 1024;  // sanity limit per key/value

bool parse_op(char c, AofRecord::Op& op) {
    switch (c) {
        case 'S': op = AofRecord::Op::Set; return true;
        case 'D': op = AofRecord::Op::Del; return true;
        case 'E': op = AofRecord::Op::Expire; return true;
        default: return false;
    }
}

}  // namespace

Aof::Aof(std::string path) : path_(std::move(path)) {}

Aof::LoadResult Aof::load() {
    LoadResult result;
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        return result;  // no file yet: nothing to replay
    }

    std::streamoff last_good = 0;
    bool damaged = false;

    while (true) {
        std::string header;
        if (!std::getline(in, header)) {
            break;  // clean end of file
        }
        if (in.eof()) {  // header without its newline: write was cut off
            damaged = true;
            break;
        }

        std::istringstream fields(header);
        char op_char = 0;
        AofRecord record{};
        std::uint64_t key_len = 0;
        std::uint64_t value_len = 0;
        if (!(fields >> op_char >> record.expires_at_ms >> key_len >> value_len) ||
            !parse_op(op_char, record.op) || key_len > kMaxFieldBytes ||
            value_len > kMaxFieldBytes) {
            damaged = true;
            break;
        }

        std::string payload(static_cast<std::size_t>(key_len + value_len), '\0');
        in.read(payload.data(), static_cast<std::streamsize>(payload.size()));
        char terminator = 0;
        if (in.gcount() != static_cast<std::streamsize>(payload.size()) || !in.get(terminator) ||
            terminator != '\n') {
            damaged = true;
            break;
        }

        record.key = payload.substr(0, static_cast<std::size_t>(key_len));
        record.value = payload.substr(static_cast<std::size_t>(key_len));
        result.records.push_back(std::move(record));
        last_good = in.tellg();
    }

    in.close();
    if (damaged) {
        // Drop the partial tail so new records aren't appended after garbage.
        fs::resize_file(path_, static_cast<std::uintmax_t>(last_good));
        result.repaired_tail = true;
    }
    return result;
}

void Aof::open_for_append() {
    out_.close();
    out_.clear();
    out_.open(path_, std::ios::binary | std::ios::app);
    if (!out_) {
        throw std::runtime_error("cannot open append-only file: " + path_);
    }
}

void Aof::write_record(std::ostream& out, const AofRecord& record) {
    out << static_cast<char>(record.op) << ' ' << record.expires_at_ms << ' ' << record.key.size()
        << ' ' << record.value.size() << '\n'
        << record.key << record.value << '\n';
}

void Aof::append(const AofRecord& record) {
    write_record(out_, record);
    out_.flush();
    if (!out_) {
        throw std::runtime_error("write to append-only file failed: " + path_);
    }
}

void Aof::rewrite(const std::vector<AofRecord>& records) {
    const std::string tmp_path = path_ + ".tmp";
    {
        std::ofstream tmp(tmp_path, std::ios::binary | std::ios::trunc);
        if (!tmp) {
            throw std::runtime_error("cannot create " + tmp_path);
        }
        for (const auto& record : records) {
            write_record(tmp, record);
        }
        tmp.flush();
        if (!tmp) {
            throw std::runtime_error("write to " + tmp_path + " failed");
        }
    }
    out_.close();
    fs::rename(tmp_path, path_);  // atomic replace: readers see the old file or the new one
    open_for_append();
}

}  // namespace kvlite

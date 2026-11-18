#include "kvlite/commands.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>

namespace kvlite {

namespace {

// Largest TTL accepted: 10 years, so time arithmetic can't overflow.
constexpr std::int64_t kMaxTtlMs = 10LL * 365 * 24 * 60 * 60 * 1000;

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void expect_args(const std::vector<std::string>& args, std::size_t count) {
    if (args.size() != count) {
        throw CommandError("wrong number of arguments for '" + lower(args[0]) + "'");
    }
}

// Parses a whole-string positive integer, scaled to milliseconds.
Millis parse_ttl(const std::string& text, std::int64_t ms_per_unit) {
    std::int64_t n = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    auto [ptr, ec] = std::from_chars(first, last, n);
    if (ec != std::errc() || ptr != last || n <= 0 || n > kMaxTtlMs / ms_per_unit) {
        throw CommandError("value is not a valid positive integer");
    }
    return Millis(n * ms_per_unit);
}

std::string quote(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

std::string integer(std::int64_t n) { return "(integer) " + std::to_string(n); }

}  // namespace

std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> words;
    std::string current;
    bool in_word = false;
    bool in_quotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quotes) {
            if (c == '\\' && i + 1 < line.size()) {
                const char next = line[++i];
                current += (next == 'n') ? '\n' : next;
            } else if (c == '"') {
                in_quotes = false;
            } else {
                current += c;
            }
        } else if (c == '"') {
            in_quotes = true;
            in_word = true;  // "" is a valid empty word
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (in_word) {
                words.push_back(std::move(current));
                current.clear();
                in_word = false;
            }
        } else {
            current += c;
            in_word = true;
        }
    }
    if (in_quotes) {
        throw CommandError("unterminated quote");
    }
    if (in_word) {
        words.push_back(std::move(current));
    }
    return words;
}

std::string help_text() {
    return "SET key value [EX seconds | PX milliseconds]\n"
           "GET key\n"
           "DEL key\n"
           "EXISTS key\n"
           "EXPIRE key seconds\n"
           "TTL key          (-2 = no such key, -1 = no expiry)\n"
           "SIZE             number of live keys\n"
           "STATS            keys, capacity, evictions, replayed records\n"
           "COMPACT          rewrite the append-only file\n"
           "HELP\n"
           "QUIT";
}

std::string execute(Store& store, const std::vector<std::string>& args) {
    if (args.empty()) {
        return "";
    }
    const std::string cmd = upper(args[0]);

    if (cmd == "SET") {
        if (args.size() != 3 && args.size() != 5) {
            throw CommandError("wrong number of arguments for 'set'");
        }
        std::optional<Millis> ttl;
        if (args.size() == 5) {
            const std::string unit = upper(args[3]);
            if (unit == "EX") {
                ttl = parse_ttl(args[4], 1000);
            } else if (unit == "PX") {
                ttl = parse_ttl(args[4], 1);
            } else {
                throw CommandError("syntax error");
            }
        }
        store.set(args[1], args[2], ttl);
        return "OK";
    }
    if (cmd == "GET") {
        expect_args(args, 2);
        const auto value = store.get(args[1]);
        return value ? quote(*value) : "(nil)";
    }
    if (cmd == "DEL") {
        expect_args(args, 2);
        return integer(store.del(args[1]) ? 1 : 0);
    }
    if (cmd == "EXISTS") {
        expect_args(args, 2);
        return integer(store.exists(args[1]) ? 1 : 0);
    }
    if (cmd == "EXPIRE") {
        expect_args(args, 3);
        return integer(store.expire(args[1], parse_ttl(args[2], 1000)) ? 1 : 0);
    }
    if (cmd == "TTL") {
        expect_args(args, 2);
        const std::int64_t ms = store.ttl_ms(args[1]);
        return integer(ms < 0 ? ms : (ms + 500) / 1000);  // round to nearest second
    }
    if (cmd == "SIZE") {
        expect_args(args, 1);
        return integer(static_cast<std::int64_t>(store.size()));
    }
    if (cmd == "STATS") {
        expect_args(args, 1);
        const Store::Stats s = store.stats();
        return "keys=" + std::to_string(s.keys) + " capacity=" + std::to_string(s.capacity) +
               " evictions=" + std::to_string(s.evictions) +
               " replayed=" + std::to_string(s.replayed_records) +
               " persistence=" + (store.persistent() ? "on" : "off");
    }
    if (cmd == "COMPACT") {
        expect_args(args, 1);
        if (!store.persistent()) {
            throw CommandError("persistence is off (start with --aof FILE)");
        }
        store.compact();
        return "OK";
    }
    if (cmd == "HELP") {
        return help_text();
    }
    throw CommandError("unknown command '" + args[0] + "'");
}

}  // namespace kvlite

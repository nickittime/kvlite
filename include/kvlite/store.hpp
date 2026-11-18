#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "kvlite/aof.hpp"
#include "kvlite/lru_cache.hpp"

namespace kvlite {

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;
using Millis = std::chrono::milliseconds;

// In-memory key-value store with LRU eviction, per-key expiry (TTL) and
// optional append-only-file persistence.
class Store {
public:
    struct Options {
        std::size_t capacity = 10000;      // maximum number of keys
        std::string aof_path;              // empty = keep data in memory only
        std::function<TimePoint()> clock;  // injectable for tests; defaults to the system clock
    };

    struct Stats {
        std::size_t keys = 0;
        std::size_t capacity = 0;
        std::uint64_t evictions = 0;
        std::size_t replayed_records = 0;
        bool repaired_aof_tail = false;
    };

    explicit Store(Options options);

    // The eviction callback captures `this`, so a copied or moved Store would
    // call back into the wrong object. Forbid both.
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&) = delete;
    Store& operator=(Store&&) = delete;

    // Stores a value. With a ttl, the key expires after that much time.
    void set(const std::string& key, std::string value, std::optional<Millis> ttl = std::nullopt);

    // Returns the value, or nothing if the key is missing or expired.
    std::optional<std::string> get(const std::string& key);

    // Deletes a key. Returns true if a live key was removed.
    bool del(const std::string& key);

    bool exists(const std::string& key) const;

    // Sets a key to expire after `ttl`. Returns false if the key doesn't exist.
    bool expire(const std::string& key, Millis ttl);

    // Remaining time to live in milliseconds: -2 if the key doesn't exist,
    // -1 if it has no expiry (same convention as Redis TTL).
    std::int64_t ttl_ms(const std::string& key) const;

    // Removes every expired key and returns how many were removed.
    std::size_t purge_expired();

    // Number of live keys.
    std::size_t size();

    // Rewrites the append-only file so it holds only the current data.
    // Throws std::logic_error if persistence is off.
    void compact();

    bool persistent() const { return aof_ != nullptr; }
    Stats stats();

private:
    struct Entry {
        std::string value;
        std::optional<TimePoint> expires_at;
    };

    bool is_expired(const Entry& entry) const;
    void apply(const AofRecord& record);
    void log(const AofRecord& record);
    void on_evict(const std::string& key);

    Options options_;
    LruCache<std::string, Entry> cache_;
    std::unique_ptr<Aof> aof_;
    bool replaying_ = false;
    std::uint64_t evictions_ = 0;
    std::size_t replayed_records_ = 0;
    bool repaired_aof_tail_ = false;
};

}  // namespace kvlite

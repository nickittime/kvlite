#include "kvlite/store.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kvlite {

namespace {

std::int64_t to_epoch_ms(const std::optional<TimePoint>& t) {
    if (!t) {
        return 0;
    }
    return std::chrono::duration_cast<Millis>(t->time_since_epoch()).count();
}

std::optional<TimePoint> from_epoch_ms(std::int64_t ms) {
    if (ms == 0) {
        return std::nullopt;
    }
    return TimePoint(std::chrono::duration_cast<Clock::duration>(Millis(ms)));
}

}  // namespace

Store::Store(Options options)
    : options_(std::move(options)),
      cache_(options_.capacity, [this](const std::string& key, const Entry&) { on_evict(key); }) {
    if (!options_.clock) {
        options_.clock = [] { return Clock::now(); };
    }
    if (!options_.aof_path.empty()) {
        aof_ = std::make_unique<Aof>(options_.aof_path);
        Aof::LoadResult loaded = aof_->load();
        replaying_ = true;
        for (const auto& record : loaded.records) {
            apply(record);
        }
        replaying_ = false;
        replayed_records_ = loaded.records.size();
        repaired_aof_tail_ = loaded.repaired_tail;
        aof_->open_for_append();
    }
}

bool Store::is_expired(const Entry& entry) const {
    return entry.expires_at && *entry.expires_at <= options_.clock();
}

void Store::log(const AofRecord& record) {
    if (aof_ && !replaying_) {
        aof_->append(record);
    }
}

void Store::on_evict(const std::string& key) {
    ++evictions_;
    // Log evictions as deletes. Reads change LRU order but aren't logged, so
    // replay must not have to work out which key to evict on its own.
    log({AofRecord::Op::Del, 0, key, ""});
}

void Store::apply(const AofRecord& record) {
    switch (record.op) {
        case AofRecord::Op::Set: {
            Entry entry{record.value, from_epoch_ms(record.expires_at_ms)};
            if (is_expired(entry)) {
                cache_.erase(record.key);  // it expired while the process was down
            } else {
                cache_.put(record.key, std::move(entry));
            }
            break;
        }
        case AofRecord::Op::Del:
            cache_.erase(record.key);
            break;
        case AofRecord::Op::Expire:
            if (Entry* entry = cache_.get(record.key)) {
                entry->expires_at = from_epoch_ms(record.expires_at_ms);
                if (is_expired(*entry)) {
                    cache_.erase(record.key);
                }
            }
            break;
    }
}

void Store::set(const std::string& key, std::string value, std::optional<Millis> ttl) {
    std::optional<TimePoint> expires_at;
    if (ttl) {
        if (ttl->count() <= 0) {
            throw std::invalid_argument("ttl must be positive");
        }
        expires_at = options_.clock() + *ttl;
    }
    // put() may evict and log a delete first; the set is logged after it, so
    // replaying the file applies them in the same order.
    cache_.put(key, Entry{std::move(value), expires_at});
    log({AofRecord::Op::Set, to_epoch_ms(expires_at), key, cache_.peek(key)->value});
}

std::optional<std::string> Store::get(const std::string& key) {
    Entry* entry = cache_.get(key);
    if (!entry) {
        return std::nullopt;
    }
    if (is_expired(*entry)) {
        cache_.erase(key);  // lazy expiry; replay drops it anyway
        return std::nullopt;
    }
    return entry->value;
}

bool Store::del(const std::string& key) {
    const Entry* entry = cache_.peek(key);
    if (!entry) {
        return false;
    }
    const bool was_live = !is_expired(*entry);
    cache_.erase(key);
    if (was_live) {
        log({AofRecord::Op::Del, 0, key, ""});
    }
    return was_live;
}

bool Store::exists(const std::string& key) const {
    const Entry* entry = cache_.peek(key);
    return entry && !is_expired(*entry);
}

bool Store::expire(const std::string& key, Millis ttl) {
    if (ttl.count() <= 0) {
        throw std::invalid_argument("ttl must be positive");
    }
    Entry* entry = cache_.get(key);
    if (!entry || is_expired(*entry)) {
        return false;
    }
    entry->expires_at = options_.clock() + ttl;
    log({AofRecord::Op::Expire, to_epoch_ms(entry->expires_at), key, ""});
    return true;
}

std::int64_t Store::ttl_ms(const std::string& key) const {
    const Entry* entry = cache_.peek(key);
    if (!entry || is_expired(*entry)) {
        return -2;
    }
    if (!entry->expires_at) {
        return -1;
    }
    return std::chrono::duration_cast<Millis>(*entry->expires_at - options_.clock()).count();
}

std::size_t Store::purge_expired() {
    std::vector<std::string> expired;
    cache_.for_each([&](const std::string& key, const Entry& entry) {
        if (is_expired(entry)) {
            expired.push_back(key);
        }
    });
    for (const auto& key : expired) {
        cache_.erase(key);
    }
    return expired.size();
}

std::size_t Store::size() {
    purge_expired();
    return cache_.size();
}

void Store::compact() {
    if (!aof_) {
        throw std::logic_error("persistence is off");
    }
    purge_expired();
    std::vector<AofRecord> records;
    records.reserve(cache_.size());
    cache_.for_each([&](const std::string& key, const Entry& entry) {
        records.push_back({AofRecord::Op::Set, to_epoch_ms(entry.expires_at), key, entry.value});
    });
    // for_each goes from most to least recent. Write least recent first so a
    // replay rebuilds the same LRU order.
    std::reverse(records.begin(), records.end());
    aof_->rewrite(records);
}

Store::Stats Store::stats() {
    return Stats{size(), cache_.capacity(), evictions_, replayed_records_, repaired_aof_tail_};
}

}  // namespace kvlite

#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace kvlite {

// Fixed-capacity least-recently-used (LRU) cache.
//
// A doubly linked list keeps entries in recency order (front = most recently
// used). A hash map points from each key to its list node, so get, put and
// erase all run in O(1) average time. Moving a node to the front is a
// std::list::splice, which relinks pointers and never copies the value.
template <typename K, typename V>
class LruCache {
public:
    using EvictCallback = std::function<void(const K&, const V&)>;

    explicit LruCache(std::size_t capacity, EvictCallback on_evict = nullptr)
        : capacity_(capacity), on_evict_(std::move(on_evict)) {
        if (capacity_ == 0) {
            throw std::invalid_argument("capacity must be greater than 0");
        }
    }

    // Returns the value and marks it most recently used, or nullptr if absent.
    V* get(const K& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return nullptr;
        }
        order_.splice(order_.begin(), order_, it->second);
        return &it->second->second;
    }

    // Looks a key up without changing its recency.
    const V* peek(const K& key) const {
        auto it = index_.find(key);
        return it == index_.end() ? nullptr : &it->second->second;
    }

    // Inserts or replaces a value. If the cache is full and the key is new,
    // the least recently used entry is evicted first.
    void put(const K& key, V value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        if (order_.size() == capacity_) {
            evict_least_recent();
        }
        order_.emplace_front(key, std::move(value));
        index_.emplace(key, order_.begin());
    }

    // Removes a key. Returns true if it was present.
    bool erase(const K& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return false;
        }
        order_.erase(it->second);
        index_.erase(it);
        return true;
    }

    std::size_t size() const { return order_.size(); }
    std::size_t capacity() const { return capacity_; }

    // Visits entries from most recently used to least recently used.
    template <typename F>
    void for_each(F&& visit) const {
        for (const auto& entry : order_) {
            visit(entry.first, entry.second);
        }
    }

private:
    void evict_least_recent() {
        const auto& victim = order_.back();
        if (on_evict_) {
            on_evict_(victim.first, victim.second);
        }
        index_.erase(victim.first);
        order_.pop_back();
    }

    using Node = std::pair<K, V>;

    std::size_t capacity_;
    EvictCallback on_evict_;
    std::list<Node> order_;
    std::unordered_map<K, typename std::list<Node>::iterator> index_;
};

}  // namespace kvlite

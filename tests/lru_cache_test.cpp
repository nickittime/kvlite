#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "kvlite/lru_cache.hpp"

using kvlite::LruCache;

TEST(LruCache, RejectsZeroCapacity) {
    EXPECT_THROW((LruCache<int, int>(0)), std::invalid_argument);
}

TEST(LruCache, GetReturnsStoredValue) {
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    ASSERT_NE(cache.get("a"), nullptr);
    EXPECT_EQ(*cache.get("a"), 1);
    EXPECT_EQ(cache.get("missing"), nullptr);
}

TEST(LruCache, EvictsLeastRecentlyUsed) {
    std::vector<std::string> evicted;
    LruCache<std::string, int> cache(2, [&](const std::string& k, const int&) { evicted.push_back(k); });
    cache.put("a", 1);
    cache.put("b", 2);
    cache.get("a");     // a is now most recent, so b is least recent
    cache.put("c", 3);  // evicts b
    EXPECT_EQ(evicted, std::vector<std::string>{"b"});
    EXPECT_EQ(cache.get("b"), nullptr);
    EXPECT_NE(cache.get("a"), nullptr);
    EXPECT_NE(cache.get("c"), nullptr);
}

TEST(LruCache, PeekDoesNotChangeOrder) {
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    cache.peek("a");    // not a "use"
    cache.put("c", 3);  // a is still least recent
    EXPECT_EQ(cache.peek("a"), nullptr);
}

TEST(LruCache, UpdatingExistingKeyDoesNotEvict) {
    int evictions = 0;
    LruCache<std::string, int> cache(2, [&](const std::string&, const int&) { ++evictions; });
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("a", 10);
    EXPECT_EQ(evictions, 0);
    EXPECT_EQ(*cache.get("a"), 10);
    EXPECT_EQ(cache.size(), 2u);
}

TEST(LruCache, EraseFreesASlot) {
    LruCache<std::string, int> cache(1);
    cache.put("a", 1);
    EXPECT_TRUE(cache.erase("a"));
    EXPECT_FALSE(cache.erase("a"));
    EXPECT_EQ(cache.size(), 0u);
}

// Property test: 20,000 random operations, checked step by step against a
// simple reference model (a vector kept in recency order).
TEST(LruCache, MatchesReferenceModelOnRandomOperations) {
    constexpr std::size_t kCapacity = 8;
    LruCache<int, int> cache(kCapacity);
    std::vector<std::pair<int, int>> model;  // front = most recent

    auto find = [&](int key) {
        return std::find_if(model.begin(), model.end(), [&](const auto& kv) { return kv.first == key; });
    };

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> key_dist(0, 20);
    std::uniform_int_distribution<int> op_dist(0, 2);

    for (int step = 0; step < 20000; ++step) {
        const int key = key_dist(rng);
        switch (op_dist(rng)) {
            case 0: {  // put
                const int value = step;
                cache.put(key, value);
                auto it = find(key);
                if (it != model.end()) {
                    model.erase(it);
                } else if (model.size() == kCapacity) {
                    model.pop_back();
                }
                model.insert(model.begin(), {key, value});
                break;
            }
            case 1: {  // get
                int* got = cache.get(key);
                auto it = find(key);
                if (it == model.end()) {
                    ASSERT_EQ(got, nullptr) << "step " << step;
                } else {
                    ASSERT_NE(got, nullptr) << "step " << step;
                    ASSERT_EQ(*got, it->second) << "step " << step;
                    auto kv = *it;
                    model.erase(it);
                    model.insert(model.begin(), kv);
                }
                break;
            }
            default: {  // erase
                auto it = find(key);
                ASSERT_EQ(cache.erase(key), it != model.end()) << "step " << step;
                if (it != model.end()) {
                    model.erase(it);
                }
            }
        }
        ASSERT_EQ(cache.size(), model.size()) << "step " << step;
    }

    std::vector<std::pair<int, int>> order;
    cache.for_each([&](const int& k, const int& v) { order.emplace_back(k, v); });
    EXPECT_EQ(order, model);
}

#include <gtest/gtest.h>

#include "kvlite/store.hpp"
#include "test_helpers.hpp"

using kvlite::Millis;
using kvlite::Store;
using kvlite_test::FakeClock;

namespace {

Store make_store(FakeClock& clock, std::size_t capacity = 100) {
    Store::Options options;
    options.capacity = capacity;
    options.clock = clock.fn();
    return Store(options);
}

}  // namespace

TEST(Store, SetGetDelete) {
    FakeClock clock;
    Store store = make_store(clock);
    store.set("lang", "c++");
    EXPECT_EQ(store.get("lang"), "c++");
    EXPECT_TRUE(store.exists("lang"));
    EXPECT_TRUE(store.del("lang"));
    EXPECT_FALSE(store.del("lang"));
    EXPECT_EQ(store.get("lang"), std::nullopt);
}

TEST(Store, KeyExpiresAfterTtl) {
    FakeClock clock;
    Store store = make_store(clock);
    store.set("session", "abc", Millis(1000));
    clock.advance(Millis(999));
    EXPECT_EQ(store.get("session"), "abc");
    clock.advance(Millis(1));
    EXPECT_EQ(store.get("session"), std::nullopt);
    EXPECT_FALSE(store.exists("session"));
}

TEST(Store, TtlFollowsRedisConventions) {
    FakeClock clock;
    Store store = make_store(clock);
    store.set("forever", "1");
    store.set("brief", "2", Millis(5000));
    EXPECT_EQ(store.ttl_ms("missing"), -2);
    EXPECT_EQ(store.ttl_ms("forever"), -1);
    EXPECT_EQ(store.ttl_ms("brief"), 5000);
    clock.advance(Millis(2000));
    EXPECT_EQ(store.ttl_ms("brief"), 3000);
}

TEST(Store, ExpireAddsTtlToExistingKeyOnly) {
    FakeClock clock;
    Store store = make_store(clock);
    store.set("k", "v");
    EXPECT_TRUE(store.expire("k", Millis(100)));
    EXPECT_FALSE(store.expire("missing", Millis(100)));
    clock.advance(Millis(100));
    EXPECT_FALSE(store.exists("k"));
}

TEST(Store, SetWithoutTtlClearsOldTtl) {
    FakeClock clock;
    Store store = make_store(clock);
    store.set("k", "v1", Millis(100));
    store.set("k", "v2");
    clock.advance(Millis(1000));
    EXPECT_EQ(store.get("k"), "v2");
}

TEST(Store, RejectsNonPositiveTtl) {
    FakeClock clock;
    Store store = make_store(clock);
    EXPECT_THROW(store.set("k", "v", Millis(0)), std::invalid_argument);
}

TEST(Store, EvictsLeastRecentlyUsedAtCapacity) {
    FakeClock clock;
    Store store = make_store(clock, 2);
    store.set("a", "1");
    store.set("b", "2");
    store.get("a");
    store.set("c", "3");
    EXPECT_FALSE(store.exists("b"));
    EXPECT_TRUE(store.exists("a"));
    EXPECT_TRUE(store.exists("c"));
    EXPECT_EQ(store.stats().evictions, 1u);
}

TEST(Store, SizeCountsOnlyLiveKeys) {
    FakeClock clock;
    Store store = make_store(clock);
    store.set("a", "1", Millis(10));
    store.set("b", "2");
    clock.advance(Millis(10));
    EXPECT_EQ(store.size(), 1u);
}

TEST(Store, CompactWithoutPersistenceThrows) {
    FakeClock clock;
    Store store = make_store(clock);
    EXPECT_THROW(store.compact(), std::logic_error);
}

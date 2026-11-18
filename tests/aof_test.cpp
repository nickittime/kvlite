#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "kvlite/store.hpp"
#include "test_helpers.hpp"

using kvlite::Millis;
using kvlite::Store;
using kvlite_test::FakeClock;
using kvlite_test::TempFile;

namespace {

Store open_store(const std::string& path, FakeClock& clock, std::size_t capacity = 100) {
    Store::Options options;
    options.capacity = capacity;
    options.aof_path = path;
    options.clock = clock.fn();
    return Store(options);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(Aof, DataSurvivesRestart) {
    TempFile file;
    FakeClock clock;
    {
        Store store = open_store(file.path(), clock);
        store.set("a", "1");
        store.set("b", "2");
        store.del("a");
    }
    Store reopened = open_store(file.path(), clock);
    EXPECT_FALSE(reopened.exists("a"));
    EXPECT_EQ(reopened.get("b"), "2");
    EXPECT_EQ(reopened.stats().replayed_records, 3u);
}

TEST(Aof, ValuesWithSpacesAndNewlinesRoundTrip) {
    TempFile file;
    FakeClock clock;
    const std::string tricky = "line one\nline two with spaces \"and quotes\"";
    {
        Store store = open_store(file.path(), clock);
        store.set("key with spaces", tricky);
    }
    Store reopened = open_store(file.path(), clock);
    EXPECT_EQ(reopened.get("key with spaces"), tricky);
}

TEST(Aof, KeysThatExpiredWhileDownAreDropped) {
    TempFile file;
    FakeClock clock;
    {
        Store store = open_store(file.path(), clock);
        store.set("short", "x", Millis(1000));
        store.set("long", "y", Millis(60000));
    }
    clock.advance(Millis(5000));  // "process was down" for 5 seconds
    Store reopened = open_store(file.path(), clock);
    EXPECT_FALSE(reopened.exists("short"));
    EXPECT_EQ(reopened.ttl_ms("long"), 55000);
}

TEST(Aof, EvictionsReplayToTheSameState) {
    TempFile file;
    FakeClock clock;
    {
        Store store = open_store(file.path(), clock, 2);
        store.set("a", "1");
        store.set("b", "2");
        store.get("a");      // reads aren't logged...
        store.set("c", "3");  // ...so the eviction of b is logged as a delete
    }
    Store reopened = open_store(file.path(), clock, 2);
    EXPECT_TRUE(reopened.exists("a"));
    EXPECT_FALSE(reopened.exists("b"));
    EXPECT_TRUE(reopened.exists("c"));
}

TEST(Aof, IncompleteLastRecordIsCutOff) {
    TempFile file;
    FakeClock clock;
    {
        Store store = open_store(file.path(), clock);
        store.set("good", "kept");
    }
    const auto good_size = std::filesystem::file_size(file.path());
    {
        // Simulate a crash in the middle of writing the next record.
        std::ofstream out(file.path(), std::ios::binary | std::ios::app);
        out << "S 0 4 10\nhalfwri";
    }
    Store reopened = open_store(file.path(), clock);
    EXPECT_TRUE(reopened.stats().repaired_aof_tail);
    EXPECT_EQ(reopened.get("good"), "kept");
    EXPECT_EQ(std::filesystem::file_size(file.path()), good_size);

    reopened.set("after", "crash");  // new writes land after the last good record
    Store again = open_store(file.path(), clock);
    EXPECT_FALSE(again.stats().repaired_aof_tail);
    EXPECT_EQ(again.get("after"), "crash");
}

TEST(Aof, CorruptHeaderStopsReplayThere) {
    TempFile file;
    FakeClock clock;
    {
        Store store = open_store(file.path(), clock);
        store.set("a", "1");
    }
    {
        std::ofstream out(file.path(), std::ios::binary | std::ios::app);
        out << "this is not a record\n";
    }
    Store reopened = open_store(file.path(), clock);
    EXPECT_TRUE(reopened.stats().repaired_aof_tail);
    EXPECT_EQ(reopened.get("a"), "1");
}

TEST(Aof, CompactShrinksFileAndKeepsData) {
    TempFile file;
    FakeClock clock;
    {
        Store store = open_store(file.path(), clock);
        for (int i = 0; i < 100; ++i) {
            store.set("counter", std::to_string(i));
        }
        store.set("other", "x");
        const auto before = std::filesystem::file_size(file.path());
        store.compact();
        EXPECT_LT(std::filesystem::file_size(file.path()), before);
        store.set("after", "compact");  // appending still works after the rewrite
    }
    Store reopened = open_store(file.path(), clock);
    EXPECT_EQ(reopened.get("counter"), "99");
    EXPECT_EQ(reopened.get("other"), "x");
    EXPECT_EQ(reopened.get("after"), "compact");
    EXPECT_EQ(reopened.stats().replayed_records, 3u);
}

TEST(Aof, CompactPreservesLruOrder) {
    TempFile file;
    FakeClock clock;
    {
        Store store = open_store(file.path(), clock, 3);
        store.set("a", "1");
        store.set("b", "2");
        store.set("c", "3");
        store.get("a");  // order is now a (most recent), c, b (least recent)
        store.compact();
    }
    Store reopened = open_store(file.path(), clock, 3);
    reopened.set("d", "4");  // must evict b, the least recent
    EXPECT_FALSE(reopened.exists("b"));
    EXPECT_TRUE(reopened.exists("a"));
    EXPECT_TRUE(reopened.exists("c"));
}

TEST(Aof, RecordFormatIsReadable) {
    TempFile file;
    FakeClock clock;
    {
        Store store = open_store(file.path(), clock);
        store.set("k", "hello");
    }
    EXPECT_EQ(read_file(file.path()), "S 0 1 5\nkhello\n");
}

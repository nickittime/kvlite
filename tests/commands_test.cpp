#include <gtest/gtest.h>

#include "kvlite/commands.hpp"
#include "test_helpers.hpp"

using kvlite::CommandError;
using kvlite::execute;
using kvlite::Store;
using kvlite::tokenize;
using kvlite_test::FakeClock;

using Words = std::vector<std::string>;

TEST(Tokenize, SplitsOnWhitespace) {
    EXPECT_EQ(tokenize("  SET  a   1 "), (Words{"SET", "a", "1"}));
    EXPECT_TRUE(tokenize("   ").empty());
}

TEST(Tokenize, QuotesGroupWordsAndSupportEscapes) {
    EXPECT_EQ(tokenize(R"(SET greeting "hello world")"), (Words{"SET", "greeting", "hello world"}));
    EXPECT_EQ(tokenize(R"(SET q "say \"hi\"\n")"), (Words{"SET", "q", "say \"hi\"\n"}));
    EXPECT_EQ(tokenize(R"(SET empty "")"), (Words{"SET", "empty", ""}));
}

TEST(Tokenize, UnterminatedQuoteIsAnError) {
    EXPECT_THROW(tokenize(R"(SET a "oops)"), CommandError);
}

class Commands : public ::testing::Test {
protected:
    Commands() : store_([this] {
          Store::Options options;
          options.capacity = 100;
          options.clock = clock_.fn();
          return options;
      }()) {}

    std::string run(const std::string& line) { return execute(store_, tokenize(line)); }

    FakeClock clock_;
    Store store_;
};

TEST_F(Commands, SetAndGet) {
    EXPECT_EQ(run("SET name kvlite"), "OK");
    EXPECT_EQ(run("get name"), "\"kvlite\"");  // commands are case-insensitive
    EXPECT_EQ(run("GET missing"), "(nil)");
}

TEST_F(Commands, SetWithExpiry) {
    EXPECT_EQ(run("SET token abc EX 10"), "OK");
    EXPECT_EQ(run("TTL token"), "(integer) 10");
    clock_.advance(kvlite::Millis(10000));
    EXPECT_EQ(run("GET token"), "(nil)");
    EXPECT_EQ(run("TTL token"), "(integer) -2");
}

TEST_F(Commands, SetWithMillisecondExpiry) {
    EXPECT_EQ(run("SET t v PX 1500"), "OK");
    EXPECT_EQ(run("TTL t"), "(integer) 2");  // rounds to the nearest second
}

TEST_F(Commands, DelExistsExpire) {
    run("SET a 1");
    EXPECT_EQ(run("EXISTS a"), "(integer) 1");
    EXPECT_EQ(run("EXPIRE a 5"), "(integer) 1");
    EXPECT_EQ(run("DEL a"), "(integer) 1");
    EXPECT_EQ(run("DEL a"), "(integer) 0");
    EXPECT_EQ(run("EXPIRE a 5"), "(integer) 0");
}

TEST_F(Commands, BadInputGivesClearErrors) {
    EXPECT_THROW(run("SET onlykey"), CommandError);
    EXPECT_THROW(run("SET a 1 EX -5"), CommandError);
    EXPECT_THROW(run("SET a 1 EX 10abc"), CommandError);
    EXPECT_THROW(run("SET a 1 XX 10"), CommandError);
    EXPECT_THROW(run("FLY away"), CommandError);
    EXPECT_THROW(run("COMPACT"), CommandError);  // persistence is off in this fixture
}

TEST_F(Commands, SizeAndStats) {
    run("SET a 1");
    run("SET b 2");
    EXPECT_EQ(run("SIZE"), "(integer) 2");
    EXPECT_EQ(run("STATS"), "keys=2 capacity=100 evictions=0 replayed=0 persistence=off");
}

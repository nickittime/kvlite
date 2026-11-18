#pragma once

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

#include "kvlite/store.hpp"

namespace kvlite_test {

// A clock the test moves by hand, so expiry tests never sleep.
struct FakeClock {
    kvlite::TimePoint now{std::chrono::seconds(1'700'000'000)};

    std::function<kvlite::TimePoint()> fn() {
        return [this] { return now; };
    }
    void advance(kvlite::Millis by) { now += by; }
};

// A per-test file path in the system temp folder, deleted when the test ends.
class TempFile {
public:
    TempFile() {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = (std::filesystem::temp_directory_path() /
                 (std::string("kvlite_") + info->test_suite_name() + "_" + info->name() + ".aof"))
                    .string();
        std::filesystem::remove(path_);
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        std::filesystem::remove(path_ + ".tmp", ec);
    }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

}  // namespace kvlite_test

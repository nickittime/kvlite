#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "kvlite/store.hpp"

namespace kvlite {

// Thrown for bad input: unknown command, wrong arguments, unterminated quote.
class CommandError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Splits a command line into words. Double quotes group words, and inside
// quotes \" \\ and \n are escapes:  SET greeting "hello world"
std::vector<std::string> tokenize(std::string_view line);

// Runs one command against the store and returns the reply text,
// formatted like redis-cli: OK, "value", (nil), (integer) 1.
std::string execute(Store& store, const std::vector<std::string>& args);

// Help text listing every command.
std::string help_text();

}  // namespace kvlite

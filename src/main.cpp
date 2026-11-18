// kvlite command-line shell: reads commands from stdin, one per line.
//
//   kvlite --capacity 1000 --aof data.aof
//   echo 'SET a 1' | kvlite

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <io.h>
#define KVLITE_ISATTY _isatty
#define KVLITE_FILENO _fileno
#else
#include <unistd.h>
#define KVLITE_ISATTY isatty
#define KVLITE_FILENO fileno
#endif

#include "kvlite/commands.hpp"
#include "kvlite/store.hpp"

namespace {

void usage() {
    std::cerr << "usage: kvlite [--capacity N] [--aof FILE]\n"
                 "  --capacity N  maximum number of keys before LRU eviction (default 10000)\n"
                 "  --aof FILE    persist every write to FILE and replay it on startup\n";
}

}  // namespace

int main(int argc, char** argv) {
    kvlite::Store::Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--capacity" || arg == "--aof") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (arg == "--capacity") {
                try {
                    options.capacity = static_cast<std::size_t>(std::stoull(value));
                } catch (const std::exception&) {
                    usage();
                    return EXIT_FAILURE;
                }
            } else {
                options.aof_path = value;
            }
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return EXIT_SUCCESS;
        } else {
            usage();
            return EXIT_FAILURE;
        }
    }

    try {
        kvlite::Store store(options);
        const bool interactive = KVLITE_ISATTY(KVLITE_FILENO(stdin)) != 0;

        if (interactive) {
            const auto stats = store.stats();
            std::cout << "kvlite: capacity " << stats.capacity << ", persistence "
                      << (store.persistent() ? "on (" + options.aof_path + ")" : "off");
            if (store.persistent()) {
                std::cout << ", replayed " << stats.replayed_records << " records";
            }
            std::cout << "\nType HELP for commands.\n";
        }
        if (store.stats().repaired_aof_tail) {
            std::cerr << "warning: append-only file ended with an incomplete record; it was cut off\n";
        }

        std::string line;
        while (true) {
            if (interactive) {
                std::cout << "kvlite> " << std::flush;
            }
            if (!std::getline(std::cin, line)) {
                break;
            }
            try {
                const auto args = kvlite::tokenize(line);
                if (!args.empty() && (args[0] == "QUIT" || args[0] == "quit" ||
                                      args[0] == "EXIT" || args[0] == "exit")) {
                    break;
                }
                const std::string reply = kvlite::execute(store, args);
                if (!reply.empty()) {
                    std::cout << reply << '\n';
                }
            } catch (const std::exception& e) {
                std::cout << "(error) ERR " << e.what() << '\n';
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "kvlite: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

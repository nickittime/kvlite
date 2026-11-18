# Walkthrough: read this before you talk about kvlite in an interview

Read the code in this order. Each file is short.

1. `include/kvlite/lru_cache.hpp`: the data structure. About 100 lines.
2. `include/kvlite/store.hpp` and `src/store.cpp`: expiry, plus how the cache and the file are wired together.
3. `include/kvlite/aof.hpp` and `src/aof.cpp`: the file format and crash recovery.
4. `src/commands.cpp`: turning a typed line into a call on the store.
5. `src/main.cpp`: the read-eval-print loop.
6. `tests/`: run them, then break something on purpose and watch which test fails.

## Exercises to make it yours

Do at least two. Then you've written part of it, and you can explain it from memory.

- Add an `INCR key` command: parse the value as an integer, add 1, store it, and log it as a SET. Write the tests first.
- Add `KEYS` to list live keys. Then decide whether listing should change LRU order, and say why.
- Add a CRC32 to each record header, and a test that flips one byte in the middle of the file.
- Change capacity to count bytes instead of keys.
- Add a TCP server: one thread accepting connections, a mutex around the store. Then talk about why a single global lock limits throughput.

## Questions you will likely get, with short answers

**Why a list plus a hash map?**
The map gives O(1) lookup by key. The list gives O(1) "move to front" and "remove from back", because you already have the node's iterator. Either one alone makes one of those operations O(n).

**Why `std::list` and not `std::vector`?**
Moving an element to the front of a vector is O(n) and invalidates iterators. That would break the iterators stored in the map. `std::list::splice` is O(1) and keeps every iterator valid.

**What is the cost per node?**
Two pointers for the list, plus a hash map entry holding the key again and an iterator. Storing the key twice is the usual trade-off. A fix is to store only a pointer or `string_view` to the key in one of them.

**Why lazy expiry?**
No background thread means no locking. The cost is that expired keys use memory until they're read or swept. Redis combines lazy expiry with sampling a few keys periodically.

**Why log evictions?**
Replay re-runs writes but not reads. Reads change which key is least recent, so without the log, replay could evict a different key than the live run did.

**What happens if the process is killed mid-write?**
The last record is incomplete. On startup, `Aof::load` sees that the length doesn't match or the newline is missing, keeps everything before it, and cuts the file there.

**What about a power cut?**
`flush()` hands bytes to the OS but doesn't force them to disk. You'd need `fsync` after each write (slow) or every N milliseconds (Redis's `appendfsync everysec`). That's a durability-versus-speed trade-off.

**Why is `Store` not copyable or movable?**
The eviction callback captures `this`. A copy would call back into the old object. Deleting the copy and move operations makes that mistake a compile error.

**How is time tested without sleeping?**
The clock is a `std::function` in `Store::Options`. Tests pass a fake clock and move it forward by hand.

**What does the randomized test prove?**
It runs 20,000 random get, put and erase operations on the cache and on a simple vector model, and compares them after every step. It catches ordering bugs that hand-written cases miss.

**What would you change for production?**
Concurrency (sharded locks), a network protocol, fsync policy, checksums, memory-based limits, and metrics.

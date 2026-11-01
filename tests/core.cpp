#include "knuth/numeric.hpp"
#include "knuth/visited.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message); // active in Release builds
}
void check_root(knuth::u64 n) {
    const auto r = knuth::integer_sqrt(n);
    require(static_cast<__uint128_t>(r) * r <= n, "sqrt lower bound");
    require(static_cast<__uint128_t>(r + 1) * (r + 1) > n, "sqrt upper bound");
}
}
int main() {
    try {
        std::mt19937_64 random(20261031);
        for (knuth::u64 n = 0; n < 100000; ++n) check_root(n);
        for (int i = 0; i < 100000; ++i) check_root(random());
        for (knuth::u64 r : {UINT64_C(1), UINT64_C(2), UINT64_C(65536),
                             UINT64_C(1000000000), UINT64_C(4294967295)}) {
            const auto n = r * r;
            check_root(n - 1); check_root(n); check_root(n + 1);
        }
        check_root(UINT64_MAX);
        for (unsigned shift : {0u, 6u, 12u}) {
            knuth::FlatSet set;
            set.initialize(shift, 3);
            std::unordered_set<knuth::u64> reference;
            for (int i = 0; i < 100000; ++i) {
                const auto n = (i % 2 ? random() : static_cast<knuth::u64>(i + 1)) | 1;
                const bool expected = reference.insert(n).second;
                require(set.insert(n, knuth::mix(n)) == expected, "flat-set insertion");
                require(!set.insert(n, knuth::mix(n)), "flat-set duplicate");
            }
            require(set.insert(UINT64_MAX, knuth::mix(UINT64_MAX)) == reference.insert(UINT64_MAX).second,
                    "flat-set maximum key");
            std::vector<knuth::u64> actual;
            set.append_to(actual);
            require(actual.size() == reference.size(), "flat-set size");
            for (auto n : actual) require(reference.count(n) == 1, "flat-set contents");
        }
        for (unsigned shards : {1u, 64u}) {
            knuth::Visited visited(8, 200000, shards, 100);
            std::vector<std::thread> threads;
            std::vector<unsigned> wins(4, 0);
            for (unsigned worker = 0; worker < 4; ++worker)
                threads.emplace_back([&, worker] {
                    for (knuth::u64 n = 1; n <= 50000; ++n)
                        wins[worker] += visited.insert(n);
                });
            for (auto& thread : threads) thread.join();
            require(wins[0] + wins[1] + wins[2] + wins[3] == 50000, "unique insertion ownership");
            auto all = visited.snapshot();
            std::sort(all.begin(), all.end());
            require(all.size() == 50000, "visited count");
            for (std::size_t i = 0; i < all.size(); ++i) require(all[i] == i + 1, "visited contents");
            require(visited.first_missing() == 256, "bitmap exhaustion");
            require(visited.large_count() == 50000 - 255, "large count");
        }
        knuth::Visited holes(8, 250, 1, 0);
        for (knuth::u64 n = 1; n < 250; ++n) {
            require(holes.first_missing() == n, "wordwise first missing");
            holes.insert(n);
        }
        require(holes.first_missing() == 250, "partial last word");
        holes.insert(250);
        require(holes.first_missing() == 251, "partial bitmap exhaustion");
        std::cout << "integer roots, flat sets, and concurrent visited ownership passed\n";
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n'; return 1;
    }
}

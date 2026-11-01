// Exploratory search for Knuth's one-four conjecture (equivalent start: 3).
// This program reports numerical evidence, NOT certified constructions.
#include <quadmath.h>
#include <concurrentqueue.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

uint64_t parse_integer(const std::string& value) {
    std::size_t used = 0;
    const auto result = std::stoull(value, &used);
    if (used != value.size()) throw std::invalid_argument("invalid integer: " + value);
    return static_cast<uint64_t>(result);
}

// Integer square root, with floating point used only for an initial estimate.
uint64_t integer_sqrt(uint64_t x) {
    if (x == 0) return 0;
    auto r = static_cast<uint64_t>(std::sqrt(static_cast<long double>(x)));
    while (r + 1 <= x / (r + 1)) ++r;
    while (r > x / r) --r;
    return r;
}

class Visited {
public:
    explicit Visited(unsigned bitmap_log2)
        : limit_(uint64_t{1} << bitmap_log2),
          size_((limit_ + 63) / 64),
          words_(new std::atomic<uint64_t>[size_]) {
        for (std::size_t i = 0; i < size_; ++i)
            words_[i].store(0, std::memory_order_relaxed);
    }

    bool insert(uint64_t n) {
        if (n < limit_) {
            const uint64_t mask = uint64_t{1} << (n % 64);
            const auto old = words_[n / 64].fetch_or(mask, std::memory_order_relaxed);
            return (old & mask) == 0;
        }
        std::lock_guard<std::mutex> lock(large_mutex_);
        return large_.insert(n).second;
    }

    uint64_t first_missing() const {
        for (uint64_t n = 1; n < limit_; ++n) {
            if ((words_[n / 64].load(std::memory_order_relaxed) &
                 (uint64_t{1} << (n % 64))) == 0) return n;
        }
        return limit_; // only a lower bound if the bitmap range is exhausted
    }

    uint64_t bitmap_limit() const { return limit_; }

private:
    uint64_t limit_;
    std::size_t size_;
    std::unique_ptr<std::atomic<uint64_t>[]> words_;
    std::mutex large_mutex_;
    std::unordered_set<uint64_t> large_;
};

struct Options {
    uint64_t bound = 1000000000000000ULL; // historical candidate ceiling
    unsigned bitmap_log2 = 24;             // 2 MiB by default; 32 -> 512 MiB
    unsigned threads = 1;
};

Options options_from_args(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "Usage: knuth_search [--threads N] [--bound N] [--bitmap-log2 N]\n"
                         "Computations are exploratory, not mathematically certified.\n";
            std::exit(0);
        }
        if (i + 1 >= argc) throw std::invalid_argument("missing value for " + arg);
        const auto value = parse_integer(argv[++i]);
        if (arg == "--threads") {
            if (value < 1 || value > 1024) throw std::invalid_argument("threads must be 1..1024");
            opts.threads = static_cast<unsigned>(value);
        } else if (arg == "--bound") {
            if (value < 6 || value > 1000000000000000ULL)
                throw std::invalid_argument("bound must be in [6, 10^15]");
            opts.bound = value;
        } else if (arg == "--bitmap-log2") {
            if (value < 6 || value > 32) throw std::invalid_argument("bitmap-log2 must be 6..32");
            opts.bitmap_log2 = static_cast<unsigned>(value);
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }
    return opts;
}

class Search {
public:
    explicit Search(const Options& options)
        : options_(options), visited_(options.bitmap_log2), log_bound_(logq(static_cast<__float128>(options.bound))) {
        visited_.insert(3);
        if (!queue_.enqueue(uint64_t{3})) throw std::bad_alloc();
        pending_.store(1, std::memory_order_relaxed);
        discovered_.store(1, std::memory_order_relaxed);
    }

    void run() {
        std::vector<std::thread> workers;
        workers.reserve(options_.threads);
        for (unsigned i = 0; i < options_.threads; ++i)
            workers.emplace_back([this] { worker(); });
        for (auto& worker : workers) worker.join();
    }

    void report() const {
        const auto first = visited_.first_missing();
        std::cout << "first_missing=" << first
                  << " bitmap_limit=" << visited_.bitmap_limit()
                  << " discovered=" << discovered_.load(std::memory_order_relaxed)
                  << " processed=" << processed_.load(std::memory_order_relaxed)
                  << " threads=" << options_.threads << "\n";
        if (first == visited_.bitmap_limit())
            std::cout << "note=bitmap_range_exhausted\n";
        std::cout << "status=uncertified_numerical_search\n";
    }

private:
    // Compute floor((m!)^(1 / 2^p)) with lgammaq/expq. p=0 follows
    // the original code's round-to-nearest convention. Rounding errors
    // near integral boundaries are not yet certified.
    uint64_t candidate(uint64_t m) const {
        const __float128 x = lgammaq(static_cast<__float128>(m) + 1.0Q);
        unsigned p = 0;
        if (x > log_bound_) {
            const __float128 needed = ceilq(log2q(x / log_bound_));
            p = static_cast<unsigned>(needed > 0 ? needed : 0);
        }
        const __float128 exponent = x / ldexpq(1.0Q, static_cast<int>(p));
        const __float128 y = expq(exponent);
        const __float128 z = p == 0 ? roundq(y) : floorq(y);
        // A finite limit prevents casting outside the integer range.
        if (!finiteq(z) || z < 1 || z > static_cast<__float128>(options_.bound))
            throw std::runtime_error("candidate outside configured numerical bound");
        return static_cast<uint64_t>(z);
    }

    void worker() {
        uint64_t m;
        for (;;) {
            if (!queue_.try_dequeue(m)) {
                // try_dequeue may fail spuriously: outstanding work is the
                // completion criterion, not the apparent queue emptiness.
                if (pending_.load(std::memory_order_acquire) == 0) return;
                std::this_thread::yield();
                continue;
            }
            uint64_t n = candidate(m);
            while (visited_.insert(n)) {
                // Increase outstanding count BEFORE publishing the task.
                pending_.fetch_add(1, std::memory_order_acq_rel);
                if (!queue_.enqueue(n)) std::terminate(); // OOM is unrecoverable here
                discovered_.fetch_add(1, std::memory_order_relaxed);
                n = integer_sqrt(n);
            }
            processed_.fetch_add(1, std::memory_order_relaxed);
            pending_.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    Options options_;
    Visited visited_;
    __float128 log_bound_;
    moodycamel::ConcurrentQueue<uint64_t> queue_;
    std::atomic<uint64_t> pending_{0};
    std::atomic<uint64_t> discovered_{0};
    std::atomic<uint64_t> processed_{0};
};

} // namespace

int main(int argc, char** argv) {
    try {
        Search search(options_from_args(argc, argv));
        search.run();
        search.report();
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}

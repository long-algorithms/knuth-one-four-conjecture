// Exploratory search for Knuth's one-four conjecture. Not a proof certificate.
#include "knuth/numeric.hpp"
#include "knuth/visited.hpp"
#include <concurrentqueue.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using knuth::u64;
struct Options {
    u64 bound = UINT64_C(100000000);
    u64 seed = 4;
    unsigned bitmap_log2 = 24;
    unsigned threads = 1;
    unsigned batch_size = 64;
    unsigned shards = 64;
    unsigned inline_depth = 8;
    std::size_t large_reserve = 0;
    u64 candidate = 0;
    std::string dump_reached;
};

u64 parse_integer(const std::string& text) {
    u64 value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("invalid unsigned integer: " + text);
    return value;
}

void usage() {
    std::cout <<
        "Usage: knuth_search [options]\n"
        "  --seed N          Starting integer (default 4; 3 reproduces older searches)\n"
        "  --bound N         Inclusive candidate ceiling, 1..UINT64_MAX (default 10^8)\n"
        "  --threads N       Worker count, 1..1024 (default 1)\n"
        "  --bitmap-log2 N   Bitmap address bits, 6..40 (default 24; 32 uses 512 MiB)\n"
        "  --batch-size N    Maximum tasks per queue operation, 1..4096 (default 64)\n"
        "  --inline-depth N  Follow up to N new large successors inline, 0..1024 (default 8)\n"
        "  --shards N        Power-of-two large-set shard count, 1..4096 (default 64)\n"
        "  --large-reserve N Initially reserve this many large states (default 0)\n"
        "  --dump-reached F  Write the sorted visited set after completion (uses memory)\n"
        "  --candidate N     Evaluate one factorial/root edge instead of searching\n"
        "  --help            Show this help\n"
        "Numerical edges are exploratory, not mathematically certified.\n";
}

Options options_from_args(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") { usage(); std::exit(0); }
        if (i + 1 == argc) throw std::invalid_argument("missing value for " + arg);
        const std::string text = argv[++i];
        if (arg == "--dump-reached") {
            if (text.empty()) throw std::invalid_argument("dump path must not be empty");
            opts.dump_reached = text; continue;
        }
        const auto value = parse_integer(text);
        if (arg == "--bound") {
            if (!value) throw std::invalid_argument("bound must be positive");
            opts.bound = value;
        } else if (arg == "--seed") {
            if (!value) throw std::invalid_argument("seed must be positive");
            opts.seed = value;
        } else if (arg == "--threads") {
            if (value < 1 || value > 1024) throw std::invalid_argument("threads must be 1..1024");
            opts.threads = static_cast<unsigned>(value);
        } else if (arg == "--bitmap-log2") {
            if (value < 6 || value > 40) throw std::invalid_argument("bitmap-log2 must be 6..40");
            opts.bitmap_log2 = static_cast<unsigned>(value);
        } else if (arg == "--batch-size") {
            if (value < 1 || value > 4096) throw std::invalid_argument("batch-size must be 1..4096");
            opts.batch_size = static_cast<unsigned>(value);
        } else if (arg == "--inline-depth") {
            if (value > 1024) throw std::invalid_argument("inline-depth must be 0..1024");
            opts.inline_depth = static_cast<unsigned>(value);
        } else if (arg == "--shards") {
            if (!value || value > 4096 || (value & (value - 1)))
                throw std::invalid_argument("shards must be a power of two in 1..4096");
            opts.shards = static_cast<unsigned>(value);
        } else if (arg == "--large-reserve") {
            if (value > std::numeric_limits<std::size_t>::max() / sizeof(u64))
                throw std::invalid_argument("large-reserve exceeds addressable storage");
            opts.large_reserve = static_cast<std::size_t>(value);
        } else if (arg == "--candidate") {
            if (!value) throw std::invalid_argument("candidate must be positive");
            opts.candidate = value;
        } else throw std::invalid_argument("unknown option: " + arg);
    }
    if (!opts.candidate && opts.seed > opts.bound)
        throw std::invalid_argument("seed must not exceed bound");
    return opts;
}

struct alignas(64) Statistics {
    u64 processed = 0;
    u64 discovered = 0;
    u64 batches = 0;
    u64 inlined = 0;
};

class Search {
public:
    explicit Search(Options options)
        : options_(std::move(options)),
          visited_(options_.bitmap_log2, options_.bound, options_.shards, options_.large_reserve),
          candidate_(options_.bound), stats_(options_.threads) {
        // The seed must obey the same sqrt-closure invariant as discoveries.
        // Otherwise, e.g. seed=4/bound=6 misses the valid states 2 and 1.
        std::vector<u64> initial;
        for (auto n = options_.seed; visited_.insert(n); n = knuth::integer_sqrt(n))
            initial.push_back(n);
        seeded_ = initial.size();
        pending_.store(seeded_, std::memory_order_relaxed);
        if (!queue_.enqueue_bulk(initial.data(), initial.size())) throw std::bad_alloc();
    }

    void run() {
        const auto started = std::chrono::steady_clock::now();
        std::vector<std::thread> workers;
        workers.reserve(options_.threads);
        try {
            for (unsigned i = 0; i < options_.threads; ++i)
                workers.emplace_back([this, i] {
                    try { worker(stats_[i]); }
                    catch (...) {
                        std::lock_guard<std::mutex> lock(error_mutex_);
                        if (!error_) error_ = std::current_exception();
                        aborted_.store(true, std::memory_order_release);
                    }
                });
        } catch (...) {
            aborted_.store(true, std::memory_order_release);
            for (auto& worker : workers) worker.join();
            throw;
        }
        for (auto& worker : workers) worker.join();
        elapsed_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (error_) std::rethrow_exception(error_);
    }

    void report() const {
        u64 discovered = seeded_, processed = 0, batches = 0, inlined = 0;
        for (const auto& stat : stats_) {
            discovered += stat.discovered; processed += stat.processed; batches += stat.batches; inlined += stat.inlined;
        }
        if (discovered != processed || pending_.load(std::memory_order_relaxed) != 0)
            throw std::runtime_error("incomplete search: outstanding-work invariant failed");
        if (!options_.dump_reached.empty()) {
            auto values = visited_.snapshot();
            std::sort(values.begin(), values.end());
            std::ofstream output(options_.dump_reached);
            if (!output) throw std::runtime_error("cannot open " + options_.dump_reached);
            for (auto n : values) output << n << '\n';
            output.close();
            if (!output) throw std::runtime_error("cannot write " + options_.dump_reached);
        }
        const auto first = visited_.first_missing();
        std::cout << "first_missing=" << first << " bitmap_limit=" << visited_.bitmap_limit()
                  << " discovered=" << discovered << " processed=" << processed
                  << " seed=" << options_.seed << " bound=" << options_.bound
                  << " threads=" << options_.threads << " batch_size=" << options_.batch_size
                  << " shards=" << options_.shards << " batches=" << batches
                  << " inline_depth=" << options_.inline_depth << " inlined=" << inlined << '\n'
                  << "bitmap_bytes=" << visited_.bitmap_bytes()
                  << " large_states=" << visited_.large_count()
                  << " large_capacity_bytes=" << visited_.large_capacity_bytes()
                  << " search_seconds=" << std::fixed << std::setprecision(6) << elapsed_ << '\n';
        if (first == visited_.bitmap_limit()) std::cout << "note=bitmap_range_exhausted\n";
        #ifdef KNUTH_HAVE_LGAMMAF128_R
        std::cout << "numerical_backend=glibc_lgammaf128_r\n";
#else
        std::cout << "numerical_backend=locked_lgammaq\n";
#endif
        std::cout << "status=uncertified_numerical_search\n";
    }

private:
    void worker(Statistics& stat) {
        moodycamel::ProducerToken producer(queue_);
        moodycamel::ConsumerToken consumer(queue_);
        if (!producer.valid()) throw std::bad_alloc();
        std::vector<u64> input(options_.batch_size), output;
        output.reserve(static_cast<std::size_t>(options_.batch_size) * 8);
        unsigned idle = 0;
        while (!aborted_.load(std::memory_order_acquire)) {
            const auto count = queue_.try_dequeue_bulk(consumer, input.data(), input.size());
            if (!count) {
                if (pending_.load(std::memory_order_acquire) == 0) return;
                // Short initial yields avoid sleeping through small bursts;
                // back off on prolonged starvation rather than burning a core.
                if (++idle < 64) std::this_thread::yield();
                else std::this_thread::sleep_for(std::chrono::microseconds(50));
                continue;
            }
            idle = 0;
            output.clear();
            for (std::size_t i = 0; i < count; ++i) {
                auto m = input[i];
                unsigned depth = 0;
                for (;;) {
                    auto n = candidate_(m).value;
                    u64 follow = 0;
                    // The first inserter owns the sqrt descent. Large states
                    // form a functional graph once the bitmap covers sqrt(B).
                    // Follow one new large successor without a queue round trip;
                    // publish the other children and cap depth for load balance.
                    while (visited_.insert(n)) {
                        ++stat.discovered;
                        if (!follow && n >= visited_.bitmap_limit() && depth < options_.inline_depth)
                            follow = n;
                        else output.push_back(n);
                        n = knuth::integer_sqrt(n);
                    }
                    ++stat.processed;
                    if (!follow) break;
                    m = follow; ++depth; ++stat.inlined;
                }
            }
            if (!output.empty()) {
                // Count unpublished work before making it visible. Input tasks
                // cover inline descendants too, and stay outstanding until the
                // entire batch has finished and all queued children are published.
                pending_.fetch_add(output.size(), std::memory_order_acq_rel);
                if (!queue_.enqueue_bulk(producer, output.data(), output.size())) throw std::bad_alloc();
            }
            ++stat.batches;
            pending_.fetch_sub(count, std::memory_order_acq_rel);
        }
    }
    Options options_;
    knuth::Visited visited_;
    knuth::CandidateGenerator candidate_;
    moodycamel::ConcurrentQueue<u64> queue_;
    alignas(64) std::atomic<u64> pending_{0};
    alignas(64) std::atomic<bool> aborted_{false};
    std::vector<Statistics> stats_;
    u64 seeded_ = 0;
    double elapsed_ = 0;
    std::mutex error_mutex_;
    std::exception_ptr error_;
};
} // namespace

int main(int argc, char** argv) {
    try {
        auto options = options_from_args(argc, argv);
        if (options.candidate) {
            const auto result = knuth::CandidateGenerator(options.bound)(options.candidate);
            std::cout << "candidate=" << result.value << " roots=" << result.roots << '\n';
            return 0;
        }
        Search search(std::move(options));
        search.run();
        search.report();
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}

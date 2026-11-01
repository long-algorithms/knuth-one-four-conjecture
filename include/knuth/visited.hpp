#pragma once
#include "numeric.hpp"
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace knuth {
// SplitMix64's bijective finalizer spreads adjacent integers across shards.
// Reference: https://prng.di.unimi.it/splitmix64.c (public-domain implementation).
inline u64 mix(u64 x) {
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

// Append-only open-addressed set. Zero is an empty slot: search states are
// strictly positive. No tombstones, per-key nodes, or pointer chasing.
class FlatSet {
public:
    void initialize(unsigned shift, std::size_t reserve) {
        shift_ = shift;
        if (reserve) {
            std::size_t capacity = 16;
            while (capacity - capacity / 4 < reserve) capacity = doubled(capacity);
            rehash(capacity);
        }
    }
    bool insert(u64 n, u64 hash) {
        if (!n) throw std::invalid_argument("zero is not a search state");
        if (slots_.empty()) rehash(16);
        auto i = locate(n, hash);
        if (slots_[i]) return false;
        if (size_ >= slots_.size() - slots_.size() / 4) {
            rehash(doubled(slots_.size()));
            i = locate(n, hash);
        }
        slots_[i] = n; ++size_;
        return true;
    }
    std::size_t size() const { return size_; }
    std::size_t capacity() const { return slots_.size(); }
    void append_to(std::vector<u64>& result) const {
        for (auto n : slots_) if (n) result.push_back(n);
    }
private:
    static std::size_t doubled(std::size_t capacity) {
        if (capacity > std::vector<u64>().max_size() / 2) throw std::bad_alloc();
        return capacity * 2;
    }
    std::size_t locate(u64 n, u64 hash) const {
        // The low hash bits already select a shard. Reusing them here would
        // force all keys in that shard into a small subset of initial buckets.
        auto i = static_cast<std::size_t>(hash >> shift_) & (slots_.size() - 1);
        while (slots_[i] && slots_[i] != n) i = (i + 1) & (slots_.size() - 1);
        return i;
    }
    void rehash(std::size_t capacity) {
        std::vector<u64> next(capacity, 0);
        for (auto n : slots_) if (n) {
            auto i = static_cast<std::size_t>(mix(n) >> shift_) & (capacity - 1);
            while (next[i]) i = (i + 1) & (capacity - 1);
            next[i] = n;
        }
        slots_.swap(next);
    }
    unsigned shift_ = 0;
    std::size_t size_ = 0;
    std::vector<u64> slots_;
};

class Visited {
    struct alignas(64) Shard { std::mutex mutex; FlatSet set; };
public:
    Visited(unsigned bitmap_log2, u64 bound, unsigned shard_count, std::size_t reserve)
        : limit_(UINT64_C(1) << bitmap_log2), shard_count_(shard_count),
          shards_(new Shard[shard_count]) {
        if (bound < limit_) limit_ = bound + 1; // avoid overflow at UINT64_MAX
        word_count_ = static_cast<std::size_t>((limit_ + 63) / 64);
        words_.reset(new std::atomic<u64>[word_count_]);
        for (std::size_t i = 0; i < word_count_; ++i)
            words_[i].store(0, std::memory_order_relaxed);
        unsigned shift = 0;
        for (auto n = shard_count; n > 1; n >>= 1) ++shift;
        const auto each = reserve / shard_count + (reserve % shard_count != 0);
        for (unsigned i = 0; i < shard_count; ++i) shards_[i].set.initialize(shift, each);
    }

    bool insert(u64 n) {
        if (!n) throw std::invalid_argument("zero is not a search state");
        if (n < limit_) {
            auto& word = words_[static_cast<std::size_t>(n >> 6)];
            const u64 mask = UINT64_C(1) << (n & 63);
            // Most descendants are already present. Read before issuing an
            // exclusive cache-line transaction; fetch_or still decides ownership.
            if (word.load(std::memory_order_relaxed) & mask) return false;
            return !(word.fetch_or(mask, std::memory_order_relaxed) & mask);
        }
        const auto hash = mix(n);
        auto& shard = shards_[hash & (shard_count_ - 1)];
        std::lock_guard<std::mutex> lock(shard.mutex);
        return shard.set.insert(n, hash);
    }

    // Reporting/snapshots must be called only after joining all workers.
    u64 first_missing() const {
        for (std::size_t i = 0; i < word_count_; ++i) {
            auto missing = ~words_[i].load(std::memory_order_relaxed);
            if (i == 0) missing &= ~UINT64_C(1);
            const auto remaining = limit_ - static_cast<u64>(i) * 64;
            if (remaining < 64) missing &= (UINT64_C(1) << remaining) - 1;
            if (missing) return static_cast<u64>(i) * 64 + __builtin_ctzll(missing);
        }
        return limit_; // sentinel: no missing integer inside the bitmap
    }
    u64 bitmap_limit() const { return limit_; }
    std::size_t bitmap_bytes() const { return word_count_ * sizeof(std::atomic<u64>); }
    std::size_t large_count() const {
        std::size_t n = 0;
        for (unsigned i = 0; i < shard_count_; ++i) n += shards_[i].set.size();
        return n;
    }
    std::size_t large_capacity_bytes() const {
        std::size_t n = 0;
        for (unsigned i = 0; i < shard_count_; ++i) n += shards_[i].set.capacity() * sizeof(u64);
        return n;
    }
    std::vector<u64> snapshot() const {
        std::vector<u64> result;
        for (std::size_t i = 0; i < word_count_; ++i) {
            auto word = words_[i].load(std::memory_order_relaxed);
            while (word) {
                const auto n = static_cast<u64>(i) * 64 + __builtin_ctzll(word);
                if (n && n < limit_) result.push_back(n);
                word &= word - 1;
            }
        }
        for (unsigned i = 0; i < shard_count_; ++i) shards_[i].set.append_to(result);
        return result;
    }
private:
    u64 limit_;
    unsigned shard_count_;
    std::size_t word_count_ = 0;
    std::unique_ptr<std::atomic<u64>[]> words_;
    std::unique_ptr<Shard[]> shards_;
};
} // namespace knuth

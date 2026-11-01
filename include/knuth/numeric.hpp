#pragma once
// Numerical edges are exploratory; integer factorials and square roots are exact.
#include <quadmath.h>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace knuth {
using u64 = std::uint64_t;

// Hardware sqrt provides only a guess. Wide integer products correct both sides,
// including UINT64_MAX, without the divisions in the original correction loop.
inline u64 integer_sqrt(u64 n) {
    auto r = static_cast<u64>(std::sqrt(static_cast<double>(n)));
    while (static_cast<__uint128_t>(r) * r > n) --r;
    while (static_cast<__uint128_t>(r + 1) * (r + 1) <= n) ++r;
    return r;
}

inline __float128 log_factorial(u64 n) {
#ifdef KNUTH_HAVE_LGAMMAF128_R
    int sign;
    return lgammaf128_r(static_cast<__float128>(n) + 1.0Q, &sign);
#else
    // libquadmath lgammaq can write the process-global signgam. Do not race on
    // that variable even though all our arguments have positive gamma values.
    static std::mutex mutex;
    std::lock_guard<std::mutex> guard(mutex);
    return lgammaq(static_cast<__float128>(n) + 1.0Q);
#endif
}

struct Candidate { u64 value; unsigned roots; };

class CandidateGenerator {
public:
    explicit CandidateGenerator(u64 bound)
        : bound_(bound), log_upper_(logq(static_cast<__float128>(bound) + 1.0Q)) {
        if (bound == 0) throw std::invalid_argument("bound must be positive");
    }

    Candidate operator()(u64 m) const {
        if (m == 0) throw std::invalid_argument("candidate input must be positive");
        if (m <= 20) {
            // 20! fits in uint64_t; 21! does not. No transcendental round trip.
            u64 n = 1;
            for (u64 i = 2; i <= m; ++i) n *= i;
            unsigned p = 0;
            while (n > bound_) { n = integer_sqrt(n); ++p; }
            return {n, p};
        }
        const auto x = log_factorial(m);
        // floor(exp(x/2^p)) <= B iff x/2^p < log(B+1). Exponent extraction
        // replaces ceil(log2(x/log(B))) and its extra quad division/logarithm.
        int p = ilogbq(x) - ilogbq(log_upper_);
        if (p < 0) p = 0;
        auto exponent = ldexpq(x, -p);
        if (exponent >= log_upper_) { exponent *= 0.5Q; ++p; }
        auto n = floorq(expq(exponent));
        // Floating rounding can put an estimate on the upper boundary. Keep
        // casts in range, including when B is UINT64_MAX, and descend again.
        while (finiteq(n) && n > static_cast<__float128>(bound_)) {
            exponent *= 0.5Q; ++p;
            n = floorq(expq(exponent));
        }
        if (!finiteq(n) || n < 1.0Q)
            throw std::runtime_error("non-finite or nonpositive numerical candidate");
        return {static_cast<u64>(n), static_cast<unsigned>(p)};
    }
private:
    u64 bound_;
    __float128 log_upper_;
};
} // namespace knuth

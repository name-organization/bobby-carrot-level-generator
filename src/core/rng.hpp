// Seeded random numbers for the level generator.
//
// Own helpers instead of std::uniform_int_distribution and friends: the
// standard leaves their algorithms to the library, and a seed has to give the
// same level natively and in the browser. Integers only, for the same reason.
#pragma once

#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace bc {

class Rng {
public:
    explicit Rng(std::uint32_t seed) : gen_(seed) {}

    std::uint32_t next() { return static_cast<std::uint32_t>(gen_()); }
    std::uint64_t next64() { return (std::uint64_t{next()} << 32) | next(); }

    // Inclusive on both ends.
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1));
    }

    bool chance(double p) { return static_cast<double>(next()) < p * 4294967296.0; }

    template <class T>
    void shuffle(std::vector<T>& v) {
        for (int i = static_cast<int>(v.size()) - 1; i > 0; --i) std::swap(v[i], v[range(0, i)]);
    }

private:
    std::mt19937 gen_;
};

}  // namespace bc
